#!/usr/bin/env python3
r"""
celerctl - ferramenta de depuracao/manutencao do CelerOS via USB (estilo adb).

Conversa com o firmware pelo canal HostLink: na pratica a UART do CH340
(USB do PC -> /dev/ttyUSB*) ou, em placas com USB nativo, a CDC1. Os opcodes
sao lidos diretamente de main/USBDevice/HostLink.h para manter os dois
lados em sincronia.

Comandos:
  devices [-l]                lista placas CelerOS conectadas
  info                        versao/board/heap/rede/filesystems
  shell [cmd...]              shell interativo (ou executa um comando)
  ls [-l] CAMINHO             lista diretorio (/local ou /sd)
  cat ARQUIVO                 escreve conteudo no stdout
  rm / mkdir / mv             operacoes de arquivo
  push LOCAL REMOTO           envia arquivo para o dispositivo
  pull REMOTO [LOCAL]         baixa arquivo do dispositivo
  reboot                      reinicia a placa
  logcat [--dump]              logs: stream ao vivo ou absorve o buffer (--dump)
  ota push FW.bin [--no-reboot]  grava firmware pela serial (sem esptool)
  screencap [SAIDA.png]       captura da tela do dispositivo
  tap X Y [ms]                injeta um toque (navegar pela UI via USB)
  swipe X0 Y0 X1 Y1 [ms]      injeta um arrasto (scroll/troca de pagina)
  dev PASTA                   loop de desenvolvimento: assiste a pasta, reinstala
                              o que mudou e relanca o app (exit+run), com logs
                              ao vivo (Ctrl-C sai)
  apps list                   lista apps instalados
  apps install PASTA [--run]  instala (lint antes); --run abre o app ao final
  apps pull NOME [DESTINO]    baixa um app instalado para o PC (backup)
  apps rm NOME                remove um app instalado

Exemplos:
  python3 tools/celerctl.py devices
  python3 tools/celerctl.py shell ls /local
  python3 tools/celerctl.py -b 921600 push firmware.bin /sd/fw.bin
  python3 tools/celerctl.py dev hub_apps/Celer Remote

Dependencias: pyserial (pip install -r tools/requirements.txt)
"""

import argparse
import json
import os
import re
import struct
import sys
import time
import zlib
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("erro: pyserial nao instalado (pip install -r tools/requirements.txt)")
    sys.exit(1)

# VIDs que podem hospedar o canal: CDC nativa do ESP32-S3 ou bridges USB-UART
# comuns (CH340 do SmartDisplay/CYD). A identificacao real e por probing HELLO.
USB_VIDS = (0x303A, 0x1A86, 0x10C4)
CHUNK = 4096  # tamanho maximo de payload HostLink (proto 1)
DEFAULT_BAUD = 115200
FORCE_PROTO1 = False  # --proto 1: valida o caminho legado sem CRC/janela


class CelerError(Exception):
    pass


def load_opcodes():
    """Extrai os opcodes KL_* do header do firmware (fonte unica)."""
    header = Path(__file__).resolve().parent.parent / "main" / "USBDevice" / "HostLink.h"
    ops = {}
    for name, value in re.findall(r"KL_(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", header.read_text()):
        ops[name] = int(value, 16)
    missing = {"HELLO", "INFO", "LS", "STAT", "READ", "WRITE_BEGIN", "WRITE_CHUNK",
               "WRITE_END", "DELETE", "MKDIR", "RENAME", "EXEC", "REBOOT", "EXEC_CONT",
               "SET_BAUD", "TOUCH"} - set(ops)
    if missing:
        raise CelerError(f"opcodes ausentes em {header}: {missing}")
    return ops


KL = load_opcodes()


def read_exact(ser, n, timeout):
    """Le exatamente n bytes (bloqueando ate timeout)."""
    buf = b""
    deadline = time.monotonic() + timeout
    while len(buf) < n:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise CelerError("timeout lendo do dispositivo")
        ser.timeout = min(remaining, 1.0)
        chunk = ser.read(n - len(buf))
        if chunk:
            buf += chunk
    return buf


