#!/usr/bin/env sh
# make_cert.sh -- a local test CA and a localhost server certificate, as PKCS#12.
#
#     sh extensive_examples/web_server_https/certs/make_cert.sh
#
# Writes, beside this script:
#   ca.pem     the CA certificate. Give it to a client (`curl --cacert`) or to
#              `internet.slide_into(..., {"ca": ...})`. Public; not a secret.
#   site.p12   the server's certificate chain and private key, PKCS#12,
#              protected by $FUNNY_PFX_PASSWORD (default "funnylang").
#
# THIS IS TEST MATERIAL. The CA key is thrown away as soon as the leaf is
# signed, so nothing can ever be issued from it again, and the leaf names only
# localhost / 127.0.0.1 / ::1. The committed copies are deliberately public.
#
# Two choices here are about portability, not taste:
#
#   * PBE-SHA1-3DES + a SHA-1 MAC. OpenSSL 3's default PKCS#12 encoding
#     (AES-256 + PBKDF2) is not readable by every Secure Transport or Schannel
#     still in use, and the "-legacy" one (RC2-40) is not readable by OpenSSL 3
#     itself without the legacy provider. 3DES is the one encoding all three
#     backends read out of the box. It protects a test key on disk; the TLS
#     session itself is negotiated separately and is not affected.
#   * 825 days on the leaf. Apple rejects any TLS server certificate issued
#     after 2019 with a longer validity, even under a private CA. The CA
#     itself gets ten years.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
PASS="${FUNNY_PFX_PASSWORD:-funnylang}"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 3650 \
    -keyout "$WORK/ca.key" -out "$HERE/ca.pem" \
    -subj "/O=FunnyLang/CN=FunnyLang Test CA (not a secret)" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign"

openssl req -newkey rsa:2048 -sha256 -nodes \
    -keyout "$WORK/site.key" -out "$WORK/site.csr" \
    -subj "/O=FunnyLang/CN=localhost"

cat > "$WORK/leaf.cnf" <<'CNF'
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1
CNF

openssl x509 -req -sha256 -days 825 -in "$WORK/site.csr" \
    -CA "$HERE/ca.pem" -CAkey "$WORK/ca.key" -set_serial "0x$(openssl rand -hex 8)" \
    -extfile "$WORK/leaf.cnf" -out "$WORK/site.pem"

openssl pkcs12 -export -name localhost \
    -inkey "$WORK/site.key" -in "$WORK/site.pem" -certfile "$HERE/ca.pem" \
    -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg sha1 \
    -passout "pass:$PASS" -out "$HERE/site.p12"

echo "wrote $HERE/ca.pem and $HERE/site.p12"
openssl x509 -in "$WORK/site.pem" -noout -subject -enddate
