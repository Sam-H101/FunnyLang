"""User-file module resolution and loading (PLAN.md §M7)."""
from __future__ import annotations

import os
from pathlib import Path

from .compiler import Compiler
from .errors import ImportSkillIssue
from .parser import parse_source
from .resolver import resolve_program
from .source import SourceFile

_VIRTUAL_PATH_PREFIX = "<"  # "<test>", "<script>", "<expr>", ... — not a real file


def _is_real_path(p: str | None) -> bool:
    return bool(p) and not p.startswith(_VIRTUAL_PATH_PREFIX)


class ModuleResolver:
    """Implements §3.8's resolution order for a quoted `gimme` path:
    1. relative to the importing file's directory
    2. relative to each FUNNYPATH entry (`;`-separated on Windows, `:` elsewhere)
    3. `./funny_modules/`, walking up from the importing file
    Caches loaded modules by resolved absolute path, and detects import
    cycles via a stack of paths currently being loaded."""

    def __init__(self, vm, funnypath: str | None = None):
        self.vm = vm
        self.funnypath = os.environ.get("FUNNYPATH", "") if funnypath is None else funnypath
        self.loaded: dict[str, object] = {}
        self.loading: list[str] = []

    def _path_sep(self) -> str:
        return ";" if os.name == "nt" else ":"

    def _base_dir(self, importing_file: str | None) -> Path:
        if _is_real_path(importing_file):
            return Path(importing_file).resolve().parent
        return Path.cwd()

    def _candidates(self, quoted_path: str, importing_file: str | None) -> list[Path]:
        candidates: list[Path] = []
        if _is_real_path(importing_file):
            candidates.append(Path(importing_file).parent / quoted_path)
        for entry in self.funnypath.split(self._path_sep()):
            if entry:
                candidates.append(Path(entry) / quoted_path)
        d = self._base_dir(importing_file)
        seen: set[Path] = set()
        while d not in seen:
            seen.add(d)
            candidates.append(d / "funny_modules" / quoted_path)
            if d.parent == d:
                break
            d = d.parent
        return candidates

    def resolve(self, quoted_path: str, importing_file: str | None) -> Path:
        for candidate in self._candidates(quoted_path, importing_file):
            if candidate.is_file():
                return candidate.resolve()
        raise ImportSkillIssue(
            f"can't find '{quoted_path}'.",
            roast=f"can't find `{quoted_path}`. did you make it up?",
        )

    def enter(self, real_path: str | None) -> None:
        """Registers a file as "currently loading" — called for the entry
        script too (VM.interpret), not just gimme'd modules, so a cycle that
        loops back through the entry file is caught the same way."""
        if _is_real_path(real_path):
            self.loading.append(str(Path(real_path).resolve()))

    def exit(self, real_path: str | None) -> None:
        if _is_real_path(real_path):
            key = str(Path(real_path).resolve())
            if key in self.loading:
                self.loading.remove(key)

    def load(self, quoted_path: str, importing_file: str | None):
        resolved = self.resolve(quoted_path, importing_file)
        key = str(resolved)
        if key in self.loaded:
            return self.loaded[key]
        if key in self.loading:
            cycle = self.loading[self.loading.index(key):] + [key]
            chain = " → ".join(Path(p).name for p in cycle)
            raise ImportSkillIssue(
                f"circular import: {chain}",
                roast=f"bro it's a whole loop: {chain}. did you make it up?",
            )
        self.loading.append(key)
        try:
            text = resolved.read_text(encoding="utf-8")
            source = SourceFile(str(resolved), text)
            program = parse_source(source)
            result = resolve_program(program, source)
            unit = Compiler(result, source).compile_program(program, str(resolved))
            module = self.vm.run_module(unit, source, str(resolved))
        finally:
            self.loading.pop()
        self.loaded[key] = module
        return module
