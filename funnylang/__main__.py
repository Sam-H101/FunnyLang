import sys

# Absolute import, deliberately: when PyInstaller freezes this file directly
# (ci.yml's release job points PyInstaller at funnylang/__main__.py by path),
# its bootloader runs it as a bare top-level script with no package context,
# so `from .cli import main` fails with "attempted relative import with no
# known parent package" the moment it's frozen -- `funnylang` itself is still
# on sys.path inside the bundle, so the absolute form resolves. Same fix
# stub_main.py already documents and uses for the same reason.
from funnylang.cli import main

sys.exit(main())
