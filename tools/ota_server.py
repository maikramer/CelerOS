#!/usr/bin/env python3
"""Servidor OTA de desenvolvimento para o KryonOS.

Serve o firmware.bin de uma build local + um update.json dinamico para a
placa testar o fluxo de OTA over-the-air na LAN, sem precisar pushar nada
para o GitHub.

Uso tipico:

    pio run -e smartdisplay_4848S040
    python3 tools/ota_server.py --env smartdisplay_4848S040

A versao publicada e lida do platformio.ini (KRYONOS_VERSION); para testar
a atualizacao, suba a versao la, rebuild e reinicie o servidor. No
dispositivo, grave em /local/ota_url.txt a URL impressa no inicio:

    http://<ip-do-pc>:10234/update.json

(coloque o arquivo pelo web file manager ou pelo cartao SD). Remova o
arquivo para voltar a usar o canal do GitHub.
"""

import argparse
import json
import os
import re
import socket
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse


def lan_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def read_version(repo_root: str, override: str | None) -> str:
    if override:
        return override
    ini = os.path.join(repo_root, "platformio.ini")
    try:
        with open(ini, encoding="utf-8") as f:
            m = re.search(r'KRYONOS_VERSION=\\"([^"\\]+)\\"', f.read())
        if m:
            return m.group(1)
    except OSError:
        pass
    return "0.0.0"


class Handler(BaseHTTPRequestHandler):
    bin_path: str = ""
    version: str = "0.0.0"

    def do_GET(self):  # noqa: N802
        path = urlparse(self.path).path
        if path in ("/", "/update.json"):
            doc = {
                "version": self.version,
                "api_version": 2,
                "major_update": True,
                "minor_update": False,
                "security_update": False,
                "changelog": "- OTA dev build served by tools/ota_server.py",
                "guide": "Tap INSTALL to flash this build.",
                # Relativa: o dispositivo resolve contra o diretorio do update.json
                "firmware_url": "firmware.bin",
            }
            body = json.dumps(doc, indent=2).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(body)
        elif path == "/firmware.bin":
            try:
                size = os.path.getsize(self.bin_path)
            except OSError:
                self.send_error(404, "firmware.bin not found (build first)")
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(size))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            with open(self.bin_path, "rb") as f:
                while chunk := f.read(64 * 1024):
                    self.wfile.write(chunk)
        else:
            self.send_error(404)

    def do_HEAD(self):  # noqa: N802
        self.do_GET()

    def log_message(self, fmt, *args):
        print(f"[ota] {self.address_string()} {fmt % args}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--env", default="smartdisplay_4848S040", help="env do PlatformIO (para achar o firmware.bin)")
    ap.add_argument("--bin", help="caminho direto do firmware.bin (sobrepoe --env)")
    ap.add_argument("--port", type=int, default=10234)
    ap.add_argument("--version", help="versao a publicar (default: KRYONOS_VERSION do platformio.ini)")
    args = ap.parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    bin_path = args.bin or os.path.join(repo_root, ".pio", "build", args.env, "firmware.bin")
    if not os.path.isfile(bin_path):
        raise SystemExit(f"firmware nao encontrado: {bin_path} (rode pio run antes)")

    version = read_version(repo_root, args.version)
    Handler.bin_path = bin_path
    Handler.version = version

    ip = lan_ip()
    print(f"[ota] firmware : {bin_path} ({os.path.getsize(bin_path)} bytes)")
    print(f"[ota] version  : {version}")
    print(f"[ota] no device: escreva em /local/ota_url.txt ->")
    print(f"[ota]            http://{ip}:{args.port}/update.json")
    print(f"[ota] servindo em http://{ip}:{args.port} (Ctrl+C para sair)")

    HTTPServer(("0.0.0.0", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
