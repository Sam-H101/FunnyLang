# syntax=docker/dockerfile:1
#
# Battleship, in a container.
#
#     docker build -f extensive_examples/battleship/Dockerfile -t funnylang-battleship .
#     docker run --rm -p 8443:8443 funnylang-battleship
#
# then https://localhost:8443, and accept the certificate warning -- the image
# ships this example's own test certificate, signed by a test CA that nothing
# on earth trusts, which is exactly what an unknown CA is for.
#
# THE BUILD STAGE INSTALLS A C COMPILER AND NOTHING ELSE. No Python, no
# package manager for the language, no build system, no third-party library.
# That is the repository's whole claim, and a Dockerfile is the shortest
# possible proof of it: `gcc` and `libc6-dev` go in, and a working toolchain
# comes out about fifteen seconds later.
#
# THE RUNTIME STAGE HAS NO TOOLCHAIN IN IT AT ALL. `funny yeet` freezes a
# program into a standalone executable -- the runtime stub with the compiled
# bytecode appended -- so what ships is two binaries of about 450 KB each, the
# page, and a test certificate. There is no interpreter in the image, nothing
# to `funny run`, and nothing that could compile anything.
#
# THE HEALTHCHECK IS ALSO FUNNYLANG. The usual move is to apt-get curl into
# the runtime image so that Docker has something to ask the server with, which
# would be a strange thing to do in an image whose entire point is that it
# needs nothing installed. `healthcheck.funny` is yeeted beside the server, and
# it completes the TLS handshake against the certificate the server is actually
# presenting and insists on a real game in the reply -- see its own header.

ARG DEBIAN=debian:12-slim

# ---------------------------------------------------------------- build ----

FROM ${DEBIAN} AS build

# gcc and libc6-dev. That is the entire dependency list for the language.
RUN apt-get update \
 && apt-get install -y --no-install-recommends gcc libc6-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# One compiler invocation and no build system: ./funny and ./funnyrt.
RUN ./build.sh

# Prove it before shipping it. The golden drives the rules, the engine and a
# whole game; it has no sockets in it, so it needs nothing but the binary that
# was just built.
RUN FUNNY_NO_NET=1 ./funny test extensive_examples/battleship

# Freeze the server and its healthcheck into standalone executables, and put
# the page and the certificate beside them. `the_script()` in a yeeted binary
# is the executable itself, so `web/` and `certs/` have to sit next to it --
# that is checked, not assumed.
RUN mkdir -p /app \
 && ./funny yeet extensive_examples/battleship/serve.funny       -o /app/battleship \
 && ./funny yeet extensive_examples/battleship/healthcheck.funny -o /app/healthcheck \
 && cp -r extensive_examples/battleship/web   /app/web \
 && cp -r extensive_examples/battleship/certs /app/certs

# -------------------------------------------------------------- runtime ----

FROM ${DEBIAN} AS runtime

# `ldd` on the yeeted binary lists libm, libc and the loader, and nothing
# else. OpenSSL is not linked -- it is `dlopen`'d the first time a TLS socket
# is opened -- so `libssl3` is here for the TLS listener and for nothing else.
# Running with `--no-tls` behind a reverse proxy does not need it.
RUN apt-get update \
 && apt-get install -y --no-install-recommends libssl3 \
 && rm -rf /var/lib/apt/lists/* \
 && groupadd --system --gid 10001 admiral \
 && useradd --system --uid 10001 --gid 10001 --no-create-home --shell /usr/sbin/nologin admiral

# Owned by root and read by nobody in particular. The server writes no files,
# so the container runs happily with `--read-only`.
COPY --from=build --chown=root:root /app /app

# Read by the healthcheck only. If you change the port in the command below,
# change BATTLESHIP_PORT with it, or the healthcheck will be asking the wrong
# door whether anybody is home.
ENV BATTLESHIP_PORT=8443 \
    BATTLESHIP_TLS=1 \
    BATTLESHIP_CA=/app/certs/ca.pem

USER admiral
WORKDIR /app
EXPOSE 8443

HEALTHCHECK --interval=15s --timeout=5s --start-period=5s --retries=3 \
    CMD ["/app/healthcheck"]

# `--` is how a FunnyLang program is handed its own arguments, so it belongs to
# the entrypoint rather than to the command. Everything after it is
# serve.funny's, and `docker run <image> --help` prints its usage.
#
# --host 0.0.0.0 is not optional in a container: the server defaults to
# loopback only, and a container's loopback is not yours.
ENTRYPOINT ["/app/battleship", "--"]
CMD ["8443", "--host", "0.0.0.0"]
