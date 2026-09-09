from __future__ import annotations

import io

import pytest

from conftest import compile_prog, make_source
from funnylang.errors import ImportSkillIssue, WhoDis
from funnylang.stdlib import install_stdlib
from funnylang.vm import VM


def _run_file(path, monkeypatch_env=None) -> str:
    src = open(path, encoding="utf-8").read()
    source = make_source(src, str(path))
    unit = compile_prog(src, str(path))
    vm = VM(stdout=io.StringIO())
    install_stdlib(vm)
    vm.interpret(unit, source)
    return vm.stdout.getvalue()


def _write(tmp_path, name, content):
    p = tmp_path / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content, encoding="utf-8")
    return p


def test_examples_modules_main_runs():
    out = _run_file("examples/modules/main.funny")
    assert out.splitlines() == ["42", "4.0", "20", "6.28318", "QUIET"]


def test_basic_import_binds_module_object(tmp_path):
    _write(tmp_path, "lib.funny", "flex bet f() { bounce 1 }\n")
    _write(tmp_path, "main.funny", 'gimme "lib.funny"\nyap lib.f()\n')
    assert _run_file(tmp_path / "main.funny") == "1\n"

    _write(tmp_path, "lib.funny", "flex bet f() { bounce 1 }\n")
    _write(tmp_path, "main.funny", 'gimme "lib.funny"\nyap lib.f()\n')
    assert _run_file(tmp_path / "main.funny") == "1\n"


def test_import_with_alias(tmp_path):
    _write(tmp_path, "lib.funny", "flex bet f() { bounce 99 }\n")
    _write(tmp_path, "main.funny", 'gimme "lib.funny" as thing\nyap thing.f()\n')
    assert _run_file(tmp_path / "main.funny") == "99\n"


def test_named_import(tmp_path):
    _write(tmp_path, "lib.funny", 'flex deadass X = 5\nflex bet f() { bounce X * 2 }\n')
    _write(tmp_path, "main.funny", 'gimme { X, f } from "lib.funny"\nyap X\nyap f()\n')
    assert _run_file(tmp_path / "main.funny") == "5\n10\n"


def test_non_exported_name_raises_with_exact_roast(tmp_path):
    _write(tmp_path, "lib.funny", "bet secret() { bounce 1 }\n")
    _write(tmp_path, "main.funny", 'gimme { secret } from "lib.funny"\n')
    with pytest.raises(WhoDis) as excinfo:
        _run_file(tmp_path / "main.funny")
    assert "isn't flexed. it's shy." in excinfo.value.roast


def test_non_flexed_name_not_accessible_via_module_object(tmp_path):
    _write(tmp_path, "lib.funny", "bet secret() { bounce 1 }\nflex bet pub() { bounce 2 }\n")
    _write(tmp_path, "main.funny", 'gimme "lib.funny"\nyap lib.secret()\n')
    with pytest.raises(WhoDis):
        _run_file(tmp_path / "main.funny")


def test_missing_file_raises_import_skill_issue(tmp_path):
    _write(tmp_path, "main.funny", 'gimme "does_not_exist.funny"\n')
    with pytest.raises(ImportSkillIssue):
        _run_file(tmp_path / "main.funny")


def test_circular_import_lists_the_cycle(tmp_path):
    _write(tmp_path, "a.funny", 'gimme "b.funny"\n')
    _write(tmp_path, "b.funny", 'gimme "a.funny"\n')
    with pytest.raises(ImportSkillIssue) as excinfo:
        _run_file(tmp_path / "a.funny")
    assert "a.funny" in excinfo.value.message
    assert "b.funny" in excinfo.value.message
    assert "→" in excinfo.value.message


def test_self_import_is_a_cycle_of_one(tmp_path):
    _write(tmp_path, "a.funny", 'gimme "a.funny"\n')
    with pytest.raises(ImportSkillIssue):
        _run_file(tmp_path / "a.funny")


def test_module_executes_only_once(tmp_path):
    _write(tmp_path, "lib.funny", 'yap "loaded"\nflex deadass X = 1\n')
    _write(tmp_path, "main.funny", 'gimme "lib.funny"\ngimme { X } from "lib.funny"\nyap X\n')
    out = _run_file(tmp_path / "main.funny")
    assert out == "loaded\n1\n"  # "loaded" only printed once, not twice


def test_stdlib_bare_import(tmp_path):
    _write(tmp_path, "main.funny", "gimme mafs\nyap mafs.sqrt(16)\n")
    assert _run_file(tmp_path / "main.funny") == "4.0\n"


def test_stdlib_import_with_alias(tmp_path):
    _write(tmp_path, "main.funny", "gimme rizz as luck\nyap what_is_it(luck.roll(1,6))\n")
    assert _run_file(tmp_path / "main.funny") == "numba\n"


def test_funnypath_resolution(tmp_path, monkeypatch):
    libdir = tmp_path / "libs"
    libdir.mkdir()
    (libdir / "helper.funny").write_text("flex bet f() { bounce 7 }\n", encoding="utf-8")
    maindir = tmp_path / "proj"
    maindir.mkdir()
    _write(maindir, "main.funny", 'gimme "helper.funny"\nyap helper.f()\n')
    monkeypatch.setenv("FUNNYPATH", str(libdir))
    assert _run_file(maindir / "main.funny") == "7\n"


def test_relative_to_importing_file_directory(tmp_path):
    sub = tmp_path / "sub"
    sub.mkdir()
    _write(sub, "helper.funny", "flex bet f() { bounce 3 }\n")
    _write(sub, "main.funny", 'gimme "helper.funny"\nyap helper.f()\n')
    assert _run_file(sub / "main.funny") == "3\n"


def test_funny_modules_directory_walk_up(tmp_path):
    (tmp_path / "funny_modules").mkdir()
    (tmp_path / "funny_modules" / "shared.funny").write_text(
        "flex bet f() { bounce 11 }\n", encoding="utf-8"
    )
    deep = tmp_path / "a" / "b" / "c"
    deep.mkdir(parents=True)
    _write(deep, "main.funny", 'gimme "shared.funny"\nyap shared.f()\n')
    assert _run_file(deep / "main.funny") == "11\n"


def test_module_globals_are_isolated(tmp_path):
    _write(tmp_path, "lib.funny", "yo secret_local = 42\nflex bet get_it() { bounce secret_local }\n")
    _write(tmp_path, "main.funny", 'gimme "lib.funny"\nyap lib.get_it()\nyap what_is_it(lib)\n')
    out = _run_file(tmp_path / "main.funny")
    assert out == "42\nmodule\n"
    # the importer's own globals must not see the module's private locals
    _write(tmp_path, "main2.funny", 'gimme "lib.funny"\nyap secret_local\n')
    with pytest.raises(WhoDis):
        _run_file(tmp_path / "main2.funny")
