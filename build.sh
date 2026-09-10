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

# -lm: numfmt.c uses <math.h> (floor/log10/isnan/isinf/signbit). Must come
# after the sources on the link line (GNU ld resolves libraries left to
# right against what's already been seen). Harmless where it's not needed
# (macOS's libSystem already has these symbols; passing -lm is a no-op).
#
# The rest is N5b's TLS (NATIVE_PLAN.md §3.1), and is the only
# platform-conditional part of this script:
#   -ldl                  Linux/BSD: platform.c dlopen()s OpenSSL at run
#                         time rather than linking it, so the binary still
#                         builds and runs on a machine with no OpenSSL at
#                         all. (glibc >= 2.34 folded dlopen into libc and
#                         ships -ldl as an empty stub, so this stays
#                         harmless on new distros while keeping old ones
#                         working.) There is no -ldl on macOS.
#   -framework Security   macOS: Secure Transport, part of the OS. No
#   -framework CoreFoundation                dlopen, no third-party dependency.
LIBS=(-lm)
case "$(uname -s)" in
    Darwin) LIBS+=(-framework Security -framework CoreFoundation) ;;
    *) LIBS+=(-ldl) ;;
esac

# native/ has two entry points -- main.c (the `funny` CLI) and stub_main.c
# (the yeet runtime stub, NATIVE_PLAN.md N7 task 2) -- so each binary gets
# every other source plus exactly one of them. The stub deliberately has no
# compiler in it: a shipped executable only ever runs bytecode, which is why
# a yeeted program is a couple of hundred KB rather than PyInstaller's 8 MB.
# toolchain_blob.c is excluded from CORE and added to the `funny` link only:
# funnyrt is a VM with no compiler in it, and linking 372KB it never
# references would land directly in the size of every yeeted program.
CORE=()
for f in "${SRCS[@]}"; do
    case "$f" in
        native/main.c | native/stub_main.c | native/toolchain_blob.c) ;;
        *) CORE+=("$f") ;;
    esac
done

echo "+ $CC ${FLAGS[*]} -o $OUT ${CORE[*]} native/main.c native/toolchain_blob.c ${LIBS[*]}"
"$CC" "${FLAGS[@]}" -o "$OUT" "${CORE[@]}" native/main.c native/toolchain_blob.c "${LIBS[@]}"

echo "+ $CC ${FLAGS[*]} -o funnyrt ${CORE[*]} native/stub_main.c ${LIBS[*]}"
"$CC" "${FLAGS[@]}" -o funnyrt "${CORE[@]}" native/stub_main.c "${LIBS[@]}"
