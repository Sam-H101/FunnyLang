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


# ---------------------------------------------------------------------------
# Bundling (`funny build`) and pak-aware loading (`funny run x.funnypak`)
# ---------------------------------------------------------------------------


class CanonicalSource:
    """A minimal stand-in for SourceFile when running from a bundle: no
    source text is embedded in a .funnypak, so error snippets can't show
    code, but line/col (from the bundled line table) and the module's
    canonical name still work everywhere `source.path` is read."""

    __slots__ = ("path",)

    def __init__(self, path: str):
        self.path = path

    def line_text(self, n: int) -> str:
        return ""

    def num_lines(self) -> int:
        return 0


def _canonical_name(abs_path: Path, entry_dir: Path) -> str:
    try:
        return abs_path.relative_to(entry_dir).as_posix()
    except ValueError:
        return abs_path.as_posix()


def build_bundle(entry_path: str) -> tuple[dict, str]:
    """Walks every file-based `gimme` reachable from `entry_path`, compiling
    each exactly once, keyed by its path relative to the entry file's own
    directory (POSIX-style) — the same key scheme `make_pak_module_loader`
    resolves against at runtime, with no filesystem access needed then."""
    from . import ast_nodes as A
    from .compiler import Compiler
    from .resolver import resolve_program

    entry_abs = Path(entry_path).resolve()
    entry_dir = entry_abs.parent
    resolver = ModuleResolver(vm=None)
    units: dict[str, object] = {}

    def visit(abs_path: Path, canonical: str) -> None:
        if canonical in units:
            return
        text = abs_path.read_text(encoding="utf-8")
        source = SourceFile(str(abs_path), text)
        program = parse_source(source)
        result = resolve_program(program, source)
        unit = Compiler(result, source).compile_program(program, canonical)
        units[canonical] = unit
        for stmt in program.statements:
            target = stmt.decl if isinstance(stmt, A.Export) else stmt
            if isinstance(target, A.Import) and not target.is_stdlib:
                child_abs = resolver.resolve(target.source, str(abs_path))
                visit(child_abs, _canonical_name(child_abs, entry_dir))

    entry_canonical = _canonical_name(entry_abs, entry_dir)
    visit(entry_abs, entry_canonical)
    return units, entry_canonical


def make_pak_module_loader(modules: dict, entry_canonical: str):
    """A VM module_loader for running a linked `.funnypak`: resolves a
    quoted `gimme` path relative to the *currently executing* bundled
    module's own canonical name (pure string/path logic — no filesystem
    involved, since the bundle is meant to be self-contained)."""
    import posixpath

    from .errors import ImportSkillIssue

    cache: dict[str, object] = {}

    def _loader(vm, path: str, mode: int):
        if mode == 2:
            from .stdlib import get_stdlib_module

            module = get_stdlib_module(path)
            if module is None:
                raise ImportSkillIssue(
                    f"no stdlib module named '{path}'.",
                    roast=f"can't find `{path}`. did you make it up?",
                )
            return module
        current = getattr(vm.source, "path", entry_canonical)
        current_dir = posixpath.dirname(current)
        target = posixpath.normpath(posixpath.join(current_dir, path)) if current_dir else posixpath.normpath(path)
        if target in cache:
            return cache[target]
        if target not in modules:
            raise ImportSkillIssue(
                f"'{path}' isn't in this bundle.",
                roast=f"can't find `{path}`. did you make it up?",
            )
        module_obj = vm.run_module(modules[target], CanonicalSource(target), target)
        cache[target] = module_obj
        return module_obj

    return _loader
