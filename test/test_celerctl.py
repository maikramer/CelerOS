#!/usr/bin/env python3
"""Testes do celerctl contra um device simulado (protocolo HostLink).

Sem hardware: um FakeDevice espelha o comportamento do firmware (parser
v1/v2 com CRC, ACK seq/total dos chunks, READ com offset) e responde na
hora. Valida o lado host — o lado device e coberto pelos testes C++ do
HostFrame.h (test/cpp/run_tests.cpp) e pela bancada.

    python3 -m unittest test.test_celerctl -v
"""

import io
import json
import os
import socket
import struct
import sys
import tempfile
import threading
import time
import unittest
import zlib
from contextlib import redirect_stderr
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import celerctl as C  # noqa: E402

KL = C.KL
MAGIC = 0x43


def frame_v2(cmd, payload=b"", status=None):
    if status is not None:
        payload = bytes([status]) + payload
    head = struct.pack("<BH", cmd, len(payload))
    return (bytes([MAGIC]) + head + struct.pack("<I", zlib.crc32(head + payload) & 0xFFFFFFFF)
            + payload)


def frame_v1(cmd, payload=b"", status=None):
    if status is not None:
        payload = bytes([status]) + payload
    return bytes([MAGIC, cmd]) + struct.pack("<H", len(payload)) + payload


# ---- fotos de profiling (KL_STATS) sinteticas: A e B separadas por uma
# janela de ~300ms. Runtime por task e cumulativo; total_rt em 2 nucleos
# avanca ~2x a janela. Deltas esperados: IDLE0 +1.000.000us, main +50.000us,
# "nova" nasceu na janela (base 0, rt inteiro).
STATS_SNAP_A = {
    "uptime_us": 60_000_000, "cpu_mhz": 240,
    "heap": {"free": 200_000, "min": 150_000, "largest": 90_000,
             "int_free": 100_000, "int_min": 80_000, "int_largest": 60_000,
             "psram_free": 4_000_000, "psram_total": 8_000_000,
             "psram_min": 3_900_000, "psram_largest": 3_000_000},
    "js": {"active": 1, "launch_free": 180_000, "now_free": 160_000,
           "allocs": 500, "allocs_peak": 700},
    "loop": {"busy_us": 1_000_000, "total_us": 10_000_000},
    "ui": {"frames": 100, "presents": 200, "us": 400_000, "us_max": 8_000},
    "total_rt_us": 55_000_000,
    "tasks": [
        {"n": "main", "s": "R", "p": 1, "stk": 12_000, "rt": 20_000_000},
        {"n": "IDLE0", "s": "r", "p": 0, "stk": 900, "rt": 30_000_000},
        {"n": "wifi", "s": "B", "p": 23, "stk": 4_000, "rt": 5_000_000},
    ],
    "trunc": 0,
}
STATS_SNAP_B = {
    "uptime_us": 60_300_000, "cpu_mhz": 240,
    "heap": {"free": 195_000, "min": 150_000, "largest": 88_000,
             "int_free": 98_000, "int_min": 80_000, "int_largest": 60_000,
             "psram_free": 3_900_000, "psram_total": 8_000_000,
             "psram_min": 3_900_000, "psram_largest": 3_000_000},
    "js": {"active": 1, "launch_free": 180_000, "now_free": 155_000,
           "allocs": 560, "allocs_peak": 700},
    "loop": {"busy_us": 1_050_000, "total_us": 10_100_000},
    "ui": {"frames": 107, "presents": 215, "us": 431_000, "us_max": 9_500},
    "total_rt_us": 56_060_000,
    "tasks": [
        {"n": "main", "s": "R", "p": 1, "stk": 11_800, "rt": 20_050_000},
        {"n": "IDLE0", "s": "r", "p": 0, "stk": 890, "rt": 31_000_000},
        {"n": "wifi", "s": "B", "p": 23, "stk": 4_000, "rt": 5_000_000},
        {"n": "nova", "s": "B", "p": 2, "stk": 2_048, "rt": 10_000},
    ],
    "trunc": 0,
}


