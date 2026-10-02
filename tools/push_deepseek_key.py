#!/usr/bin/env python3
"""Provisiona a chave da DeepSeek no aparelho.

Le DEEPSEEK_API_KEY do .env na raiz do repo e grava /local/deepseek_key.txt
no device via celerctl push. A chave NUNCA vai para o git (o .gitignore
cobre o .env; ver nota de seguranca no AGENTS.md).

Uso:
    python3 tools/push_deepseek_key.py                # autodetecta a porta
    python3 tools/push_deepseek_key.py -p /dev/ttyUSB0
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENV_FILE = os.path.join(ROOT, ".env")
KEY_PATH = "/local/deepseek_key.txt"


def read_env_key(path=ENV_FILE):
    if not os.path.exists(path):
        print(f"erro: {path} nao existe (copie de .env.example)", file=sys.stderr)
        return None
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            if k.strip() == "DEEPSEEK_API_KEY":
                return v.strip()
    return None


def main():
    key = read_env_key()
    if not key:
        print("erro: DEEPSEEK_API_KEY vazia no .env", file=sys.stderr)
        return 1
    if not key.startswith("sk-"):
        print("aviso: a chave nao comeca com 'sk-' (formato inesperado)",
              file=sys.stderr)

    # arquivo temporario FORA do repo: o push le o conteudo e o device
    # grava em /local (o temp some no fim, nada sobra no disco)
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as tmp:
        tmp.write(key + "\n")
        tmp_path = tmp.name
    try:
        cmd = [sys.executable, os.path.join(ROOT, "tools", "celerctl.py"),
               "push", tmp_path, KEY_PATH]
        cmd += sys.argv[1:]  # -p PORT, -b BAUD, etc.
        rc = subprocess.call(cmd)
    finally:
        os.unlink(tmp_path)
    if rc == 0:
        print(f"ok: chave provisionada em {KEY_PATH}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