class HostLink:
    """Cliente do protocolo HostLink sobre a CDC1 ou a UART do CH340.

    Dois formatos por sessao (ver main/USBDevice/HostFrame.h):
      proto 1: [43][cmd][len u16][payload]
      proto 2: [43][cmd][len u16][crc32 u32][payload]   crc sobre cmd+len+payload
    O hello() negocia: "CELERCTL2" ativa proto 2 (resposta traz
    "proto 2|chunk W|win K"); firmware antigo responde "proto 1" e a
    sessao segue em v1 (sem CRC, stop-and-wait — como sempre funcionou).
    """

    def __init__(self, port, timeout=3.0, baud=DEFAULT_BAUD):
        self.ser = serial.Serial(port, baud, timeout=timeout, write_timeout=timeout)
        self.timeout = timeout
        self.push_queue = []  # frames nao-solicitados (logs) que chegaram no meio de um xfer
        # preenchidos pelo hello() a partir do que o device anuncia
        self.proto = 1
        self.max_chunk = CHUNK
        self.win = 1

    def close(self):
        self.ser.close()

    def _frame(self, cmd, payload=b""):
        if self.proto == 2:
            head = struct.pack("<BH", cmd, len(payload))
            crc = zlib.crc32(head + payload) & 0xFFFFFFFF
            return b"\x43" + head + struct.pack("<I", crc) + payload
        return bytes([0x43, cmd]) + struct.pack("<H", len(payload)) + payload

    def _read_frame(self, expect_cmd=None, timeout=None):
        timeout = timeout or self.timeout
        # procura o magic 0x43 um byte por vez (resincroniza se houver lixo)
        while True:
            first = read_exact(self.ser, 1, timeout)
            if first[0] != 0x43:
                continue
            head = first + read_exact(self.ser, 3, timeout)
            cmd, length = head[1], struct.unpack("<H", head[2:4])[0]
            if self.proto == 2:
                crc = struct.unpack("<I", read_exact(self.ser, 4, timeout))[0]
                payload = read_exact(self.ser, length, timeout) if length else b""
                want = zlib.crc32(head[1:4] + payload) & 0xFFFFFFFF
                if crc != want:
                    continue  # frame corrompido: descarta e caca o proximo magic
            else:
                payload = read_exact(self.ser, length, timeout) if length else b""
            return cmd, payload

    # comandos que podem ser reenviados sem efeito colateral (retry do xfer)
    _IDEMPOTENT = {"HELLO", "INFO", "LS", "STAT", "READ", "MKDIR", "DELETE", "RENAME",
                   "LOG_ON", "LOG_OFF", "TOUCH", "SCREENSHOT", "COREDUMP"}

    def xfer(self, cmd, payload=b"", timeout=None, retries=None):
        """Envia um comando e retorna (cmd_resposta, payload_resposta).

        Frames nao-solicitados (logs do logcat) que chegarem no meio do
        caminho sao guardados em push_queue em vez de confundir a resposta.
        Comandos idempotentes ganham retry com re-hello (sessao pode ter
        caido no idle de 8s da UART entre uma chamada e outra).
        """
        name = next((k for k, v in KL.items() if v == cmd and not k.endswith("_DATA")
                     and k != "EXEC_CONT"), None)
        if retries is None:
            retries = 1 if name in self._IDEMPOTENT else 0
        last_err = None
        for attempt in range(retries + 1):
            if attempt > 0:
                time.sleep(0.2)
                try:
                    self.hello(force_v1=self.proto == 1)
                except (CelerError, serial.SerialException):
                    continue
            frame = self._frame(cmd, payload)
            try:
                self.ser.reset_input_buffer()
                self.push_queue = []
                self.ser.write(frame)
                cmd_r, payload_r = self._read_frame(timeout=timeout)
                while cmd_r != cmd and cmd_r in (KL["LOG_DATA"], KL["SCR_DATA"]):
                    self.push_queue.append((cmd_r, payload_r))
                    cmd_r, payload_r = self._read_frame(timeout=timeout)
                if cmd_r == 0x00 or (payload_r and payload_r[0] == 1):
                    raise CelerError(payload_r[1:].decode("utf-8", "replace") or
                                     "erro no dispositivo")
                return cmd_r, payload_r
            except CelerError as e:
                last_err = e
                if "timeout" not in str(e):
                    raise
        raise last_err if last_err else CelerError("timeout lendo do dispositivo")

    # ---------------------------------------------------------------- comandos

    def hello(self, force_v1=False):
        magic = b"CELERCTL1" if (force_v1 or FORCE_PROTO1) else b"CELERCTL2"
        cmd, payload = self.xfer(KL["HELLO"], magic, timeout=1.0, retries=0)
        if cmd != KL["HELLO"]:
            raise CelerError("resposta inesperada ao HELLO")
        ident = payload[1:].decode("utf-8", "replace")
        self.proto = 1
        self.max_chunk = CHUNK
        self.win = 1
        for field in ident.split("|"):
            key, _, value = field.partition(" ")
            if key == "proto" and value == "2":
                self.proto = 2
            elif key == "chunk":
                self.max_chunk = max(64, min(int(value), 0xFFFF))
            elif key == "win":
                self.win = max(1, min(int(value), 64))
        return ident

    def keepalive(self):
        """HELLO no formato da sessao vigente (dev loop)."""
        self.xfer(KL["HELLO"], b"CELERCTL1" if self.proto == 1 else b"CELERCTL2",
                  timeout=1.0, retries=0)

    def info(self):
        _, payload = self.xfer(KL["INFO"])
        return json.loads(payload[1:].decode())

    def ls(self, path):
        entries = []
        cursor = 0
        while True:
            req = path.encode() + b"\0"
            if self.proto == 2:
                req += struct.pack("<I", cursor)  # paginacao: dirs grandes de graça
            _, payload = self.xfer(KL["LS"], req)
            body = payload[1:]
            if self.proto == 2:
                (cursor,) = struct.unpack("<I", body[:4])
                body = body[4:]
            (count,) = struct.unpack("<H", body[:2])
            off = 2
            for _ in range(count):
                is_dir = body[off]
                size, mtime = struct.unpack("<II", body[off + 1:off + 9])
                name_len = body[off + 9]
                name = body[off + 10:off + 10 + name_len].decode("utf-8", "replace")
                entries.append({"dir": bool(is_dir), "size": size, "mtime": mtime, "name": name})
                off += 10 + name_len
            if self.proto == 1 or cursor == 0:
                return entries  # 0 = fim (ou v1: tudo num frame)

    def delete(self, path, recursive=False):
        payload = path.encode() + b"\0"
        if recursive and self.proto == 2:
            payload += b"\x01"  # device apaga a arvore em um comando
        self.xfer(KL["DELETE"], payload)

    def stat(self, path):
        _, payload = self.xfer(KL["STAT"], path.encode() + b"\0")
        body = payload[1:]
        if body[0] == 0:
            return None
        size, mtime = struct.unpack("<II", body[2:10])
        return {"dir": bool(body[1]), "size": size, "mtime": mtime}

    def read_chunk(self, path, offset, want):
        req = path.encode() + b"\0" + struct.pack("<II", offset, want)
        _, payload = self.xfer(KL["READ"], req, timeout=10.0)
        body = payload[1:]
        if self.proto == 2:
            # resposta prefixada com o offset (casa mesmo fora de ordem)
            (got_off,) = struct.unpack("<I", body[:4])
            if got_off != offset:
                raise CelerError(f"READ voltou offset {got_off}, pedi {offset}")
            body = body[4:]
        return body

    def exec(self, line):
        _, payload = self.xfer(KL["EXEC"], line.encode(), timeout=15.0)
        body = payload[1:]
        exit_code = body[0]
        (out_len,) = struct.unpack("<I", body[1:5])
        out = body[5:]
        # saida grande chega em frames de continuacao (logs do logcat que
        # intercalam sao guardados, nao abortam — mesmo tratamento do
        # screenshot/coredump)
        while len(out) < out_len:
            cmd, more = self._read_frame(timeout=15.0)
            if cmd in (KL["LOG_DATA"], KL["SCR_DATA"]):
                self.push_queue.append((cmd, more))
                continue
            if cmd != KL["EXEC_CONT"]:
                raise CelerError("frame inesperado durante EXEC")
            out += more[1:]
        return exit_code, out.decode("utf-8", "replace")

    # ------------------------------------------------------------ janela de chunk

    def _pump_chunks(self, f, total, chunk_cmd, timeout, label):
        """Envia o conteudo de `f` em chunks com janela deslizante (proto 2).

        ACK do device: [u16 proximo seq esperado][u32 total aplicado] — o
        total e a verdade: chunks confirmados saem da janela, os que faltam
        sao reenviados. Em proto 1 cai para stop-and-wait (janela 1, payload
        sem seq), byte a byte como antes. Retorna (total_aplicado, crc32).
        """
        chunk = self.max_chunk - (2 if self.proto == 2 else 0)  # seq u16 desconta
        win = self.win if self.proto == 2 else 1
        crc = 0
        pendings = {}   # seq -> (start, tamanho)
        next_seq = 0
        applied = 0
        last_ack = time.monotonic()
        tries = 0

        def frame_for(seq, start):
            f.seek(start)
            data = f.read(min(chunk, total - start))
            payload = (struct.pack("<H", seq) + data) if self.proto == 2 else data
            return self._frame(chunk_cmd, payload)

        while applied < total or pendings:
            # enche a janela (crc calculado uma vez por faixa unica do arquivo)
            while len(pendings) < win and next_seq * chunk < total:
                start = next_seq * chunk
                f.seek(start)
                crc = zlib.crc32(f.read(min(chunk, total - start)), crc) & 0xFFFFFFFF
                self.ser.write(frame_for(next_seq, start))
                pendings[next_seq] = (start, min(chunk, total - start))
                next_seq += 1
            if not pendings:
                break

            # espera ACK (logs que intercalam vao para a fila)
            try:
                cmd_r, payload_r = self._read_frame(timeout=timeout)
            except CelerError:
                cmd_r = None
            if cmd_r == chunk_cmd and payload_r and payload_r[0] == 1:
                raise CelerError(payload_r[1:].decode("utf-8", "replace") or "erro no chunk")
            if cmd_r == chunk_cmd and payload_r and payload_r[0] == 0 and \
                    self.proto == 2 and len(payload_r) >= 7:
                ack_seq, ack_total = struct.unpack("<HI", payload_r[1:7])
                if ack_total > applied:
                    applied = ack_total
                    tries = 0
                for seq in [s for s, (st, n) in pendings.items() if st + n <= applied]:
                    del pendings[seq]
                last_ack = time.monotonic()
            elif cmd_r == chunk_cmd and payload_r and payload_r[0] == 0:
                # proto 1: ACK vazio confirma o chunk mais antigo em voo
                seq = min(pendings)
                start, n = pendings.pop(seq)
                applied = start + n
                last_ack = time.monotonic()
            elif cmd_r in (KL["LOG_DATA"], KL["SCR_DATA"]):
                self.push_queue.append((cmd_r, payload_r))
                last_ack = time.monotonic()
            elif cmd_r is not None:
                raise CelerError(f"frame inesperado durante {label}: "
                                 f"cmd=0x{cmd_r:02x} payload={payload_r[:24].hex() if payload_r else '-'}")
            elif time.monotonic() - last_ack > 1.5:
                # sem ACK: reenvia a janela nao confirmada (max 3 rodadas)
                tries += 1
                if tries > 3:
                    raise CelerError(f"sem ACK do device em {label} (reenvios esgotados)")
                for seq, (start, n) in list(pendings.items()):
                    self.ser.write(frame_for(seq, start))
                last_ack = time.monotonic()
            show_progress(label, applied, total)
        return applied, crc

    def write_file(self, local_path, remote_path, progress=True):
        total = os.path.getsize(local_path)
        self.xfer(KL["WRITE_BEGIN"], remote_path.encode() + b"\0")
        with open(local_path, "rb") as f:
            try:
                applied, crc = self._pump_chunks(f, total, KL["WRITE_CHUNK"], 15.0,
                                                 f"push {os.path.basename(remote_path)}")
                end_payload = struct.pack("<II", crc, total) if self.proto == 2 else b""
                _, payload = self.xfer(KL["WRITE_END"], end_payload, timeout=10.0)
            except (CelerError, serial.SerialException):
                # aborta a escrita: o device remove o arquivo parcial
                try:
                    self.xfer(KL["WRITE_ABORT"], b"", timeout=2.0, retries=0)
                except (CelerError, serial.SerialException):
                    pass
                raise
        if progress:
            print()
        body = payload[1:]
        if self.proto == 2:
            if len(body) >= 8:
                written, dev_crc = struct.unpack("<II", body[:8])
                if written != total or dev_crc != crc:
                    raise CelerError(f"crc/tamanho divergem: enviado {total}/{crc:08x}, "
                                     f"gravado {written}/{dev_crc:08x}")
        else:
            (written,) = struct.unpack("<I", body[:4])
            if written != total:
                raise CelerError(f"escrito {written} de {total} bytes")

    def read_file(self, remote_path, local_path, progress=True):
        st = self.stat(remote_path)
        if st is None or st["dir"]:
            raise CelerError(f"{remote_path} nao existe ou e diretorio")
        total = st["size"]
        # proto 2: resposta = status + offset u32 + dados -> sobra 5 bytes
        want = min(self.max_chunk - 1 - (4 if self.proto == 2 else 0), 0xFFFF)
        win = self.win if self.proto == 2 else 1
        label = f"pull {os.path.basename(remote_path)}"
        # offsets alvo conhecidos de antemao: a janela pede varios e casa
        # cada resposta pelo offset — fora de ordem nao confunde
        targets = [(off, min(want, total - off)) for off in range(0, total, want)] or [(0, 0)]
        done = set()
        pend = {}   # offset -> n pedido
        idx = 0
        last_frame = time.monotonic()
        tries = 0
        with open(local_path, "wb") as f:
            while len(done) < len(targets) or pend:
                while len(pend) < win and idx < len(targets):
                    off, n = targets[idx]
                    if off not in done:
                        req = remote_path.encode() + b"\0" + struct.pack("<II", off, n)
                        self.ser.write(self._frame(KL["READ"], req))
                        pend[off] = n
                    idx += 1
                if not pend:
                    break
                try:
                    cmd, payload = self._read_frame(timeout=10.0)
                except CelerError:
                    cmd = None
                if cmd == KL["READ"] and payload and payload[0] == 0:
                    body = payload[1:]
                    if self.proto == 2:
                        (off,) = struct.unpack("<I", body[:4])
                        body = body[4:]
                        if off not in pend:
                            raise CelerError(f"READ voltou offset {off} fora da janela")
                    else:
                        off = min(pend)  # stop-and-wait: o mais antigo
                    del pend[off]
                    f.seek(off)
                    f.write(body)
                    done.add(off)
                    tries = 0
                    last_frame = time.monotonic()
                elif cmd in (KL["LOG_DATA"], KL["SCR_DATA"]):
                    self.push_queue.append((cmd, payload))
                    last_frame = time.monotonic()
                elif cmd is not None:
                    raise CelerError("frame inesperado durante pull")
                elif time.monotonic() - last_frame > 2.0:
                    # timeout: repede tudo que falta (max 3 rodadas)
                    tries += 1
                    if tries > 3:
                        raise CelerError("pull travado (repedidos esgotados)")
                    pend = {}
                    idx = 0
                    last_frame = time.monotonic()
                if progress:
                    show_progress(label, sum(n for o, n in targets if o in done), total)
        if progress:
            print()
        got = sum(n for o, n in targets if o in done)
        if got != total:
            raise CelerError(f"pull incompleto: {got} de {total} bytes")

    def simple(self, op, payload=b""):
        self.xfer(KL[op], payload)

    def reboot(self):
        self.xfer(KL["REBOOT"])

    def set_baud(self, baud):
        """Negocia a troca de baud e reabre a porta no novo valor.

        Canais sem baud (CDC nativa do S3) respondem erro: vira aviso, a
        sessao segue na velocidade do USB.
        """
        try:
            self.xfer(KL["SET_BAUD"], struct.pack("<I", baud), retries=0)
        except CelerError as e:
            if "nao suportada" in str(e):
                print("aviso: canal sem baud (USB nativo); seguindo na velocidade do USB",
                      file=sys.stderr)
                return
            raise
        port, timeout = self.ser.port, self.ser.timeout
        self.ser.close()
        time.sleep(0.15)  # firmware troca o baud apos o ACK
        self.ser = serial.Serial(port, baud, timeout=timeout, write_timeout=timeout)
        self.hello()  # re-sincroniza a sessao no novo baud

    # ------------------------------------------------------------- logcat/ota

    def logcat_on(self):
        self.xfer(KL["LOG_ON"])

    def logcat_off(self):
        self.xfer(KL["LOG_OFF"])

    def read_push_frame(self, timeout=60.0):
        """Le um frame nao-solicitado (log/screenshot), consumindo a fila."""
        if self.push_queue:
            return self.push_queue.pop(0)
        return self._read_frame(timeout=timeout)

    def ota_write(self, local_path, progress=True):
        _, payload = self.xfer(KL["OTA_BEGIN"])
        part = payload[1:].decode("utf-8", "replace")
        total = os.path.getsize(local_path)
        try:
            with open(local_path, "rb") as f:
                applied, crc = self._pump_chunks(f, total, KL["OTA_CHUNK"], 30.0,
                                                 f"ota {os.path.basename(local_path)} -> {part}")
                end_payload = struct.pack("<II", crc, total) if self.proto == 2 else b""
                _, payload = self.xfer(KL["OTA_END"], end_payload, timeout=30.0)
        except (CelerError, serial.SerialException):
            # cancela a escrita da particao: um begin futuro tambem aborta,
            # mas o estado nao fica pendurado ate lah
            try:
                self.xfer(KL["OTA_ABORT"], b"", timeout=2.0, retries=0)
            except (CelerError, serial.SerialException):
                pass
            raise
        if progress:
            print()
        (written,) = struct.unpack("<I", payload[1:5])
        return part, written

    def screenshot(self):
        # pede RLE (firmware antigo ignora o byte e manda cru, sem o 5o byte
        # de formato no cabecalho)
        _, payload = self.xfer(KL["SCREENSHOT"], bytes([1]), timeout=30.0)
        w, h = struct.unpack("<HH", payload[1:5])
        rle = len(payload) >= 6 and payload[5] == 1
        need = w * h * 2
        data = bytearray()
        pending = bytearray()
        while len(data) < need:
            cmd, more = self._read_frame(timeout=30.0)
            if cmd == KL["LOG_DATA"]:  # log do logcat no meio: guarda e segue
                self.push_queue.append((cmd, more))
                continue
            if cmd != KL["SCR_DATA"]:
                raise CelerError("frame inesperado durante screenshot")
            if not rle:
                data += more[1:]
                continue
            pending += more[1:]
            n = len(pending) // 4 * 4
            for i in range(0, n, 4):
                count, px = struct.unpack_from("<HH", pending, i)
                data += struct.pack("<H", px) * count
            del pending[:n]
        return w, h, bytes(data[:need])


    def coredump(self, keep=False):
        """Baixa o coredump ELF da particao dedicada (b'' se vazio)."""
        _, payload = self.xfer(KL["COREDUMP"], b"\x01" if keep else b"", timeout=30.0)
        if len(payload) < 5:  # [0]=status, [1:5]=u32 tamanho
            raise CelerError("resposta de coredump sem cabecalho")
        (size,) = struct.unpack("<I", payload[1:5])
        if size == 0:
            return b""
        data = bytearray(payload[5:])
        while len(data) < size:
            cmd, more = self._read_frame(timeout=30.0)
            if cmd == KL["LOG_DATA"]:  # log do logcat no meio: guarda e segue
                self.push_queue.append((cmd, more))
                continue
            if cmd != KL["COREDUMP_DATA"]:
                raise CelerError("frame inesperado durante coredump")
            data += more[1:]
        return bytes(data[:size])

    def touch(self, samples):
        """Enfileira amostras de touch sinteticas: [(down, x, y, delay_ms)]."""
        if not 1 <= len(samples) <= 16:
            raise CelerError("gesto deve ter 1..16 amostras")
        payload = bytes([len(samples)])
        for down, x, y, delay in samples:
            payload += bytes([1 if down else 0]) + struct.pack("<HHH", x, y, delay)
        self.xfer(KL["TOUCH"], payload)


