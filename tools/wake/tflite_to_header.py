#!/usr/bin/env python3
"""Converte o hi_celer.tflite treinado em header C embutido no firmware.

Uso: python3 tools/wake/tflite_to_header.py [caminho/do/hi_celer.tflite]
Gera main/Assets/Wake/HiCelerModel.h (padrao "generated but committed" do
repo, como CelerFonts/SplashLogo). Sem o arquivo, escreve o placeholder
(vetor vazio — o WakeWord.start() devolve false e o build segue verde).
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
OUT = ROOT / "main" / "Assets" / "Wake" / "HiCelerModel.h"

src = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "")
data = src.read_bytes() if src.is_file() else b""

rows = []
for i in range(0, len(data), 12):
    rows.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 12]) + ",")
body = "\n".join(rows) if rows else ""  # placeholder: vetor vazio

hdr = f"""#ifndef CELER_ASSETS_WAKE_HICELER_MODEL_H
#define CELER_ASSETS_WAKE_HICELER_MODEL_H

// GERADO por tools/wake/tflite_to_header.py — nao editar na mao.
// Modelo microWakeWord "hi celer" ({len(data)} bytes, int8 streaming,
// pre-processamento no frontend C++ de main/Hardware/Wake/frontend).
// Origem: treino proprio (tools/wake/README.md){f" a partir de {src.name}" if src.is_file() else "; PLACEHOLDER vazio (sem modelo treinado)"}.

#include <stdint.h>
#include <stddef.h>

// clang-format off
static const uint8_t kHiCelerModel[] = {{
{body}
}};
// clang-format on
static const size_t kHiCelerModelLen = sizeof(kHiCelerModel);

#endif  // CELER_ASSETS_WAKE_HICELER_MODEL_H
"""
OUT.write_text(hdr)
print(f"{OUT}: {len(data)} bytes -> kHiCelerModel")
