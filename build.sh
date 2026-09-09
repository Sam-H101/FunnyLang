#!/usr/bin/env bash
# NATIVE_PLAN.md N0 task 1: one compiler invocation, no build system.
#
# Usage:
#   ./build.sh          release build -> ./funny
#   ./build.sh debug    -g -fsanitize=address,undefined -DFUNNY_DEBUG -> ./funny
#
# CC defaults to `cc` (whatever that resolves to: gcc or clang on Linux/macOS).
set -euo pipefail

CC="${CC:-cc}"
OUT="${OUT:-funny}"
MODE="${1:-release}"

# native/stdlib/*.c doesn't exist yet (lands starting N5) -- glob only what's
# actually present so this script never needs editing as files are added.
shopt -s nullglob
SRCS=(native/*.c native/stdlib/*.c)
shopt -u nullglob

if [ "${#SRCS[@]}" -eq 0 ]; then
    echo "build.sh: no sources found under native/" >&2
    exit 1
fi

if [ "$MODE" = "debug" ]; then
    FLAGS=(-std=c11 -g -O0 -Wall -Wextra -Werror -fsanitize=address,undefined -DFUNNY_DEBUG)
elif [ "$MODE" = "release" ]; then
    FLAGS=(-std=c11 -O2 -Wall -Wextra -Werror)
else
    echo "build.sh: unknown mode '$MODE' (expected 'release' or 'debug')" >&2
    exit 1
fi

echo "+ $CC ${FLAGS[*]} -o $OUT ${SRCS[*]}"
"$CC" "${FLAGS[@]}" -o "$OUT" "${SRCS[@]}"
