#!/usr/bin/env bash
# Gera a imagem LittleFS de data/ e grava na particao "littlefs".
# Uso: tools/flash_data.sh [smartdisplay|cyd|spotpear-dog] [PORTA]
# Requer ambiente ESP-IDF exportado (parttool via python do IDF).
set -euo pipefail

BOARD="${1:-smartdisplay}"
PORT="${2:-/dev/ttyUSB0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

case "$BOARD" in
  smartdisplay|spotpear-dog) PART_CSV="partitions_16MB.csv" ;;
  cyd)          PART_CSV="partitions_4MB.csv" ;;
  *) echo "board invalida: $BOARD (smartdisplay|cyd|spotpear-dog)"; exit 1 ;;
esac

# Tamanho vem do proprio CSV (fonte unica, sem duplicar valor aqui)
SIZE=$(awk -F',' '$1 ~ /^littlefs/ {gsub(/[[:space:]]/, "", $5); print $5}' "$ROOT/$PART_CSV")
if [ -z "$SIZE" ]; then echo "littlefs nao encontrada em $PART_CSV"; exit 1; fi

# Overlay opcional da placa: boards/<placa>/data/ soma por cima de data/
# (apps exclusivos de uma placa — ex.: Dog Face do cao robotico).
STAGE="$ROOT/build/littlefs-stage-$$"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp -a "$ROOT/data/." "$STAGE/"
if [ -d "$ROOT/boards/$BOARD/data" ]; then
    cp -a "$ROOT/boards/$BOARD/data/." "$STAGE/"
    echo "overlay: boards/$BOARD/data somado"
fi

IMG="$ROOT/build/littlefs-$(date +%s).img"
"$ROOT/tools/bin/mklittlefs.bin" -c "$STAGE" -s "$SIZE" "$IMG"
rm -rf "$STAGE"
python "$IDF_PATH/components/partition_table/parttool.py" \
    --port "$PORT" \
    write_partition --partition-name=littlefs --input "$IMG"
echo "OK: data/ gravado na particao littlefs ($BOARD, $SIZE bytes)"
rm -f "$IMG"
