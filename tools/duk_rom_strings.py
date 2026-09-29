#!/usr/bin/env python3
"""Gera components/duktape/celeros_rom_strings.yaml.

Com DUK_USE_ROM_STRINGS, as strings listadas em add_forced_strings vao para
a flash junto com as dos builtins: os nomes da API JS (System.drawRect,
FS.readTextFile, BLACK...) deixam de ser internados na RAM a cada app.

Uso: python3 tools/duk_rom_strings.py   (depois regerar o Duktape, ver
components/duktape/celeros_duk_config.yaml)
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
RUNTIME = ROOT / "main" / "Runtime"
OUT = ROOT / "components" / "duktape" / "celeros_rom_strings.yaml"

PATTERNS = [
    re.compile(r'\{\s*"([A-Za-z_$][A-Za-z0-9_$]*)"\s*,\s*js_'),               # tabelas JsFn
    re.compile(r'duk_put_prop_string\([^,]+,\s*-?\d+\s*,\s*"([A-Za-z_$][A-Za-z0-9_$]*)"'),
    re.compile(r'duk_(?:get|put)_global_string\([^,]+,\s*"([A-Za-z_$][A-Za-z0-9_$]*)"'),
    re.compile(r'duk_get_prop_string\([^,]+,\s*-?\d+\s*,\s*"([A-Za-z_$][A-Za-z0-9_$]*)"'),
]


def main():
    names = set()
    for src in sorted(RUNTIME.glob("*.cpp")):
        text = src.read_text(encoding="utf-8", errors="replace")
        for pat in PATTERNS:
            names.update(pat.findall(text))
    lines = [
        "# GERADO por tools/duk_rom_strings.py — nao editar a mao.",
        "# Nomes da API JS do CelerOS forcados para a ROM do Duktape.",
        "add_forced_strings:",
    ]
    lines += ['  - str: "%s"' % n for n in sorted(names)]
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("%d strings -> %s" % (len(names), OUT.relative_to(ROOT)))


if __name__ == "__main__":
    main()