class FakeDevice:
    """Espelho minimo do firmware para os testes do host.

    Cada write() do celerctl e um frame completo (o host nunca parte
    frames). `drop_chunk`/`corrupt_chunk` simulam o canal ruim: o frame
    some ou chega com um bit virado (CRC nao fecha — device descarta).
    """

    def __init__(self, serial, proto=2, chunk=4096, win=4):
        self.ser = serial
        self.proto = proto
        self.chunk = chunk
        self.win = win
        self.files = {}           # path -> bytes (conteudo para READ)
        self.dirs = {}            # path -> [nomes] (conteudo para LS)
        self.page_size = 4096     # entradas por pagina de LS (forcar poucas no teste)
        self.wr_path = None
        self.wr_data = None
        self.wr_seq = 0
        self.wr_crc = 0
        self.drop_chunk = []      # seqs cujo WRITE_CHUNK some no caminho (1x cada)
        self.corrupt_chunk = []   # seqs que chegam com 1 byte virado (1x cada)
        # fila de fotos de profiling: cada KL_STATS devolve a proxima (a
        # ultima repete — o celerctl top pede 2+ seguidas)
        self.stats_q = [STATS_SNAP_A]

    # ---- envio de resposta
    def reply(self, cmd, payload=b"", status=0):
        self.ser.rx += frame_v2(cmd, payload, status) if self.proto == 2 \
            else frame_v1(cmd, payload, status)

    # ---- recepcao
    def _lost_in_transit(self, data):
        """True se o frame nao chega integro ao device (drop/corrupt)."""
        if data[0] != MAGIC or data[1] != KL["WRITE_CHUNK"] or self.proto != 2:
            return False
        (seq,) = struct.unpack("<H", data[8:10])
        if seq in self.drop_chunk:
            self.drop_chunk.remove(seq)
            return True  # sumiu: nenhum ACK, host reenvia no timeout
        if seq in self.corrupt_chunk:
            self.corrupt_chunk.remove(seq)
            return True  # bit virado: CRC do frame nao fecha, device descarta
        return False

    def on_bytes(self, data):
        if self._lost_in_transit(data):
            return
        cmd, payload, ok = self.parse(data)
        if not ok:
            return  # CRC invalido: firmware descarta e espera reenvio
        getattr(self, "op_%02x" % cmd, lambda p: self.reply(cmd, b"opcode?", 1))(payload)

    def parse(self, data):
        if data[0] != MAGIC:
            return None, None, False
        cmd = data[1]
        (ln,) = struct.unpack("<H", data[2:4])
        off = 4
        if self.proto == 2:
            (crc,) = struct.unpack("<I", data[4:8])
            off = 8
            if crc != zlib.crc32(data[1:4] + data[off:off + ln]) & 0xFFFFFFFF:
                # fallback do firmware (HostFrame.h): HELLO v1 de um host
                # que ainda nao conhece a versao chega com "CELE" no lugar
                # do crc — devolve os bytes ao payload e cai para proto 1
                if cmd == 0x01 and struct.pack("<I", crc) == b"CELE":
                    self.proto = 1
                    return cmd, b"CELE" + data[off:off + ln], True
                return None, None, False
        return cmd, data[off:off + ln], True

    # ---- handlers
    def op_01(self, p):  # HELLO
        v2 = p == b"CELERCTL2"
        if v2:
            ident = f"CelerOS 9.9|test|api 99|proto 2|chunk {self.chunk}|win {self.win}"
        else:
            ident = "CelerOS 9.9|test|api 99|proto 1"
        # resposta sai no formato VIGENTE da sessao; a troca para v2 vale
        # para o frame SEGUINTE (espelho do handleHello do firmware)
        self.reply(KL["HELLO"], ident.encode())
        self.proto = 2 if v2 else 1

    def op_20(self, p):  # STATS: foto de profiling (JSON)
        snap = self.stats_q.pop(0) if len(self.stats_q) > 1 else self.stats_q[0]
        self.reply(KL["STATS"], json.dumps(snap).encode())

    def op_06(self, p):  # WRITE_BEGIN
        self.wr_path = p.rstrip(b"\0").decode()
        self.wr_data = bytearray()
        self.wr_seq = 0
        self.wr_crc = 0
        self.reply(KL["WRITE_BEGIN"])

    def op_07(self, p):  # WRITE_CHUNK
        if self.wr_data is None:
            self.reply(KL["WRITE_CHUNK"], b"escrita nao iniciada", 1)
            return
        if self.proto == 2:
            (seq,) = struct.unpack("<H", p[:2])
            if seq != self.wr_seq:
                # duplicado/atrasado: ACK aponta o proximo esperado, sem aplicar
                self.reply(KL["WRITE_CHUNK"], struct.pack("<HI", self.wr_seq, len(self.wr_data)))
                return
            data = p[2:]
            self.wr_seq += 1
        else:
            data = p
        self.wr_data += data
        self.wr_crc = zlib.crc32(bytes(data), self.wr_crc) & 0xFFFFFFFF
        if self.proto == 2:
            self.reply(KL["WRITE_CHUNK"], struct.pack("<HI", self.wr_seq, len(self.wr_data)))
        else:
            self.reply(KL["WRITE_CHUNK"])

    def op_08(self, p):  # WRITE_END
        if self.wr_data is None:
            self.reply(KL["WRITE_END"], b"escrita nao iniciada", 1)
            return
        total = len(self.wr_data)
        if self.proto == 2:
            crc_host, size_host = struct.unpack("<II", p[:8]) if len(p) >= 8 else (0, 0xFFFFFFFF)
            if crc_host != self.wr_crc or size_host != total:
                self.wr_data = None
                self.reply(KL["WRITE_END"], b"crc/tamanho divergem no fim da escrita", 1)
                return
            self.files[self.wr_path] = bytes(self.wr_data)
            self.wr_data = None
            self.reply(KL["WRITE_END"], struct.pack("<II", total, self.wr_crc))
        else:
            self.files[self.wr_path] = bytes(self.wr_data)
            self.wr_data = None
            self.reply(KL["WRITE_END"], struct.pack("<I", total))

    def op_05(self, p):  # READ: path\0 + u32 offset + u32 want
        z = p.index(b"\0")
        path = p[:z].decode()
        off, want = struct.unpack("<II", p[z + 1:z + 9])
        data = self.files.get(path, b"")
        body = data[off:off + want]
        if self.proto == 2:
            self.reply(KL["READ"], struct.pack("<I", off) + body)
        else:
            self.reply(KL["READ"], body)

    def op_04(self, p):  # STAT: path -> exists,isDir,u32 size,u32 mtime
        path = p.rstrip(b"\0").decode()
        data = self.files.get(path)
        if data is None:
            self.reply(KL["STAT"], b"\x00")
        else:
            self.reply(KL["STAT"], struct.pack("<BBI", 1, 0, len(data)) + b"\0" * 4)

    def op_03(self, p):  # LS: path\0 [u32 cursor] -> [u32 next] u16 n + entradas
        z = p.index(b"\0")
        path = p[:z].decode()
        rest = p[z + 1:]
        names = sorted(self.dirs.get(path, []))
        if len(rest) >= 4:  # paginado
            (cursor,) = struct.unpack("<I", rest[:4])
            page = names[cursor:cursor + self.page_size]
            nxt = 0 if cursor + len(page) >= len(names) else cursor + len(page)
            head = struct.pack("<IH", nxt, len(page))
        else:  # pedido antigo: tudo num frame
            page = names
            head = struct.pack("<H", len(page))
        body = b""
        for name in page:
            data = self.files.get(f"{path}/{name}")
            rec = struct.pack("<BIIB", 0 if data is not None else 1,
                              len(data or b""), 0, len(name))
            body += rec + name.encode()
        self.reply(KL["LS"], head + body)

    def op_09(self, p):  # DELETE: path\0 [u8 1=recursivo]
        z = p.index(b"\0")
        path = p[:z].decode()
        recursive = p[z + 1:] == b"\x01"
        if recursive:
            for k in [k for k in self.files if k.startswith(path + "/")]:
                del self.files[k]
            for k in [k for k in self.dirs if k == path or k.startswith(path + "/")]:
                del self.dirs[k]
        else:
            self.files.pop(path, None)
            self.dirs.pop(path, None)
        self.reply(KL["DELETE"])


