#!/usr/bin/env python3
r"""
kryonctl - ferramenta de depuracao/manutencao do KryonOS via USB (estilo adb).

Conversa com o firmware pelo canal KryonLink: na pratica a UART do CH340
(USB do PC -> /dev/ttyUSB*) ou, em placas com USB nativo, a CDC1. Os opcodes
sao lidos diretamente de main/USBDevice/KryonLink.h para manter os dois
lados em sincronia.

Comandos:
  devices [-l]                lista placas KryonOS conectadas
  info                        versao/board/heap/rede/filesystems
  shell [cmd...]              shell interativo (ou executa um comando)
  ls [-l] CAMINHO             lista diretorio (/local ou /sd)
  cat ARQUIVO                 escreve conteudo no stdout
  rm / mkdir / mv             operacoes de arquivo
  push LOCAL REMOTO           envia arquivo para o dispositivo
  pull REMOTO [LOCAL]         baixa arquivo do dispositivo
  reboot                      reinicia a placa
  logcat                      stream de logs em tempo real (Ctrl-C sai)
  ota push FW.bin [--no-reboot]  grava firmware pela serial (sem esptool)
  screencap [SAIDA.png]       captura da tela do dispositivo

Exemplos:
  python3 tools/kryonctl.py devices
  python3 tools/kryonctl.py shell ls /local
  python3 tools/kryonctl.py -b 921600 push firmware.bin /sd/fw.bin

Dependencias: pyserial (pip install -r tools/requirements.txt)
"""

import argparse
import json
import os
import re
import struct
import sys
import time
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
CHUNK = 4096  # tamanho maximo de payload KryonLink
DEFAULT_BAUD = 115200


class KryonError(Exception):
    pass


def load_opcodes():
    """Extrai os opcodes KL_* do header do firmware (fonte unica)."""
    header = Path(__file__).resolve().parent.parent / "main" / "USBDevice" / "KryonLink.h"
    ops = {}
    for name, value in re.findall(r"KL_(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", header.read_text()):
        ops[name] = int(value, 16)
    missing = {"HELLO", "INFO", "LS", "STAT", "READ", "WRITE_BEGIN", "WRITE_CHUNK",
               "WRITE_END", "DELETE", "MKDIR", "RENAME", "EXEC", "REBOOT", "EXEC_CONT",
               "SET_BAUD"} - set(ops)
    if missing:
        raise KryonError(f"opcodes ausentes em {header}: {missing}")
    return ops


KL = load_opcodes()


def read_exact(ser, n, timeout):
    """Le exatamente n bytes (bloqueando ate timeout)."""
    buf = b""
    deadline = time.monotonic() + timeout
    while len(buf) < n:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise KryonError("timeout lendo do dispositivo")
        ser.timeout = min(remaining, 1.0)
        chunk = ser.read(n - len(buf))
        if chunk:
            buf += chunk
    return buf


