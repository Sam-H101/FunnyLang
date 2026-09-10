"""NATIVE_PLAN.md N5b's acceptance gate: real HTTPS, over the OS's own TLS.

Everything here needs the live internet, so it is marked `network` and
skips itself under `FUNNY_NO_NET=1` (which is also what the differential
suite next door runs under -- that suite sets the variable per-test via a
fixture precisely so it can't reach in here and silently skip this one).

Deliberately not part of the differential suite: `tests/native/programs/`
diffs the C VM's stdout against the Python VM's byte for byte, which needs
output that doesn't depend on a third party being reachable. These tests
assert *properties* of a live fetch instead, the same exception already
made for randomness (`rizz`) and timing (`clock`).
"""
from __future__ import annotations

import os
import subprocess
import textwrap
from pathlib import Path

import pytest

from funnylang.compiler import Compiler
from funnylang.parser import parse_source
from funnylang.resolver import resolve_program
from funnylang.serializer import dump_funnyc
from funnylang.source import SourceFile

pytestmark = pytest.mark.network


def _requires_network():
    # Checked at call time, not import time: whether the network is off is
    # a property of the run, and reading it late keeps this immune to any
    # other module's environment fiddling.
    if os.environ.get("FUNNY_NO_NET") == "1":
        pytest.skip("FUNNY_NO_NET=1 -- the live-network gate is opt-in")


def _run_native(binary: Path, tmp_path: Path, src: str) -> subprocess.CompletedProcess:
    source_path = tmp_path / "net.funny"
    source_path.write_text(textwrap.dedent(src), encoding="utf-8")
    source = SourceFile(str(source_path), source_path.read_text(encoding="utf-8"))
    program = parse_source(source)
    resolved = resolve_program(program, source)
    unit = Compiler(resolved, source).compile_program(program, str(source_path))
    funnyc_path = tmp_path / "net.funnyc"
    funnyc_path.write_bytes(dump_funnyc(unit))
    env = dict(os.environ)
    env.pop("FUNNY_NO_NET", None)
    return subprocess.run([str(binary), str(funnyc_path)], capture_output=True, text=True, env=env, timeout=120)


def test_https_fetch_returns_200_and_a_body(native_binary, tmp_path):
    """§3.1's headline: `internet.go_brrrr` reaches an https:// URL through
    the OS's TLS -- dlopen'd OpenSSL, WinHTTP or Security.framework
    depending on the platform -- and gets a real response back."""
    _requires_network()
    result = _run_native(
        native_binary,
        tmp_path,
        """
        gimme internet
        yo r = internet.go_brrrr("https://example.com/")
        yap r["status"]
        yap r["body"].how_thicc() > 100
        yap what_is_it(r["headers"])
        """,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout == "200\nfax\ngroupchat\n"


def test_https_certificate_verification_rejects_bad_certificates(native_binary, tmp_path):
    """Verification is on unconditionally and there is no way to turn it
    off (§3.1). Each of these must be refused -- `wrong.host` in particular
    catches the classic mistake of verifying the chain but never checking
    that the certificate was issued for the host actually being talked to.
    """
    _requires_network()
    result = _run_native(
        native_binary,
        tmp_path,
        """
        gimme internet
        bet probe(url) {
            sketchy {
                internet.go_brrrr(url)
                yap "ACCEPTED"
            } my_bad (e) {
                yap "rejected"
            }
        }
        probe("https://expired.badssl.com/")
        probe("https://self-signed.badssl.com/")
        probe("https://wrong.host.badssl.com/")
        probe("https://untrusted-root.badssl.com/")
        """,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout == "rejected\nrejected\nrejected\nrejected\n"


def test_https_download_writes_the_real_bytes(native_binary, tmp_path):
    """`internet.download()` goes through the same interface (N5b task 7),
    so it gets TLS for free rather than having its own transport."""
    _requires_network()
    out_file = tmp_path / "downloaded.html"
    result = _run_native(
        native_binary,
        tmp_path,
        f"""
        gimme internet
        yo n = internet.download("https://example.com/", "{out_file.as_posix()}")
        yap n > 100
        """,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout == "fax\n"
    assert out_file.exists()
    assert b"Example Domain" in out_file.read_bytes()


def test_http_to_https_redirect_is_followed(native_binary, tmp_path):
    """A plain-http entry point that redirects to https has to hand over to
    the TLS path mid-chain rather than failing -- on Windows that means
    handing the rest of the request to WinHTTP part-way through the
    redirect loop."""
    _requires_network()
    result = _run_native(
        native_binary,
        tmp_path,
        """
        gimme internet
        yo r = internet.go_brrrr("http://github.com/")
        yap r["status"]
        yap r["body"].how_thicc() > 100
        """,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout == "200\nfax\n"


def test_network_disabled_short_circuits_before_any_tls_backend(native_binary, tmp_path):
    """`FUNNY_NO_NET=1` still refuses before a backend is even loaded --
    the one test here that deliberately keeps the variable set, so it needs
    no network of its own."""
    source_path = tmp_path / "off.funny"
    source_path.write_text(
        'gimme internet\nsketchy {\n internet.go_brrrr("https://example.com/")\n} my_bad (e) {\n yap e.flavor\n}\n',
        encoding="utf-8",
    )
    source = SourceFile(str(source_path), source_path.read_text(encoding="utf-8"))
    program = parse_source(source)
    unit = Compiler(resolve_program(program, source), source).compile_program(program, str(source_path))
    funnyc_path = tmp_path / "off.funnyc"
    funnyc_path.write_bytes(dump_funnyc(unit))
    env = dict(os.environ)
    env["FUNNY_NO_NET"] = "1"
    result = subprocess.run([str(native_binary), str(funnyc_path)], capture_output=True, text=True, env=env, timeout=60)
    assert result.returncode == 0, result.stderr
    assert result.stdout == "SkillIssue\n"