class FakeSerial:
    def __init__(self, device):
        self.dev = device
        self.rx = b""
        self.timeout = 3.0
        self.port = "FAKE"
        self.baudrate = C.DEFAULT_BAUD

    def write(self, data):
        self.dev.on_bytes(data)
        return len(data)

    def read(self, n):
        out, self.rx = self.rx[:n], self.rx[n:]
        return out

    def reset_input_buffer(self):
        self.rx = b""

    def close(self):
        pass


def make_link(proto=2, chunk=4096, win=4):
    ser = FakeSerial(None)
    dev = FakeDevice(ser, proto=proto, chunk=chunk, win=win)
    ser.dev = dev
    link = C.HostLink.__new__(C.HostLink)  # sem porta real
    link.ser = ser
    link.timeout = 3.0
    link.push_queue = []
    link.proto = 1
    link.max_chunk = C.CHUNK
    link.win = 1
    return link, dev


def tmpfile(payload):
    f = tempfile.NamedTemporaryFile(delete=False)
    f.write(payload)
    f.close()
    return f.name


class TestProto2(unittest.TestCase):
    def test_hello_negocia_v2(self):
        link, dev = make_link()
        ident = link.hello()
        self.assertEqual(link.proto, 2)
        self.assertEqual(link.max_chunk, 4096)
        self.assertEqual(link.win, 4)
        self.assertIn("proto 2", ident)

    def test_push_completo_e_crc(self):
        link, dev = make_link()
        link.hello()
        payload = bytes(range(256)) * 700  # 179200 bytes ~ 44 chunks
        path = tmpfile(payload)
        try:
            link.write_file(path, "/local/x.bin", progress=False)
        finally:
            os.unlink(path)
        self.assertEqual(dev.files["/local/x.bin"], payload)

    def test_push_com_chunk_perdido_recupera(self):
        link, dev = make_link()
        link.hello()
        payload = b"z" * 40000  # ~10 chunks
        path = tmpfile(payload)
        try:
            dev.drop_chunk = [3]  # o chunk 3 some no caminho (1x)
            link.write_file(path, "/local/y.bin", progress=False)
        finally:
            os.unlink(path)
        self.assertEqual(dev.files["/local/y.bin"], payload)  # sem buraco/duplicacao

    def test_push_com_corrupcao_cai_no_crc(self):
        link, dev = make_link()
        link.hello()
        payload = b"q" * 12000
        path = tmpfile(payload)
        try:
            dev.corrupt_chunk = [1]  # byte virado: CRC do frame rejeita, host reenvia
            link.write_file(path, "/local/z.bin", progress=False)
        finally:
            os.unlink(path)
        self.assertEqual(dev.files["/local/z.bin"], payload)

    def test_pull_pipelined(self):
        link, dev = make_link()
        link.hello()
        payload = bytes((i * 13) & 0xFF for i in range(60000))
        dev.files["/local/big.bin"] = payload
        fd, path = tempfile.mkstemp()
        os.close(fd)
        try:
            link.read_file("/local/big.bin", path, progress=False)
            with open(path, "rb") as f:
                self.assertEqual(f.read(), payload)
        finally:
            os.unlink(path)

    def test_caminho_legado_proto1(self):
        # --proto 1 do CLI: host fala v1 de proposito com firmware novo
        C.FORCE_PROTO1 = True
        try:
            link, dev = make_link()
            link.hello()
            self.assertEqual(link.proto, 1)
            self.assertEqual(link.win, 1)
            payload = b"a" * 9000
            path = tmpfile(payload)
            try:
                link.write_file(path, "/local/v1.bin", progress=False)  # stop-and-wait
            finally:
                os.unlink(path)
            self.assertEqual(dev.files["/local/v1.bin"], payload)
            # pull v1 (stop-and-wait) tambem fecha
            dev.files["/local/b.bin"] = payload
            fd, path = tempfile.mkstemp()
            os.close(fd)
            try:
                link.read_file("/local/b.bin", path, progress=False)
                with open(path, "rb") as f:
                    self.assertEqual(f.read(), payload)
            finally:
                os.unlink(path)
        finally:
            C.FORCE_PROTO1 = False


    def test_ls_paginado_e_rm_recursivo(self):
        link, dev = make_link()
        link.hello()
        dev.page_size = 5  # forca 3 paginas
        names = [f"app{i:02d}" for i in range(12)]
        dev.dirs["/local/apps"] = list(names)
        for i, n in enumerate(names):
            dev.files[f"/local/apps/{n}"] = b"x" * i
        entries = link.ls("/local/apps")
        self.assertEqual([e["name"] for e in entries], names)
        self.assertTrue(all(not e["dir"] for e in entries))
        # rm -r: um comando apaga a arvore inteira
        link.delete("/local/apps", recursive=True)
        self.assertNotIn("/local/apps", dev.dirs)
        self.assertEqual([k for k in dev.files if k.startswith("/local/apps/")], [])