# ------------------------------------------------------------------ utilidades

def show_progress(label, current, total):
    width = 24
    if total > 0:
        frac = min(current / total, 1.0)
        bar = "#" * int(width * frac)
        sys.stdout.write(f"\r{label}: [{bar:{width}}] {current}/{total} bytes ")
    else:
        sys.stdout.write(f"\r{label}: {current} bytes ")
    sys.stdout.flush()


def human_size(n):
    for unit in ("B", "K", "M", "G"):
        if n < 1024 or unit == "G":
            return f"{n:.0f}{unit}" if unit == "B" else f"{n:.1f}{unit}"
        n /= 1024


def probe(port, fast=False):
    """Abre a porta e verifica se e o canal HostLink (CDC1/UART).

    Tenta tambem 921600: uma sessao anterior morta ha menos de 8s deixa a
    UART do device em baud alto (o restore so acontece no idle timeout).
    """
    t = 0.6 if fast else 1.5
    for baud in (DEFAULT_BAUD, 921600):
        try:
            link = HostLink(port.device, timeout=t, baud=baud)
            try:
                return link.hello()
            finally:
                link.close()
        except (CelerError, serial.SerialException, OSError):
            continue
    return None


def find_devices(verbose=False):
    """Varre portas USB candidatas e identifica o canal celerctl por HELLO."""
    found = []
    for port in list_ports.comports():
        if port.vid not in USB_VIDS:
            continue
        ident = probe(port)
        if ident:
            found.append((port, ident))
            if verbose:
                desc = port.description or "-"
                serial = port.serial_number or "-"
                print(f"{port.device:<14} {serial:<14} {desc}")
                print(f'{"":14} {ident}')
    return found


