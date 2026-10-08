#!/usr/bin/env python3
"""Confere se os build*/sdkconfig da bancada seguem os sdkconfig.defaults.

O ESP-IDF so aplica um default NOVO a um sdkconfig que ainda nao tem a
chave: mudar um valor em sdkconfig.defaults (ou boards/<b>/sdkconfig.defaults)
nao chega a um build dir que ja existe. Foi assim que a receita de RAM
(pilha da main 32 -> 24 KB) ficou de fora do relogio e do cao na bancada
enquanto o CI (build limpo) a tinha (2026-10-08).

Para cada build*/ com CMakeCache.txt: le CELEROS_BOARD, junta
sdkconfig.defaults + boards/<placa>/sdkconfig.defaults (a placa vence) e
compara cada chave declarada com o sdkconfig do build. Sai com 1 se houver
divergencia.

    python3 tools/sdkconfig_check.py            # todos os build*/
    python3 tools/sdkconfig_check.py build-dog  # so esse

Corrigir: apague o build*/sdkconfig (o configure o regenera dos defaults) ou
edite a linha no sdkconfig do build.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LINE = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
UNSET = re.compile(r"^# (CONFIG_[A-Za-z0-9_]+) is not set$")


def parse(path):
    """chave -> valor ('n' para 'is not set')."""
    out = {}
    if not path.is_file():
        return out
    for raw in path.read_text(errors="replace").splitlines():
        line = raw.strip()
        m = LINE.match(line)
        if m:
            out[m.group(1)] = m.group(2)
            continue
        m = UNSET.match(line)
        if m:
            out[m.group(1)] = "n"
    return out


def board_of(build):
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        return None
    m = re.search(r"^CELEROS_BOARD:\w+=(.+)$", cache.read_text(errors="replace"), re.M)
    return m.group(1).strip() if m else "smartdisplay"


def check(build):
    board = board_of(build)
    if board is None:
        return None
    want = parse(ROOT / "sdkconfig.defaults")
    want.update(parse(ROOT / "boards" / board / "sdkconfig.defaults"))
    have = parse(build / "sdkconfig")
    if not have:
        return None
    diffs, unknown = [], []
    for key, val in sorted(want.items()):
        if key not in have:
            # nem "=..." nem "is not set": a opcao nao existe neste IDF/alvo
            # (renomeada ou de outro chip) — aviso, nao divergencia
            unknown.append(key)
            continue
        if have[key] != val:
            diffs.append((key, val, have[key]))
    return board, diffs, unknown


def main(args):
    builds = [ROOT / a for a in args] if args else sorted(ROOT.glob("build*/"))
    bad = 0
    for b in builds:
        r = check(b)
        if r is None:
            continue
        board, diffs, unknown = r
        if unknown:
            print("%-16s %-16s aviso: chave(s) que o IDF nao conhece: %s" % (b.name, board, ", ".join(unknown)))
        if not diffs:
            print("%-16s %-16s ok" % (b.name, board))
            continue
        bad += 1
        print("%-16s %-16s %d divergencia(s) com os defaults:" % (b.name, board, len(diffs)))
        for key, val, got in diffs:
            print("    %s: defaults=%s build=%s" % (key, val, got))
    if bad:
        print("\ncorrija apagando o <build>/sdkconfig (regenera dos defaults) ou editando a linha")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