class KryonLink:
    """Cliente do protocolo KryonLink sobre a CDC1."""

    def __init__(self, port, timeout=3.0):
        self.ser = serial.Serial(port, 115200, timeout=timeout, write_timeout=timeout)
        self.timeout = timeout
        self.push_queue = []  # frames nao-solicitados (logs) que chegaram no meio de um xfer

    def close(self):
        self.ser.close()

    def _read_frame(self, expect_cmd=None, timeout=None):
        timeout = timeout or self.timeout
        # procura o magic 0x4B um byte por vez (resincroniza se houver lixo)
        while True:
            first = read_exact(self.ser, 1, timeout)
            if first[0] != 0x4B:
                continue
            head = first + read_exact(self.ser, 3, timeout)
            cmd, length = head[1], struct.unpack("<H", head[2:4])[0]
            break
        payload = read_exact(self.ser, length, timeout) if length else b""
        return cmd, payload

    def xfer(self, cmd, payload=b"", timeout=None):
        """Envia um comando e retorna (cmd_resposta, payload_resposta).

        Frames nao-solicitados (logs do logcat) que chegarem no meio do
        caminho sao guardados em push_queue em vez de confundir a resposta.
        """
        frame = bytes([0x4B, cmd]) + struct.pack("<H", len(payload)) + payload
        self.ser.reset_input_buffer()
        self.push_queue = []
        self.ser.write(frame)
        while True:
            cmd_r, payload_r = self._read_frame(timeout=timeout)
            if cmd_r != cmd and cmd_r in (KL["LOG_DATA"], KL["SCR_DATA"]):
                self.push_queue.append((cmd_r, payload_r))
                continue
            break
        if cmd_r == 0x00 or (payload_r and payload_r[0] == 1):
            raise KryonError(payload_r[1:].decode("utf-8", "replace") or "erro no dispositivo")
        return cmd_r, payload_r

    # ---------------------------------------------------------------- comandos

    def hello(self):
        cmd, payload = self.xfer(KL["HELLO"], b"KRYONCTL1", timeout=1.0)
        if cmd != KL["HELLO"]:
            raise KryonError("resposta inesperada ao HELLO")
        return payload[1:].decode("utf-8", "replace")

    def info(self):
        _, payload = self.xfer(KL["INFO"])
        return json.loads(payload[1:].decode())

    def ls(self, path):
        _, payload = self.xfer(KL["LS"], path.encode() + b"\0")
        body = payload[1:]
        (count,) = struct.unpack("<H", body[:2])
        entries = []
        off = 2
        for _ in range(count):
            is_dir = body[off]
            size, mtime = struct.unpack("<II", body[off + 1:off + 9])
            name_len = body[off + 9]
            name = body[off + 10:off + 10 + name_len].decode("utf-8", "replace")
            entries.append({"dir": bool(is_dir), "size": size, "mtime": mtime, "name": name})
            off += 10 + name_len
        return entries

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
        return payload[1:]  # status + dados

    def exec(self, line):
        _, payload = self.xfer(KL["EXEC"], line.encode(), timeout=15.0)
        body = payload[1:]
        exit_code = body[0]
        (out_len,) = struct.unpack("<I", body[1:5])
        out = body[5:]
        # saida grande chega em frames de continuacao
        while len(out) < out_len:
            cmd, more = self._read_frame(expect_cmd=KL["EXEC_CONT"])
            if cmd != KL["EXEC_CONT"]:
                raise KryonError("frame inesperado durante EXEC")
            out += more[1:]
        return exit_code, out.decode("utf-8", "replace")

    def write_file(self, local_path, remote_path, progress=True):
        total = os.path.getsize(local_path)
        self.xfer(KL["WRITE_BEGIN"], remote_path.encode() + b"\0")
        sent = 0
        with open(local_path, "rb") as f:
            while True:
                chunk = f.read(CHUNK)
                if not chunk:
                    break
                self.xfer(KL["WRITE_CHUNK"], chunk, timeout=15.0)
                sent += len(chunk)
                if progress:
                    show_progress(f"push {os.path.basename(remote_path)}", sent, total)
        _, payload = self.xfer(KL["WRITE_END"], timeout=10.0)
        (written,) = struct.unpack("<I", payload[1:5])
        if progress:
            print()
        if written != total:
            raise KryonError(f"escrito {written} de {total} bytes")

    def read_file(self, remote_path, local_path, progress=True):
        st = self.stat(remote_path)
        if st is None or st["dir"]:
            raise KryonError(f"{remote_path} nao existe ou e diretorio")
        total = st["size"]
        got = 0
        with open(local_path, "wb") as f:
            while got < total:
                data = self.read_chunk(remote_path, got, min(CHUNK, total - got))
                if not data:
                    raise KryonError("fim de arquivo inesperado")
                f.write(data)
                got += len(data)
                if progress:
                    show_progress(f"pull {os.path.basename(remote_path)}", got, total)
        if progress:
            print()

    def simple(self, op, payload=b""):
        self.xfer(KL[op], payload)

    def reboot(self):
        self.xfer(KL["REBOOT"])

    def set_baud(self, baud):
        """Negocia a troca de baud e reabre a porta no novo valor."""
        self.xfer(KL["SET_BAUD"], struct.pack("<I", baud))
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
        sent = 0
        with open(local_path, "rb") as f:
            while True:
                chunk = f.read(CHUNK)
                if not chunk:
                    break
                self.xfer(KL["OTA_CHUNK"], chunk, timeout=30.0)
                sent += len(chunk)
                if progress:
                    show_progress(f"ota {os.path.basename(local_path)} -> {part}", sent, total)
        if progress:
            print()
        _, payload = self.xfer(KL["OTA_END"], timeout=30.0)
        (written,) = struct.unpack("<I", payload[1:5])
        return part, written

    def screenshot(self):
        _, payload = self.xfer(KL["SCREENSHOT"], timeout=30.0)
        w, h = struct.unpack("<HH", payload[1:5])
        need = w * h * 2
        data = bytearray()
        while len(data) < need:
            cmd, more = self._read_frame(timeout=30.0)
            if cmd != KL["SCR_DATA"]:
                raise KryonError("frame inesperado durante screenshot")
            data += more[1:]
        return w, h, bytes(data)


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
    """Abre a porta e verifica se e o canal KryonLink (CDC1)."""
    try:
        link = KryonLink(port.device, timeout=0.6 if fast else 1.5)
        try:
            return link.hello()
        finally:
            link.close()
    except (KryonError, serial.SerialException, OSError):
        return None


