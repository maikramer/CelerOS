#!/usr/bin/env bash
# Gera a imagem LittleFS de data/ e grava na particao "littlefs".
# Uso: tools/flash_data.sh [smartdisplay|cyd] [PORTA]
# Requer ambiente ESP-IDF exportado (parttool via python do IDF).
set -euo pipefail

BOARD="${1:-smartdisplay}"
PORT="${2:-/dev/ttyUSB0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

case "$BOARD" in
  smartdisplay) PART_CSV="partitions_16MB.csv" ;;
  cyd)          PART_CSV="partitions_4MB.csv" ;;
  *) echo "board invalida: $BOARD (smartdisplay|cyd)"; exit 1 ;;
esac

# Tamanho vem do proprio CSV (fonte unica, sem duplicar valor aqui)
SIZE=$(awk -F',' '$1 ~ /^littlefs/ {gsub(/[[:space:]]/, "", $5); print $5}' "$ROOT/$PART_CSV")
if [ -z "$SIZE" ]; then echo "littlefs nao encontrada em $PART_CSV"; exit 1; fi

IMG="$ROOT/build/littlefs-$(date +%s).img"
"$ROOT/tools/bin/mklittlefs.bin" -c "$ROOT/data" -s "$SIZE" "$IMG"
python "$IDF_PATH/components/partition_table/parttool.py" \
    --port "$PORT" \
    write_partition --partition-name=littlefs --input "$IMG"
echo "OK: data/ gravado na particao littlefs ($BOARD, $SIZE bytes)"
rm -f "$IMG"
