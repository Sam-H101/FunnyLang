"""Native executable packaging (PLAN.md §M10). Freezes `stub_main.py` into a
runtime stub via PyInstaller once, then `funny yeet` just appends a linked
`.funnypak` payload plus a 17-byte trailer (§5.4) — no per-program freeze."""
from __future__ import annotations

import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

TRAILER_MAGIC = b"FUNNYYEET"
STUB_NAME = "funnyrt"

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "build"
STUB_DIR = BUILD_DIR / "stub"
WORK_DIR = BUILD_DIR / "_work"
SPEC_DIR = BUILD_DIR / "_spec"

# Every stdlib module is loaded dynamically (funnylang/stdlib/__init__.py
# picks one by name at runtime), so PyInstaller's static import scan needs a
# nudge even though the `from . import x` lines are textually present.
_STDLIB_MODULES = (
    "mafs", "yapper", "stash", "groupchat", "rizz", "filez",
    "clock", "sus", "computer", "internet",
)


class StubBuildError(RuntimeError):
    pass


def _stub_binary_name() -> str:
    return f"{STUB_NAME}.exe" if sys.platform == "win32" else STUB_NAME


def stub_path() -> Path:
    return STUB_DIR / _stub_binary_name()


def _hash_marker_path() -> Path:
    return STUB_DIR / f"{STUB_NAME}.hash"


def _source_hash() -> str:
    h = hashlib.sha256()
    h.update(sys.version.encode("utf-8"))
    pkg_dir = Path(__file__).resolve().parent
    for path in sorted(pkg_dir.rglob("*.py")):
        h.update(str(path.relative_to(pkg_dir)).encode("utf-8"))
        h.update(path.read_bytes())
    return h.hexdigest()


def pyinstaller_available() -> bool:
    try:
        import PyInstaller  # noqa: F401
        return True
    except ImportError:
        return False


def ensure_stub(*, icon: str | None = None, rebuild: bool = False) -> Path:
    """Builds the frozen stub if it's missing or stale, and returns its path.
    Cached by a hash of every funnylang/**/*.py file plus the Python version,
    so it only rebuilds when the runtime itself actually changed."""
    target = stub_path()
    marker = _hash_marker_path()
    current_hash = _source_hash()
    if not rebuild and target.exists() and marker.exists():
        if marker.read_text(encoding="utf-8").strip() == current_hash:
            return target

    if not pyinstaller_available():
        raise StubBuildError(
            "PyInstaller isn't installed. run: pip install pyinstaller"
        )

    STUB_DIR.mkdir(parents=True, exist_ok=True)
    WORK_DIR.mkdir(parents=True, exist_ok=True)
    SPEC_DIR.mkdir(parents=True, exist_ok=True)

    entry_script = Path(__file__).resolve().parent / "stub_main.py"
    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--onefile", "--name", STUB_NAME,
        "--distpath", str(STUB_DIR),
        "--workpath", str(WORK_DIR),
        "--specpath", str(SPEC_DIR),
        "--console",
        "--noconfirm",
    ]
    for mod in _STDLIB_MODULES:
        cmd += ["--hidden-import", f"funnylang.stdlib.{mod}"]
    if icon:
        cmd += ["--icon", icon]
    cmd.append(str(entry_script))

    result = subprocess.run(cmd, cwd=str(REPO_ROOT), capture_output=True, text=True)
    if result.returncode != 0 or not target.exists():
        raise StubBuildError(
            f"PyInstaller failed (exit {result.returncode}):\n{result.stdout[-4000:]}\n{result.stderr[-4000:]}"
        )
    marker.write_text(current_hash, encoding="utf-8")
    return target


def yeet(pak_bytes: bytes, out_path: str, *, icon: str | None = None, rebuild_stub: bool = False) -> int:
    """Copies the (cached) stub to `out_path`, appends `pak_bytes` plus the
    17-byte trailer (§5.4), and marks it executable on POSIX. Returns the
    final file's total size in bytes."""
    stub = ensure_stub(icon=icon, rebuild=rebuild_stub)
    out = Path(out_path)
    shutil.copyfile(stub, out)
    with open(out, "ab") as f:
        f.write(pak_bytes)
        f.write(TRAILER_MAGIC)
        f.write(len(pak_bytes).to_bytes(8, "big"))
    if sys.platform != "win32":
        out.chmod(out.stat().st_mode | 0o111)
    return out.stat().st_size