def open_link(args):
    def try_open(port):
        """Abre e faz hello; cai para o baud alto se a sessao anterior
        (<8s) ainda estiver viva no device."""
        link = HostLink(port, timeout=3.0)
        try:
            link.hello()
            return link
        except (CelerError, serial.SerialException):
            link.close()
        fallback = getattr(args, "baud", None) or 921600
        link = HostLink(port, timeout=3.0, baud=fallback)
        try:
            link.hello()
            return link
        except (CelerError, serial.SerialException):
            link.close()
            return None

    if args.port:
        port = resolve_port(args.port)
        link = try_open(port)
        if link is None:
            die(f"{args.port} nao responde ao protocolo HostLink")
    else:
        devices = find_devices()
        if not devices:
            die("nenhum CelerOS encontrado (usar -p PORTA para especificar)")
        link = try_open(devices[0][0].device)
        if link is None:
            die(f"{devices[0][0].device} nao responde ao protocolo HostLink")
    if getattr(args, "baud", None) and args.baud != DEFAULT_BAUD and link.ser.baudrate == DEFAULT_BAUD:
        link.set_baud(args.baud)
    return link


def die(msg, code=1):
    print(f"erro: {msg}", file=sys.stderr)
    sys.exit(code)


# ------------------------------------------------------------------- comandos

def resolve_port(selector):
    """-p aceita caminho de porta OU prefixo do serial number USB (a MAC
    "K..." que o firmware S3 define) — varias placas na mesma maquina."""
    if selector.startswith("/") or ":" in selector:
        return selector  # caminho (ou COM3: estilo windows)
    for port in list_ports.comports():
        sn = port.serial_number or ""
        if sn.upper().startswith(selector.upper()):
            return port.device
    die(f"nenhuma placa com serial comecando por {selector!r} "
        f"(celerctl devices lista os seriais)")