def find_devices(verbose=False):
    """Varre portas USB candidatas e identifica o canal kryonctl por HELLO."""
    found = []
    for port in list_ports.comports():
        if port.vid not in USB_VIDS:
            continue
        ident = probe(port)
        if ident:
            found.append((port, ident))
            if verbose:
                desc = port.description or "-"
                print(f"{port.device:<14} {desc}")
                print(f'{"":14} {ident}')
    return found


def open_link(args):
    if args.port:
        link = KryonLink(args.port, timeout=3.0)
        try:
            link.hello()
        except KryonError:
            link.close()
            die(f"{args.port} nao responde ao protocolo KryonLink")
    else:
        devices = find_devices()
        if not devices:
            die("nenhum KryonOS encontrado (usar -p PORTA para especificar)")
        link = KryonLink(devices[0][0].device, timeout=3.0)
    if getattr(args, "baud", None) and args.baud != DEFAULT_BAUD:
        link.set_baud(args.baud)
    return link


def die(msg, code=1):
    print(f"erro: {msg}", file=sys.stderr)
    sys.exit(code)


# ------------------------------------------------------------------- comandos

def cmd_devices(args):
    found = find_devices(verbose=True)
    if not found and not args.long:
        print("nenhum dispositivo encontrado")
    elif found and not args.long:
        for port, ident in found:
            print(f"{port.device}  {ident}")


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
        print('KryonOS shell (Ctrl-D para sair)')
        while True:
            try:
                line = input("kryon> ").strip()
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
        print("aguardando logs do dispositivo (Ctrl-C para sair)", file=sys.stderr)
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
        die("uso: kryonctl ota push FIRMWARE.bin")
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
        out[i * 3] = (p >> 8) & 0xF8 | (p >> 13)
        out[i * 3 + 1] = (p >> 3) & 0xFC | (p >> 9)
        out[i * 3 + 2] = (p << 3) & 0xF8 | (p >> 2) & 0x07
    Image.frombytes("RGB", (w, h), bytes(out)).save(args.out)
    print(f"{args.out}: {w}x{h}")


# ---------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(prog="kryonctl", description="ferramenta USB do KryonOS (estilo adb)")
    parser.add_argument("-p", "--port", help="porta serial do canal kryonctl (ex: /dev/ttyUSB0)")
    parser.add_argument("-b", "--baud", type=int, default=DEFAULT_BAUD,
                        help="negocia este baud com o firmware (ex: 921600 acelera push/pull)")
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

    p = sub.add_parser("rm", help="apaga arquivo")
    p.add_argument("path")
    p.set_defaults(func=cmd_simple("DELETE"))

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
    p.set_defaults(func=cmd_logcat)

    p = sub.add_parser("ota", help="grava firmware pela conexao (sem esptool)")
    ota_sub = p.add_subparsers(dest="ota_cmd", required=True)
    po = ota_sub.add_parser("push", help="envia e grava um firmware.bin")
    po.add_argument("file")
    po.add_argument("--no-reboot", action="store_true", help="nao reinicia apos gravar")
    p.set_defaults(func=cmd_ota)

    p = sub.add_parser("screencap", help="captura da tela -> PNG")
    p.add_argument("out", nargs="?", default="kryon_screencap.png")
    p.set_defaults(func=cmd_screencap)

    args = parser.parse_args()
    if args.command == "pull" and args.local is None:
        args.local = os.path.basename(args.remote) or "kryon_pull.bin"

    try:
        args.func(args)
    except KryonError as e:
        die(str(e))
    except serial.SerialException as e:
        die(f"porta serial: {e}")
    except KeyboardInterrupt:
        print()
        sys.exit(130)


if __name__ == "__main__":
    main()