class TestDebugProxy(unittest.TestCase):
    """split_frames: o leitor nao-bloqueante do `celerctl debug`."""

    def test_frames_inteiros_e_lixo_de_console(self):
        dbg = KL["DEBUG_DATA"]
        wire = b"I (123) wifi: texto do console\r\n" + frame_v2(dbg, b"\x04\x81\x01\x00") + frame_v2(dbg, b"abc")
        frames, rest = C.split_frames(wire, 2)
        self.assertEqual(frames, [(dbg, b"\x04\x81\x01\x00"), (dbg, b"abc")])
        self.assertEqual(rest, b"")

    def test_frame_pela_metade_espera_o_resto(self):
        dbg = KL["DEBUG_DATA"]
        full = frame_v2(dbg, bytes(range(200)))
        got, buf = [], b""
        for i in range(len(full)):  # byte a byte: nada se perde no meio
            buf += full[i:i + 1]
            frames, buf = C.split_frames(buf, 2)
            got += frames
        self.assertEqual(got, [(dbg, bytes(range(200)))])

    def test_crc_errado_descarta_e_resincroniza(self):
        dbg = KL["DEBUG_DATA"]
        bad = bytearray(frame_v2(dbg, b"xyz"))
        bad[-1] ^= 0xFF
        frames, rest = C.split_frames(bytes(bad) + frame_v2(dbg, b"ok"), 2)
        self.assertEqual(frames, [(dbg, b"ok")])

    def test_keep_push_nao_perde_frames_do_debugger(self):
        # attach do app chega ENTRE dois comandos: o xfer seguinte nao pode
        # limpar a entrada/fila, e a resposta velha do keepalive e descartada
        link, dev = make_link()
        link.hello()
        link.keep_push = True
        dbg = frame_v2(KL["DEBUG_DATA"], b"2 20700 v2.7.0 alvo\n")
        stale = frame_v2(KL["HELLO"], b"proto 2", status=0)
        link.ser.rx += dbg + stale
        link.xfer(KL["WRITE_BEGIN"], b"/local/x\0")
        self.assertEqual(link.push_queue, [(KL["DEBUG_DATA"], b"2 20700 v2.7.0 alvo\n")])

    def test_sem_keep_push_continua_limpando(self):
        link, dev = make_link()
        link.hello()
        link.ser.rx += frame_v2(KL["DEBUG_DATA"], b"x")
        link.xfer(KL["WRITE_BEGIN"], b"/local/x\0")
        self.assertEqual(link.push_queue, [])

    def test_proto1(self):
        frames, _ = C.split_frames(frame_v1(KL["LOG_DATA"], b"\x00linha\n"), 1)
        self.assertEqual(frames, [(KL["LOG_DATA"], b"\x00linha\n")])


