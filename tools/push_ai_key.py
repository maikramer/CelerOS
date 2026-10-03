#!/usr/bin/env python3
"""Provisiona chaves de IA no aparelho.

Le a chave do .env na raiz do repo e grava /local/<provider>_key.txt no
device via celerctl push. A chave NUNCA vai para o git (o .gitignore cobre
o .env; ver nota de seguranca no AGENTS.md).

Providers (objeto AI do runtime, ver JsAi.cpp):
    deepseek   -> DEEPSEEK_API_KEY    -> /local/deepseek_key.txt
    openrouter -> OPENROUTER_API_KEY  -> /local/openrouter_key.txt

Uso:
    python3 tools/push_ai_key.py openrouter           # autodetecta a porta
    python3 tools/push_ai_key.py deepseek -p /dev/ttyUSB0
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENV_FILE = os.path.join(ROOT, ".env")

PROVIDERS = {
    "deepseek": ("DEEPSEEK_API_KEY", "/local/deepseek_key.txt", "sk-"),
    "openrouter": ("OPENROUTER_API_KEY", "/local/openrouter_key.txt", "sk-or-"),
}


def read_env_key(env_var, path=ENV_FILE):
    if not os.path.exists(path):
        print(f"erro: {path} nao existe (copie de .env.example)", file=sys.stderr)
        return None
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            if k.strip() == env_var:
                return v.strip()
    return None


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in PROVIDERS:
        names = "|".join(PROVIDERS)
        print(f"uso: {sys.argv[0]} <{names}> [-p PORT ...]", file=sys.stderr)
        return 1
    provider = sys.argv[1]
    env_var, key_path, prefix = PROVIDERS[provider]
    key = read_env_key(env_var)
    if not key:
        print(f"erro: {env_var} vazia no .env", file=sys.stderr)
        return 1
    if not key.startswith(prefix):
        print(f"aviso: a chave nao comeca com '{prefix}' (formato inesperado)",
              file=sys.stderr)

    # arquivo temporario FORA do repo: o push le o conteudo e o device
    # grava em /local (o temp some no fim, nada sobra no disco). Flags do
    # celerctl (-p PORT, -b BAUD...) sao globais: entram ANTES do subcomando.
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as tmp:
        tmp.write(key + "\n")
        tmp_path = tmp.name
    try:
        cmd = [sys.executable, os.path.join(ROOT, "tools", "celerctl.py")]
        cmd += sys.argv[2:]  # -p PORT, -b BAUD, etc.
        cmd += ["push", tmp_path, key_path]
        rc = subprocess.call(cmd)
    finally:
        os.unlink(tmp_path)
    if rc == 0:
        print(f"ok: chave do {provider} provisionada em {key_path}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
