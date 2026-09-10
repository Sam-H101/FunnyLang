#!/usr/bin/env sh
# FunnyLang installer — NATIVE_PLAN.md N10 task 7.
#
#   curl -fsSL https://raw.githubusercontent.com/Sam-H101/FunnyLang/master/install.sh | sh
#
# Downloads the right binary for this machine, verifies its SHA-256 against
# the release's own SHA256SUMS, and drops it in ~/.local/bin. Refuses to
# proceed on a checksum mismatch — a corrupted or substituted download is
# exactly what a checksum is for, so there is no --force.
#
# POSIX sh on purpose: this has to run on a machine that has nothing on it.
set -eu

REPO="Sam-H101/FunnyLang"
VERSION="${FUNNY_VERSION:-latest}"
INSTALL_DIR="${FUNNY_INSTALL_DIR:-$HOME/.local/bin}"

say() { printf '%s\n' "$*"; }
die() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }

need() {
    command -v "$1" >/dev/null 2>&1 || die "this needs '$1' and it isn't on PATH."
}

# -- which binary --------------------------------------------------------

detect_target() {
    os=$(uname -s)
    arch=$(uname -m)
    case "$os" in
        Linux)  os_part=linux ;;
        Darwin) os_part=macos ;;
        *) die "no prebuilt binary for '$os'. Build from source: https://github.com/$REPO" ;;
    esac
    case "$arch" in
        x86_64 | amd64)  arch_part=x86_64 ;;
        arm64 | aarch64) arch_part=aarch64 ;;
        *) die "no prebuilt binary for '$arch'. Build from source: https://github.com/$REPO" ;;
    esac
    # macOS x86_64 is the one combination whose asset is named differently
    # from the uname pair, because Apple calls it x86_64 and everyone else
    # calls the ARM one aarch64. Keep the mapping in one place.
    printf '%s-%s' "$os_part" "$arch_part"
}

fetch() {
    # $1 url, $2 destination
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$1" -o "$2" || return 1
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$2" "$1" || return 1
    else
        die "this needs curl or wget and has neither."
    fi
}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    else
        die "this needs sha256sum or shasum to verify the download, and has neither."
    fi
}

# -- do it ---------------------------------------------------------------

need mkdir
target=$(detect_target)
asset="funny-$target"
stub="funnyrt-$target"

# FUNNY_BASE_URL exists so this can be pointed at a local directory and
# actually tested. A release script that is only ever exercised during a
# release is a bad place to find out the quoting is wrong.
if [ -n "${FUNNY_BASE_URL:-}" ]; then
    base="$FUNNY_BASE_URL"
elif [ "$VERSION" = "latest" ]; then
    base="https://github.com/$REPO/releases/latest/download"
else
    base="https://github.com/$REPO/releases/download/$VERSION"
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

say "FunnyLang: fetching $asset ($VERSION)"
fetch "$base/$asset" "$tmp/$asset" || die "couldn't download $base/$asset"
fetch "$base/SHA256SUMS" "$tmp/SHA256SUMS" || die "couldn't download the checksums."

expected=$(grep " $asset\$" "$tmp/SHA256SUMS" | cut -d' ' -f1 || true)
[ -n "$expected" ] || die "SHA256SUMS has no entry for $asset. Refusing to install."
actual=$(sha256_of "$tmp/$asset")
if [ "$expected" != "$actual" ]; then
    say "  expected $expected"
    say "  got      $actual"
    die "checksum mismatch. Refusing to install."
fi
say "  checksum ok"

# The stub is optional: it is only needed by `funny yeet`. A failure to fetch
# it is a warning, not a failed install.
if fetch "$base/$stub" "$tmp/$stub" 2>/dev/null; then
    stub_expected=$(grep " $stub\$" "$tmp/SHA256SUMS" | cut -d' ' -f1 || true)
    if [ -n "$stub_expected" ] && [ "$stub_expected" = "$(sha256_of "$tmp/$stub")" ]; then
        have_stub=1
    else
        say "  warning: the yeet stub failed its checksum; skipping it. 'funny yeet' will not work."
        have_stub=0
    fi
else
    say "  warning: couldn't fetch the yeet stub; 'funny yeet' will not work."
    have_stub=0
fi

mkdir -p "$INSTALL_DIR"
mv "$tmp/$asset" "$INSTALL_DIR/funny"
chmod +x "$INSTALL_DIR/funny"
if [ "${have_stub:-0}" = "1" ]; then
    mv "$tmp/$stub" "$INSTALL_DIR/funnyrt"
    chmod +x "$INSTALL_DIR/funnyrt"
fi

say ""
say "installed: $INSTALL_DIR/funny"
"$INSTALL_DIR/funny" --version

case ":$PATH:" in
    *":$INSTALL_DIR:"*) ;;
    *)
        say ""
        say "$INSTALL_DIR isn't on your PATH. Add this to your shell profile:"
        say "    export PATH=\"\$PATH:$INSTALL_DIR\""
        ;;
esac

say ""
say "try:  funny vibe"