class TestStats(unittest.TestCase):
    """KL_STATS (celerctl top/stats): round-trip do protocolo e a matematica
    de delta (CPU% por task, fps, busy%) feita no host."""

    def test_stats_round_trip(self):
        link, dev = make_link()
        link.hello()
        snap = link.stats()
        self.assertEqual(snap["uptime_us"], 60_000_000)
        self.assertEqual(snap["heap"]["psram_total"], 8_000_000)
        self.assertEqual(len(snap["tasks"]), 3)
        self.assertEqual(snap["tasks"][0]["n"], "main")

    def test_delta_cpu_por_task(self):
        rows, total = C._task_rows(STATS_SNAP_A, STATS_SNAP_B)
        self.assertEqual(total, 1_060_000)  # 56.060.000 - 55.000.000
        by_name = {r["n"]: r for r in rows}
        self.assertEqual(by_name["IDLE0"]["drt"], 1_000_000)
        self.assertAlmostEqual(by_name["IDLE0"]["cpu"], 100.0 * 1_000_000 / 1_060_000)
        self.assertEqual(by_name["main"]["drt"], 50_000)
        self.assertEqual(by_name["wifi"]["drt"], 0)  # nada rodou na janela
        self.assertEqual(by_name["nova"]["drt"], 10_000)  # nasceu: base 0
        # soma das taxas ~ nucleos x 100 (2 cores)
        self.assertAlmostEqual(sum(r["cpu"] for r in rows), 100.0 * 1_060_000 / 1_060_000)

    def test_render_top(self):
        text = C._render_top(STATS_SNAP_A, STATS_SNAP_B, 0.3)
        self.assertIn("CPU 240 MHz", text)
        self.assertIn("janela 300 ms", text)
        self.assertIn("PSRAM", text)
        self.assertIn("app JS", text)  # js.active
        # ordenado por CPU: IDLE0 (janela dominante) antes de main
        self.assertLess(text.index("IDLE0"), text.index("main"))
        # taxa da UI na janela: 7 quadros em 300ms ~ 23 fps
        self.assertIn("23 fps", text)
        # task nascida na janela aparece
        self.assertIn("nova", text)

    def test_render_top_limite_e_ordenacao(self):
        text = C._render_top(STATS_SNAP_A, STATS_SNAP_B, 0.3, sort="stack", limit=2)
        self.assertNotIn("main", text)  # maior stack: cortada pelo -n 2
        # stack crescente: IDLE0 (890) antes de nova (2048)
        self.assertLess(text.index("IDLE0"), text.index("nova"))

    def test_resumo_loop_parado_sem_app(self):
        # sem foto anterior e sem app: nao renderiza linhas de taxa
        a = dict(STATS_SNAP_A, js={"active": 0, "launch_free": 0, "now_free": 0,
                                  "allocs": 0, "allocs_peak": 0})
        lines = C._summary_lines(a)
        self.assertTrue(any("heap" in ln for ln in lines))
        self.assertFalse(any("app JS" in ln for ln in lines))

    def test_firmware_antigo_recusado_com_mensagem(self):
        link, dev = make_link()
        link.hello()
        # firmware anterior a rodada: responde "opcode desconhecido"
        dev.op_20 = lambda p: dev.reply(KL["STATS"], b"opcode desconhecido", 1)
        err = io.StringIO()
        with redirect_stderr(err), self.assertRaises(SystemExit):
            C._poll_stats(link)
        self.assertIn("firmware antigo", err.getvalue())