def cmd_devices(args):
    found = find_devices(verbose=True)
    if not found and not args.long:
        print("nenhum dispositivo encontrado")
    elif found and not args.long:
        for port, ident in found:
            serial = port.serial_number or "-"
            print(f"{port.device}  {serial:<14} {ident}")


def cmd_info(args):
    link = open_link(args)
    try:
        info = link.info()
    finally:
        link.close()
    print(json.dumps(info, indent=2, ensure_ascii=False))


def cmd_shell(args):
    link = open_link(args)
    try:
        if args.cmd:
            code, out = link.exec(" ".join(args.cmd))
            sys.stdout.write(out)
            sys.exit(code)
        # modo interativo
        print('CelerOS shell (Ctrl-D para sair)')
        while True:
            try:
                line = input("celer> ").strip()
            except EOFError:
                print()
                return
            if not line:
                continue
            if line in ("exit", "quit"):
                return
            code, out = link.exec(line)
            sys.stdout.write(out if out.endswith("\n") or not out else out + "\n")
    finally:
        link.close()


def cmd_ls(args):
    link = open_link(args)
    try:
        entries = link.ls(args.path)
    finally:
        link.close()
    if args.long:
        import datetime
        for e in sorted(entries, key=lambda e: (not e["dir"], e["name"].lower())):
            when = datetime.datetime.fromtimestamp(e["mtime"]).strftime("%Y-%m-%d %H:%M")
            print(f"{'d' if e['dir'] else '-'} {human_size(e['size']):>7}  {when}  {e['name']}")
    else:
        for e in sorted(entries, key=lambda e: (not e["dir"], e["name"].lower())):
            print(("/" if e["dir"] else "") + e["name"])


def cmd_cat(args):
    link = open_link(args)
    try:
        st = link.stat(args.path)
        if st is None or st["dir"]:
            die(f"{args.path} nao existe ou e diretorio")
        got = 0
        while got < st["size"]:
            data = link.read_chunk(args.path, got, min(CHUNK, st["size"] - got))
            if not data:
                break
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
            got += len(data)
    finally:
        link.close()


def cmd_push(args):
    if not os.path.isfile(args.local):
        die(f"{args.local} nao existe")
    link = open_link(args)
    try:
        link.write_file(args.local, args.remote)
    finally:
        link.close()


def cmd_pull(args):
    link = open_link(args)
    try:
        link.read_file(args.remote, args.local)
    finally:
        link.close()


def cmd_rm(args):
    link = open_link(args)
    try:
        link.delete(args.path, recursive=args.recursive)
    finally:
        link.close()


def cmd_simple(op):
    def handler(args):
        payload = b""
        if op == "RENAME":
            payload = args.src.encode() + b"\0" + args.dst.encode() + b"\0"
        elif op in ("DELETE", "MKDIR"):
            payload = args.path.encode() + b"\0"
        link = open_link(args)
        try:
            link.simple(op, payload)
        finally:
            link.close()
    return handler


def cmd_reboot(args):
    link = open_link(args)
    try:
        link.reboot()
    finally:
        link.close()
    print("reiniciando...")


def cmd_logcat(args):
    link = open_link(args)
    try:
        link.logcat_on()
        if args.dump:
            # absorve o ring acumulado: imprime as linhas que chegarem e
            # encerra apos uma janela sem linha nova (o dispositivo para de
            # mandar quando o buffer acaba)
            quiet = args.quiet_ms / 1000.0
            last = time.monotonic()
            while time.monotonic() - last < quiet:
                try:
                    cmd, payload = link.read_push_frame(timeout=0.15)
                except Exception:
                    continue
                if cmd == KL["LOG_DATA"]:
                    sys.stdout.write(payload[1:].decode("utf-8", "replace"))
                    sys.stdout.flush()
                    last = time.monotonic()
            return
        print("aguardando logs do dispositivo (Ctrl-C para sair; --dump absorve o buffer e sai)", file=sys.stderr)
        while True:
            cmd, payload = link.read_push_frame(timeout=3600.0)
            if cmd == KL["LOG_DATA"]:
                sys.stdout.write(payload[1:].decode("utf-8", "replace"))
                sys.stdout.flush()
    except KeyboardInterrupt:
        print()
    finally:
        try:
            link.logcat_off()
        except Exception:
            pass
        link.close()


def cmd_ota(args):
    if args.ota_cmd != "push":
        die("uso: celerctl ota push FIRMWARE.bin")
    if not os.path.isfile(args.file):
        die(f"{args.file} nao existe")
    link = open_link(args)
    try:
        part, written = link.ota_write(args.file)
        print(f"OTA ok: {written} bytes em '{part}'")
        if not args.no_reboot:
            link.reboot()
            print("reiniciando para o novo firmware...")
    finally:
        link.close()



def cmd_coredump(args):
    link = open_link(args)
    try:
        data = link.coredump(keep=args.keep)
    finally:
        link.close()
    if not data:
        die("nao ha coredump gravado (nenhum crash desde o ultimo reset)")
    Path(args.out).write_bytes(data)
    print(f"{args.out}: {len(data)} bytes (ELF) — analise com:")
    print(f"  idf.py -B build coredump-info -c {args.out}")

def cmd_screencap(args):
    link = open_link(args)
    try:
        w, h, data = link.screenshot()
    finally:
        link.close()
    try:
        from PIL import Image
    except ImportError:
        die("Pillow nao instalado (pip install Pillow)")
    # RGB565 little-endian -> RGB888
    pixels = struct.unpack(f"<{w * h}H", data)
    out = bytearray(w * h * 3)
    for i, p in enumerate(pixels):
        # replica os bits altos nos baixos de cada canal (sem mascarar, o
        # (p >> 9) do verde arrastava os bits do VERMELHO para o G)
        out[i * 3] = ((p >> 8) & 0xF8) | (p >> 13)
        out[i * 3 + 1] = ((p >> 3) & 0xFC) | ((p >> 9) & 0x03)
        out[i * 3 + 2] = ((p << 3) & 0xF8) | ((p >> 2) & 0x07)
    Image.frombytes("RGB", (w, h), bytes(out)).save(args.out)
    print(f"{args.out}: {w}x{h}")


def cmd_tap(args):
    link = open_link(args)
    try:
        link.touch([(True, args.x, args.y, 0), (False, 0, 0, args.hold)])
    finally:
        link.close()
    # deixa o gesto completar no dispositivo antes de um screencap seguinte
    time.sleep((args.hold + 150) / 1000.0)


