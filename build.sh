#!/bin/sh
# build.sh - assemble a .asm file to a C64 .prg file
#
# Usage: ./build.sh source.asm [output.prg]
#

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 source.asm [output.prg]" >&2
    exit 1
fi

# ./c64asm input.asm -o output.prg [--listing out.lst]


SRC="./examples/$1"
BASE=$(basename "$SRC" .asm)
DIR=$(dirname "$SRC")
OUT="${2:-$DIR/$BASE.prg}"
LIST="${2:-$DIR/$BASE.lst}"

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
C64ASM="$SCRIPT_DIR/bin/c64asm"
[ -x "$C64ASM" ] || C64ASM=c64asm

echo "==> assembling $ASM -> $OUT"
"$C64ASM" "$SRC" -o "$OUT" --listing "$LIST"

echo "==> done: $OUT"