# ---------------------------------------------------------------- bridge WiFi
# Celer Debug Bridge: servidor TCP de verdade em 127.0.0.1 fazendo o
# handshake do firmware (banner + AUTH por token) e casando com o FakeDevice
# — valida o NetTransport do celerctl sem hardware.

class SockSerial:
    """FakeSerial conversando por socket: o reply() do device vira sendall."""

    def __init__(self, conn):
        self.conn = conn
        self.dev = None
        self.port = "FAKE_TCP"
        self.baudrate = None  # como o bridge: sem baud
        self.timeout = 3.0
        self.rx = b""

    def write(self, data):
        self.dev.on_bytes(data)
        if self.rx:
            out, self.rx = self.rx, b""
            self.conn.sendall(out)
        return len(data)

    def read(self, n):
        return self.conn.recv(n)

    def reset_input_buffer(self):
        pass

    def close(self):
        try:
            self.conn.close()
        except OSError:
            pass


class FakeBridge(threading.Thread):
    """Espelho do main/USBDevice/DebugBridge.cpp: banner "CELERBRIDGE 1",
    AUTH com 3 tentativas (NO/NO/ERR) e pipe binario para o FakeDevice."""

    TOKEN = "tok12345"

    def __init__(self, proto=2, chunk=4096, win=4):
        super().__init__(daemon=True)
        self.proto = proto
        self.chunk = chunk
        self.win = win
        self.auth_fails = []
        self.dev = None
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(2)
        self.srv.settimeout(10.0)
        self.port = self.srv.getsockname()[1]

    def run(self):
        try:
            conn, _ = self.srv.accept()
        except OSError:
            return
        with conn:
            conn.sendall(f"CELERBRIDGE 1 test 127.0.0.1 {self.port}\n".encode())
            if not self._auth(conn):
                return
            ser = SockSerial(conn)
            dev = FakeDevice(ser, proto=self.proto, chunk=self.chunk, win=self.win)
            ser.dev = dev
            self.dev = dev
            self._pipe(conn, ser)

    def _auth(self, conn):
        buf = b""
        fails = 0
        deadline = time.monotonic() + 5.0
        conn.settimeout(0.25)
        while time.monotonic() < deadline:
            try:
                data = conn.recv(64)
            except socket.timeout:
                continue
            if not data:
                return False
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode().strip()
                if text == f"AUTH {self.TOKEN}":
                    conn.sendall(b"OK\n")
                    return True
                self.auth_fails.append(text)
                fails += 1
                if fails >= 3:
                    conn.sendall(b"ERR\n")
                    return False
                conn.sendall(b"NO\n")
        return False

    def _pipe(self, conn, ser):
        # frames chegam inteiros no loopback (1 sendall = 1 recv), mas o
        # remontador custa pouco e cobre o caso de o kernel partir mesmo assim
        pending = b""
        first = True
        conn.settimeout(10.0)
        while True:
            try:
                data = conn.recv(65536)
            except OSError:
                return
            if not data:
                return
            pending += data
            while len(pending) >= 4:
                (ln,) = struct.unpack("<H", pending[2:4])
                v2 = (not first) and ser.dev.proto == 2
                total = (8 if v2 else 4) + ln
                if len(pending) < total:
                    break
                frame, pending = pending[:total], pending[total:]
                first = False
                ser.write(frame)