def cmd_swipe(args):
    steps = 8
    step_ms = max(1, args.duration // (steps + 1))
    samples = [(True, args.x0, args.y0, 0)]
    for i in range(1, steps + 1):
        t = i / (steps + 1)
        x = round(args.x0 + (args.x1 - args.x0) * t)
        y = round(args.y0 + (args.y1 - args.y0) * t)
        samples.append((True, x, y, step_ms))
    samples.append((False, 0, 0, 20))
    link = open_link(args)
    try:
        link.touch(samples)
    finally:
        link.close()
    time.sleep((args.duration + 200) / 1000.0)


# ------------------------------------------------------------------- apps ---

def _app_meta(link, base, dirname):
    """Le <base>/<dirname>/app.json no dispositivo; None se invalido."""
    import json as _json
    path = f"{base}/{dirname}/app.json"
    try:
        st = link.stat(path)
    except CelerError:
        return None
    if st is None or st["dir"] or st["size"] > 16384:
        return None
    raw = b""
    while len(raw) < st["size"]:
        chunk = link.read_chunk(path, len(raw), min(CHUNK, st["size"] - len(raw)))
        if not chunk:
            return None
        raw += chunk
    try:
        meta = _json.loads(raw.decode("utf-8", "replace"))
    except ValueError:
        return None
    meta.setdefault("name", dirname)
    meta.setdefault("version", "?")
    meta["_base"] = base
    meta["_dir"] = dirname
    return meta


def _rm_tree(link, path):
    if link.proto == 2:
        link.delete(path, recursive=True)  # um comando no device
        return
    for e in link.ls(path):
        rpath = f"{path}/{e['name']}"
        if e["dir"]:
            _rm_tree(link, rpath)
        else:
            link.simple("DELETE", rpath.encode() + b"\0")
    link.simple("DELETE", path.encode() + b"\0")


def _lint_app_folder(folder, fatal=True):
    """Lint estatico (tools/app_lint) antes de empurrar o app ao dispositivo:
    parse ES5 + checagem contra a API do firmware. Sem Node no PATH so avisa e
    segue (celerctl roda em maquinas variadas); erros abortam o install (ou
    apenas avisam com fatal=False, usado pelo loop `dev`). Retorna True se o
    app esta apto a instalar."""
    import subprocess
    lint = Path(__file__).resolve().parent / "app_lint" / "lint.js"
    if not lint.is_file():
        print("aviso: tools/app_lint/lint.js ausente; instalando sem lint")
        return True
    try:
        out = subprocess.run(["node", str(lint), "--json", str(folder)],
                             capture_output=True, text=True, timeout=120)
    except FileNotFoundError:
        print("aviso: node ausente no PATH; instalando sem lint")
        return True
    except subprocess.TimeoutExpired:
        if fatal:
            die("lint do app demorou demais (120s)")
        print("aviso: lint do app demorou demais; instalando sem lint")
        return True
    if out.returncode == 2:
        msg = f"lint falhou: {(out.stderr or out.stdout).strip()}"
        if fatal:
            die(msg)
        print(f"aviso: {msg}")
        return False
    try:
        import json as _json
        r = _json.loads(out.stdout)
    except ValueError:
        print("aviso: saida inesperada do lint; instalando sem lint")
        return True
    diags = [d for a in r.get("apps", []) for d in a.get("diagnostics", [])]
    for d in diags:
        print(f"{d.get('severity')}: {d['file']}:{d['line']}:{d['col']} {d['message']}")
    erros = sum(1 for d in diags if d.get("severity") == "erro")
    if erros and fatal:
        die(f"{erros} erro(s) no app — corrija ou force com --pula-lint")
    return erros == 0


def _push_app_files(link, src, dest, only=None, progress=True):
    """Empurra os arquivos da pasta de app para <dest> no dispositivo.
    `only` limita aos caminhos relativos dados (reload do `dev`); None = tudo
    (install completo, cria a arvore de diretorios)."""
    if only is None:
        link.simple("MKDIR", dest.encode() + b"\0")
        files = [f for f in sorted(src.rglob("*")) if f.is_file() and ".dev" not in f.parts]
        for f in files:
            rel = f.relative_to(src).parent
            if str(rel) != ".":
                d = dest + "/" + rel.as_posix()
                link.simple("MKDIR", d.encode() + b"\0")
    else:
        files = [src / rel for rel in only if (src / rel).is_file()]
    for f in files:
        link.write_file(str(f), f"{dest}/{f.relative_to(src).as_posix()}", progress=progress)
    return [f.relative_to(src).as_posix() for f in files]


def _relaunch(link, app_name):
    """Encerra o app em execucao (shell `exit`) e abre de novo (`run`).
    Firmware sem o comando `exit` apenas avisa — o run entao espera o app
    atual sair sozinho."""
    try:
        code, out = link.exec("exit")
        if code != 0:
            print(f"aviso: 'exit' recusado ({out.strip()}) — firmware antigo? "
                  "o run abre quando o app atual sair")
    except CelerError as e:
        print(f"aviso: exit falhou ({e}); tentando run mesmo assim")
    link.exec(f"run {app_name}")


def _snapshot(src):
    """Mapa caminho_relativo -> (mtime_ns, size) dos arquivos da pasta de app
    (a subpasta .dev/, onde o dev guarda screenshots, fica de fora)."""
    out = {}
    for f in src.rglob("*"):
        if f.is_file() and ".dev" not in f.parts:
            st = f.stat()
            out[f.relative_to(src).as_posix()] = (st.st_mtime_ns, st.st_size)
    return out


def _pull_tree(link, remote_dir, local_dir):
    count = 0
    for e in sorted(link.ls(remote_dir), key=lambda e: e["name"]):
        rpath = f"{remote_dir}/{e['name']}"
        lpath = Path(local_dir) / e["name"]
        if e["dir"]:
            lpath.mkdir(parents=True, exist_ok=True)
            count += _pull_tree(link, rpath, lpath)
        else:
            lpath.parent.mkdir(parents=True, exist_ok=True)
            link.read_file(rpath, str(lpath), progress=False)
            count += 1
    return count


def cmd_dev(args):
    """Loop de desenvolvimento: instala a pasta de app, abre o app no
    dispositivo e assiste aos arquivos — a cada mudanca roda o lint, empurra
    so o que mudou, encerra o app em execucao (shell `exit`) e abre de novo,
    com os logs do dispositivo ao vivo. Ctrl-C encerra (o app segue aberto).

    Detalhes do protocolo que sustentam este loop:
      - o canal HostLink cai para CONSOLE apos 8s sem bytes DO HOST, entao o
        loop manda um HELLO de keepalive a cada 5s;
      - logs chegam como frames LOG_DATA nao-solicitados entre comandos.
    """
    src = Path(args.folder).resolve()
    if not (src / "app.json").is_file():
        die(f"{src} nao tem app.json")
    if args.baud == DEFAULT_BAUD:
        args.baud = 921600  # push/pull no dev ganham muito; -b explicito vence

    base = "/sd/apps" if args.sd else "/local/apps"
    dest = f"{base}/{src.name}"
    shots_dir = src / ".dev"

    link = open_link(args)
    state = {}
    last_keepalive = time.monotonic()
    print(f"dev: {src} -> {dest} (Ctrl-C para sair)")
    try:
        link.logcat_on()
        first = True
        while True:
            snap = _snapshot(src)
            if first:
                first = False
                changed = sorted(snap)  # primeira passada: instala tudo
            else:
                changed = [rp for rp, sig in snap.items() if state.get(rp) != sig]
            removed = [rp for rp in state if rp not in snap]
            if changed or removed:
                stamp = time.strftime("%H:%M:%S")
                print(f"\n== [{stamp}] mudou: {', '.join(changed + removed)}")
                for rp in removed:
                    link.simple("DELETE", f"{dest}/{rp}".encode() + b"\0")
                    state.pop(rp, None)
                ok = args.no_lint or _lint_app_folder(src, fatal=False)
                if ok:
                    sent = _push_app_files(link, src, dest, only=changed or None,
                                           progress=False)
                    for rp, sig in snap.items():
                        if rp in sent:
                            state[rp] = sig
                    if any(rp == "app.json" or rp.endswith(".bin") or rp.endswith(".png")
                           for rp in changed):
                        link.exec("rescan")
                    _relaunch(link, src.name)
                    if args.shots:
                        time.sleep(2.5)  # launcher rescaneia (run pede rescan) e abre o app
                        _dev_screenshot(link, shots_dir)
                else:
                    print("== lint com erros; corriga e salve para tentar de novo")
                    # assume estado atual mesmo assim (nao reenvia ate mudar)
                    for rp in changed:
                        state[rp] = snap.get(rp)
                print("== aguardando mudancas...", flush=True)

            # drena logs acumulados durante os comandos acima
            while link.push_queue:
                cmd, payload = link.push_queue.pop(0)
                if cmd == KL["LOG_DATA"]:
                    sys.stdout.write(payload[1:].decode("utf-8", "replace"))
            sys.stdout.flush()

            # logs ao vivo + keepalive do canal (idle de 8s no firmware)
            try:
                cmd, payload = link._read_frame(timeout=0.3)
                if cmd == KL["LOG_DATA"]:
                    sys.stdout.write(payload[1:].decode("utf-8", "replace"))
                    sys.stdout.flush()
            except CelerError:
                pass  # sem frame neste tick
            if time.monotonic() - last_keepalive > 5.0:
                last_keepalive = time.monotonic()
                try:
                    link.keepalive()  # HELLO no formato da sessao (v2 cai para v1 se vier)
                except (CelerError, serial.SerialException):
                    print("\ndev: conexao caiu (placa reiniciou?); tentando reconectar...")
                    link = _reconnect(link)
                    link.logcat_on()
                    _relaunch(link, src.name)
    except KeyboardInterrupt:
        print("\ndev: encerrando (app segue aberto no dispositivo)")
    finally:
        try:
            link.logcat_off()
        except Exception:
            pass
        link.close()


def _reconnect(old):
    """Reabre a porta apos a conexao cair (placa reiniciou)."""
    port = old.ser.port
    baud = old.ser.baudrate
    old.close()
    for _ in range(20):  # ~20s: boot + montagens
        time.sleep(1.0)
        try:
            link = HostLink(port, timeout=3.0)
            link.hello()
            if baud != DEFAULT_BAUD:
                link.set_baud(baud)
            print("dev: reconectado")
            return link
        except (CelerError, serial.SerialException, OSError):
            continue
    die("dev: nao conseguiu reconectar")


def _dev_screenshot(link, shots_dir):
    shots_dir.mkdir(exist_ok=True)
    out = shots_dir / "last.png"
    try:
        w, h, data = link.screenshot()
        from PIL import Image
        pixels = struct.unpack(f"<{w * h}H", data)
        rgb = bytearray(w * h * 3)
        for i, p in enumerate(pixels):
            rgb[i * 3] = ((p >> 8) & 0xF8) | (p >> 13)
            rgb[i * 3 + 1] = ((p >> 3) & 0xFC) | ((p >> 9) & 0x03)
            rgb[i * 3 + 2] = (p << 3) & 0xF8 | ((p >> 2) & 0x07)
        Image.frombytes("RGB", (w, h), bytes(rgb)).save(out)
        print(f"== tela: {out} ({w}x{h})")
    except ImportError:
        print("== tela: pip install Pillow para --shots")
    except CelerError as e:
        print(f"== tela: falhou ({e})")


def cmd_apps(args):
    link = open_link(args)
    try:
        if args.action == "list":
            found = []
            for base in ("/local/apps", "/sd/apps"):
                try:
                    entries = link.ls(base)
                except CelerError:
                    continue
                for e in entries:
                    if not e["dir"]:
                        continue
                    meta = _app_meta(link, base, e["name"]) or {"name": e["name"], "_base": base}
                    found.append(meta)
            if not found:
                print("nenhum app instalado")
                return
            found.sort(key=lambda m: (not m.get("system"), m.get("name", "").lower()))
            tag_width = 8
            for m in found:
                where = "sd" if m["_base"].startswith("/sd") else "local"
                tag = "sistema" if m.get("system") else where
                # app.json sem packageName (meta do FS nao ganha _dir): mostar
                # o nome em vez de quebrar a listagem inteira
                pkg = m.get("packageName") or m.get("_dir") or m.get("name", "?")
                print(f"{tag:<{tag_width}} {m.get('name', '?'):<16} v{m.get('version', '?'):<10} api {m.get('api', '?'):<3} {pkg}")
        elif args.action == "install":
            src = Path(args.folder).resolve()
            if not (src / "app.json").is_file():
                die(f"{src} nao tem app.json")
            if not args.pula_lint:
                _lint_app_folder(src)
            base = "/sd/apps" if args.sd else "/local/apps"
            dest = f"{base}/{src.name}"
            files = [f for f in sorted(src.rglob("*"))
                     if f.is_file() and ".dev" not in f.parts]
            link.simple("MKDIR", dest.encode() + b"\0")
            for f in files:
                rel = f.relative_to(src).parent
                if str(rel) != ".":
                    d = dest + "/" + rel.as_posix()
                    link.simple("MKDIR", d.encode() + b"\0")
            for f in files:
                link.write_file(str(f), f"{dest}/{f.relative_to(src).as_posix()}")
            link.exec("rescan")
            print(f"instalado: {dest} ({len(files)} arquivos)")
            if args.run:
                _relaunch(link, src.name)
                print(f"abrindo: {src.name}")
        elif args.action == "pull":
            # procura o app em /local/apps e depois /sd/apps
            target = None
            for candidate_base in ("/local/apps", "/sd/apps"):
                st = link.stat(f"{candidate_base}/{args.name}")
                if st is not None and st["dir"]:
                    target = f"{candidate_base}/{args.name}"
                    break
            if target is None:
                die(f"app '{args.name}' nao encontrado em /local/apps nem /sd/apps")
            out_dir = Path(args.dest) if args.dest else Path(args.name)
            if out_dir.exists() and any(out_dir.iterdir()):
                die(f"{out_dir} ja existe e nao esta vazia")
            out_dir.mkdir(parents=True, exist_ok=True)
            n = _pull_tree(link, target, out_dir)
            print(f"baixado: {target} -> {out_dir} ({n} arquivos)")
        elif args.action == "rm":
            base = "/sd/apps" if args.sd else "/local/apps"
            target = f"{base}/{args.name}"
            st = link.stat(target)
            if st is None or not st["dir"]:
                die(f"{target} nao existe")
            meta = _app_meta(link, base, args.name)
            if meta and meta.get("system") and not args.force:
                die(f"'{args.name}' e app do sistema; use --force para remover")
            _rm_tree(link, target)
            link.exec("rescan")
            print(f"removido: {target}")
    finally:
        link.close()


# ---------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(prog="celerctl", description="ferramenta USB do CelerOS (estilo adb)")
    parser.add_argument("-p", "--port",
                        help="porta serial do canal celerctl (ex: /dev/ttyUSB0) ou "
                             "prefixo do serial USB da placa (celerctl devices lista)")
    parser.add_argument("-b", "--baud", type=int, default=DEFAULT_BAUD,
                        help="negocia este baud com o firmware (ex: 921600 acelera push/pull)")
    parser.add_argument("--proto", type=int, choices=(1, 2), default=2,
                        help="forca o formato do protocolo (default 2 = CRC32 + janela;"
                             " 1 valida o caminho legado)")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("devices", help="lista placas conectadas")
    p.add_argument("-l", "--long", action="store_true")
    p.set_defaults(func=cmd_devices)

    p = sub.add_parser("info", help="informacoes do sistema")
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("shell", help="shell interativo ou executa comando")
    p.add_argument("cmd", nargs="*", help="comando a executar")
    p.set_defaults(func=cmd_shell)

    p = sub.add_parser("ls", help="lista diretorio")
    p.add_argument("-l", "--long", action="store_true")
    p.add_argument("path", nargs="?", default="/local")
    p.set_defaults(func=cmd_ls)

    p = sub.add_parser("cat", help="mostra conteudo de arquivo")
    p.add_argument("path")
    p.set_defaults(func=cmd_cat)

    p = sub.add_parser("push", help="envia arquivo ao dispositivo")
    p.add_argument("local")
    p.add_argument("remote")
    p.set_defaults(func=cmd_push)

    p = sub.add_parser("pull", help="baixa arquivo do dispositivo")
    p.add_argument("remote")
    p.add_argument("local", nargs="?", default=None)
    p.set_defaults(func=cmd_pull)

    p = sub.add_parser("rm", help="apaga arquivo (ou pasta com -r)")
    p.add_argument("path")
    p.add_argument("-r", "--recursive", action="store_true",
                   help="apaga diretorio com todo o conteudo")
    p.set_defaults(func=cmd_rm)

    p = sub.add_parser("mkdir", help="cria diretorio")
    p.add_argument("path")
    p.set_defaults(func=cmd_simple("MKDIR"))

    p = sub.add_parser("mv", help="renomeia/move")
    p.add_argument("src")
    p.add_argument("dst")
    p.set_defaults(func=cmd_simple("RENAME"))

    p = sub.add_parser("reboot", help="reinicia a placa")
    p.set_defaults(func=cmd_reboot)

    p = sub.add_parser("logcat", help="stream de logs do dispositivo")
    p.add_argument("--dump", action="store_true",
                   help="absorve o buffer acumulado no dispositivo e sai")
    p.add_argument("--quiet-ms", type=int, default=800,
                   help="janela de silencio do --dump em ms (padrao 800)")
    p.set_defaults(func=cmd_logcat)

    p = sub.add_parser("ota", help="grava firmware pela conexao (sem esptool)")
    ota_sub = p.add_subparsers(dest="ota_cmd", required=True)
    po = ota_sub.add_parser("push", help="envia e grava um firmware.bin")
    po.add_argument("file")
    po.add_argument("--no-reboot", action="store_true", help="nao reinicia apos gravar")
    p.set_defaults(func=cmd_ota)

    p = sub.add_parser("coredump", help="baixa o coredump do ultimo crash (ELF)")
    p.add_argument("--out", default="coredump.elf", help="arquivo de saida")
    p.add_argument("--keep", action="store_true",
                   help="nao apaga o dump apos baixar (reler depois)")
    p.set_defaults(func=cmd_coredump)

    p = sub.add_parser("screencap", help="captura da tela -> PNG")
    p.add_argument("out", nargs="?", default="celer_screencap.png")
    p.set_defaults(func=cmd_screencap)

    p = sub.add_parser("tap", help="injeta um toque na tela")
    p.add_argument("x", type=int)
    p.add_argument("y", type=int)
    p.add_argument("hold", nargs="?", type=int, default=80, help="ms pressionado (default 80)")
    p.set_defaults(func=cmd_tap)

    p = sub.add_parser("swipe", help="injeta um arrasto (p0 -> p1)")
    p.add_argument("x0", type=int)
    p.add_argument("y0", type=int)
    p.add_argument("x1", type=int)
    p.add_argument("y1", type=int)
    p.add_argument("duration", nargs="?", type=int, default=250, help="duracao em ms (default 250)")
    p.set_defaults(func=cmd_swipe)

    p = sub.add_parser("dev", help="loop de desenvolvimento: watch + reload + logs")
    p.add_argument("folder", help="pasta do app (com app.json)")
    p.add_argument("--sd", action="store_true", help="instala no cartao (/sd/apps)")
    p.add_argument("--shots", action="store_true",
                   help="salva screenshot apos cada reload em PASTA/.dev/last.png")
    p.add_argument("--no-lint", action="store_true", help="pula o lint a cada reload")
    p.set_defaults(func=cmd_dev)

    p = sub.add_parser("apps", help="gerencia apps instalados no dispositivo")
    apps_sub = p.add_subparsers(dest="action", required=True)
    a = apps_sub.add_parser("list", help="lista apps de /local/apps e /sd/apps")
    a = apps_sub.add_parser("install", help="instala uma pasta de app local")
    a.add_argument("folder", help="pasta com app.json + main.js (+ icon.bin)")
    a.add_argument("--sd", action="store_true", help="instala no cartao (/sd/apps)")
    a.add_argument("--pula-lint", action="store_true", help="instala mesmo com erros de lint")
    a.add_argument("--run", action="store_true",
                   help="abre o app ao final (exit + run; encerra o anterior)")
    a = apps_sub.add_parser("pull", help="baixa um app instalado para o PC")
    a.add_argument("name", help="nome da pasta do app no dispositivo")
    a.add_argument("dest", nargs="?", default=None, help="pasta de destino (default ./<nome>)")
    a = apps_sub.add_parser("rm", help="remove um app instalado")
    a.add_argument("name", help="nome da pasta do app")
    a.add_argument("--sd", action="store_true", help="remove de /sd/apps")
    a.add_argument("--force", action="store_true", help="permite remover app de sistema")
    p.set_defaults(func=cmd_apps)

    args = parser.parse_args()
    if args.command == "pull" and args.local is None:
        args.local = os.path.basename(args.remote) or "celer_pull.bin"
    global FORCE_PROTO1
    FORCE_PROTO1 = args.proto == 1

    try:
        args.func(args)
    except CelerError as e:
        die(str(e))
    except serial.SerialException as e:
        die(f"porta serial: {e}")
    except KeyboardInterrupt:
        print()
        sys.exit(130)


if __name__ == "__main__":
    main()
