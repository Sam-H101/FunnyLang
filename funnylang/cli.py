"""The `funny` command-line interface."""
from __future__ import annotations

import sys

from . import __version__

BANNER = r"""
   ███████╗██╗   ██╗███╗   ██╗███╗   ██╗██╗   ██╗
   ██╔════╝██║   ██║████╗  ██║████╗  ██║╚██╗ ██╔╝
   █████╗  ██║   ██║██╔██╗ ██║██╔██╗ ██║ ╚████╔╝
   ██╔══╝  ██║   ██║██║╚██╗██║██║╚██╗██║  ╚██╔╝
   ██║     ╚██████╔╝██║ ╚████║██║ ╚████║   ██║
   ╚═╝      ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═══╝   ╚═╝
        FunnyLang v{version} — it compiles. somehow.
"""


def banner() -> str:
    return BANNER.format(version=__version__)


def _ensure_utf8_stdio() -> None:
    """FunnyLang is full of emoji. Windows consoles default to cp1252 and
    will crash on them unless we force UTF-8 on the standard streams."""
    for stream_name in ("stdout", "stderr"):
        stream = getattr(sys, stream_name, None)
        reconfigure = getattr(stream, "reconfigure", None)
        if callable(reconfigure):
            try:
                reconfigure(encoding="utf-8", errors="replace")
            except (ValueError, OSError):
                pass


def main(argv: list[str] | None = None) -> int:
    _ensure_utf8_stdio()
    if argv is None:
        argv = sys.argv[1:]
    print(banner())
    return 0


if __name__ == "__main__":
    sys.exit(main())