class TestNetBridge(unittest.TestCase):

    def setUp(self):
        # cache de tokens num tmp: nao suja o ~/.config de quem roda o teste
        self.tmp = tempfile.mkdtemp()
        self.old_xdg = os.environ.get("XDG_CONFIG_HOME")
        os.environ["XDG_CONFIG_HOME"] = self.tmp
        self.old_env_tok = os.environ.get("CELEROS_BRIDGE_TOKEN")
        os.environ.pop("CELEROS_BRIDGE_TOKEN", None)

    def tearDown(self):
        if self.old_xdg is None:
            os.environ.pop("XDG_CONFIG_HOME", None)
        else:
            os.environ["XDG_CONFIG_HOME"] = self.old_xdg
        if self.old_env_tok is not None:
            os.environ["CELEROS_BRIDGE_TOKEN"] = self.old_env_tok

    def start(self, **kw):
        br = FakeBridge(**kw)
        br.start()
        return br

    def test_endereco(self):
        self.assertTrue(C.is_net_addr("192.168.0.10"))
        self.assertTrue(C.is_net_addr("10.0.0.2:5555"))
        self.assertFalse(C.is_net_addr("/dev/ttyUSB0"))
        self.assertFalse(C.is_net_addr("K303AE"))  # serial-prefixo do S3
        self.assertEqual(C.split_net_addr("192.168.0.10"), ("192.168.0.10", 5555))
        self.assertEqual(C.normalize_net_addr("192.168.0.10"), "192.168.0.10:5555")

    def test_token_cache(self):
        addr = "192.168.0.10:5555"
        C.save_token(addr, "cache123")
        self.assertEqual(C.load_token_cache().get(addr), "cache123")
        self.assertEqual(C.net_token(addr, prompt=False), "cache123")
        os.environ["CELEROS_BRIDGE_TOKEN"] = "env123"
        self.assertEqual(C.net_token(addr, prompt=False), "env123")  # env vence
        os.environ.pop("CELEROS_BRIDGE_TOKEN", None)
        with self.assertRaises(C.CelerError):
            C.net_token("10.9.9.9:5555", prompt=False)  # sem cache, sem prompt

    def test_auth_e_hello_proto2(self):
        br = self.start()
        link = C.HostLink(f"127.0.0.1:{br.port}", token=FakeBridge.TOKEN)
        try:
            ident = link.hello()
            self.assertIn("proto 2", ident)
            self.assertIn("win 4", ident)
        finally:
            link.close()

    def test_token_errado_recusado(self):
        br = self.start()
        with self.assertRaises(C.CelerError) as ctx:
            C.HostLink(f"127.0.0.1:{br.port}", token="token-errado")
        self.assertIn("auth", str(ctx.exception))
        self.assertEqual(br.auth_fails, ["AUTH token-errado"])

    def test_banner_de_outro_servico(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", 0))
        srv.listen(1)
        srv.settimeout(5.0)
        port = srv.getsockname()[1]

        def say_hi():
            conn, _ = srv.accept()
            conn.sendall(b"SSH-2.0-OpenSSH\r\n")
            conn.close()

        threading.Thread(target=say_hi, daemon=True).start()
        with self.assertRaises(C.CelerError):
            C.HostLink(f"127.0.0.1:{port}", token="x")
        srv.close()

    def test_push_e_pull_por_tcp(self):
        br = self.start()
        link = C.HostLink(f"127.0.0.1:{br.port}", token=FakeBridge.TOKEN)
        try:
            link.hello()
            payload = os.urandom(9000)  # ~2 chunks com janela 4
            src = tmpfile(payload)
            link.write_file(src, "/local/arquivo.bin", progress=False)
            self.assertEqual(br.dev.files["/local/arquivo.bin"], payload)
            dest = os.path.join(self.tmp, "volta.bin")
            link.read_file("/local/arquivo.bin", dest, progress=False)
            with open(dest, "rb") as f:
                self.assertEqual(f.read(), payload)
        finally:
            link.close()

    def test_sem_token_nao_pergunta_em_probe(self):
        br = self.start()
        # probe (devices): prompt=False -> falha limpa, sem travar no input()
        self.assertIsNone(C.probe(f"127.0.0.1:{br.port}", fast=True))


if __name__ == "__main__":
    unittest.main()
