#!/usr/bin/env bash
# Gera a imagem LittleFS de data/ e grava na particao "littlefs".
# Uso: tools/flash_data.sh [smartdisplay|cyd] [PORTA]
# Requer ambiente ESP-IDF exportado (parttool via python do IDF).
set -euo pipefail

BOARD="${1:-smartdisplay}"
PORT="${2:-/dev/ttyUSB0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

case "$BOARD" in
  smartdisplay) SIZE=$((0x360000)); PART_CSV="partitions_16MB.csv" ;;
  cyd)          SIZE=$((0x20000)); PART_CSV="partitions_4MB.csv" ;;
  *) echo "board invalida: $BOARD (smartdisplay|cyd)"; exit 1 ;;
esac

IMG="$ROOT/build/littlefs-$(date +%s).img"
"$ROOT/tools/bin/mklittlefs.bin" -c "$ROOT/data" -s "$SIZE" "$IMG"
python "$IDF_PATH/components/partition_table/parttool.py" \
    --port "$PORT" \
    write_partition --partition-name=littlefs --input "$IMG"
echo "OK: data/ gravado na particao littlefs ($BOARD, $SIZE bytes)"
rm -f "$IMG"
