"""Dev utility (not run by pytest): compiles one .funny file straight to
.funnyc via the Python toolchain, for manually driving the native VM by
hand outside the differential test harness -- e.g. under a sanitizer build
or a debugger, which test_n2_differential.py's own tmp_path fixtures make
inconvenient to reach into.

Usage: python3 tests/native/compile_one.py source.funny out.funnyc
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from funnylang.compiler import Compiler
from funnylang.parser import parse_source
from funnylang.resolver import resolve_program
from funnylang.serializer import dump_funnyc
from funnylang.source import SourceFile

src_path, out_path = sys.argv[1], sys.argv[2]
text = open(src_path, encoding="utf-8").read()
source = SourceFile(src_path, text)
program = parse_source(source)
result = resolve_program(program, source)
unit = Compiler(result, source).compile_program(program, src_path)
open(out_path, "wb").write(dump_funnyc(unit))
