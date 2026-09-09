from __future__ import annotations

from conftest import run_funny


def _r(src: str) -> str:
    return run_funny(src)


# -- builtins ---------------------------------------------------------------


def test_how_thicc_stash():
    assert _r("yap how_thicc([1,2,3])\n") == "3\n"


def test_how_thicc_string():
    assert _r('yap how_thicc("hello")\n') == "5\n"


def test_how_thicc_groupchat():
    assert _r('yap how_thicc({"a": 1, "b": 2})\n') == "2\n"


def test_what_is_it():
    assert _r("yap what_is_it(5)\n") == "numba\n"
    assert _r('yap what_is_it("x")\n') == "yapstring\n"
    assert _r("yap what_is_it(fax)\n") == "boolski\n"
    assert _r("yap what_is_it([1])\n") == "stash\n"
    assert _r("yap what_is_it(ghost)\n") == "ghost\n"


def test_to_yap():
    assert _r("yap to_yap(42)\n") == "42\n"


def test_to_numba():
    assert _r('yap to_numba("42")\n') == "42\n"
    assert _r('yap to_numba("3.5")\n') == "3.5\n"


def test_to_int():
    assert _r("yap to_int(3.9)\n") == "3\n"


def test_sheesh_prints_repr_and_returns_value():
    assert _r('yap sheesh("x") + "y"\n') == '"x"\nxy\n'


def test_no_cap_passes():
    assert _r("no_cap(fax)\nyap \"ok\"\n") == "ok\n"


def test_no_cap_fails():
    from funnylang.errors import SkillIssue
    from conftest import expect_error
    err = expect_error('no_cap(cap, "custom message")\n')
    assert isinstance(err, SkillIssue)
    assert err.message == "custom message"


def test_dip_exits():
    import pytest
    with pytest.raises(SystemExit):
        _r("dip(3)\n")


def test_the_args_default_empty():
    assert _r("yap the_args()\n") == "[]\n"


def test_combo_composes_left_to_right():
    src = "bet inc(x) { bounce x + 1 }\nbet double(x) { bounce x * 2 }\nyap combo(inc, double)(5)\n"
    assert _r(src) == "12\n"


def test_identity():
    assert _r("yap identity(7)\n") == "7\n"


def test_range_stash_two_arg():
    assert _r("yap range_stash(0, 5)\n") == "[0, 1, 2, 3, 4]\n"


def test_range_stash_one_arg():
    assert _r("yap range_stash(3)\n") == "[0, 1, 2]\n"


def test_range_stash_with_step():
    assert _r("yap range_stash(10, 0, -2)\n") == "[10, 8, 6, 4, 2]\n"


def test_zip_em():
    assert _r("yap zip_em([1,2], [3,4])\n") == "[[1, 3], [2, 4]]\n"


def test_enumerate_em():
    assert _r('yap enumerate_em(["a", "b"])\n') == '[[0, "a"], [1, "b"]]\n'


def test_deep_clone_is_independent():
    src = "yo a = [[1,2]]\nyo b = deep_clone(a)\nb[0][0] = 99\nyap a\nyap b\n"
    assert _r(src) == "[[1, 2]]\n[[99, 2]]\n"


# -- mafs ---------------------------------------------------------------


def test_mafs_sqrt():
    assert _r("gimme mafs\nyap mafs.sqrt(16)\n") == "4.0\n"


def test_mafs_abs_floor_ceil():
    assert _r("gimme mafs\nyap mafs.abs(-5)\nyap mafs.floor(3.7)\nyap mafs.ceil(3.2)\n") == "5\n3\n4\n"


def test_mafs_round():
    assert _r("gimme mafs\nyap mafs.round(3.456, 2)\n") == "3.46\n"


def test_mafs_min_max():
    assert _r("gimme mafs\nyap mafs.min(3, 1, 2)\nyap mafs.max(3, 1, 2)\n") == "1\n3\n"


def test_mafs_pow():
    assert _r("gimme mafs\nyap mafs.pow(2, 10)\n") == "1024\n"


def test_mafs_log_family():
    assert _r("gimme mafs\nyap mafs.log2(8)\nyap mafs.log10(1000)\n") == "3.0\n3.0\n"


def test_mafs_trig():
    assert _r("gimme mafs\nyap mafs.sin(0)\nyap mafs.cos(0)\n") == "0.0\n1.0\n"


def test_mafs_clamp():
    assert _r("gimme mafs\nyap mafs.clamp(15, 0, 10)\nyap mafs.clamp(-5, 0, 10)\n") == "10\n0\n"


def test_mafs_sign():
    assert _r("gimme mafs\nyap mafs.sign(-5)\nyap mafs.sign(5)\nyap mafs.sign(0)\n") == "-1\n1\n0\n"


def test_mafs_gcd_lcm():
    assert _r("gimme mafs\nyap mafs.gcd(12, 18)\nyap mafs.lcm(4, 6)\n") == "6\n12\n"


def test_mafs_is_prime():
    assert _r("gimme mafs\nyap mafs.is_prime(17)\nyap mafs.is_prime(18)\n") == "fax\ncap\n"


def test_mafs_factorial():
    assert _r("gimme mafs\nyap mafs.factorial(5)\n") == "120\n"


def test_mafs_constants():
    out = _r("gimme mafs\nyap mafs.skibidi_pi\nyap mafs.e\n")
    assert out.startswith("3.14159")


def test_numba_instance_methods():
    src = "yap 3.14159.round(2)\nyap (-5).abs()\nyap 3.9.floor()\nyap 3.1.ceil()\nyap 4.0.is_whole()\n"
    assert _r(src) == "3.14\n5\n3\n4\nfax\n"


# -- yapper ---------------------------------------------------------------


def test_yapper_split_join():
    assert _r('gimme yapper\nyap yapper.split("a,b,c", ",")\n') == '["a", "b", "c"]\n'
    assert _r('gimme yapper\nyap yapper.join("-", ["a", "b"])\n') == "a-b\n"


def test_yapper_scream_whisper():
    assert _r('gimme yapper\nyap yapper.SCREAM("hi")\nyap yapper.whisper("HI")\n') == "HI\nhi\n"


def test_yapper_trim_variants():
    assert _r('gimme yapper\nyap yapper.trim("  hi  ")\n') == "hi\n"


def test_yapper_replace_contains():
    assert _r('gimme yapper\nyap yapper.replace("hello", "l", "L")\n') == "heLLo\n"
    assert _r('gimme yapper\nyap yapper.contains("hello", "ell")\n') == "fax\n"


def test_yapper_starts_ends_with():
    assert _r('gimme yapper\nyap yapper.starts_with("hello", "he")\n') == "fax\n"
    assert _r('gimme yapper\nyap yapper.ends_with("hello", "lo")\n') == "fax\n"


def test_yapper_sarcasm_case():
    assert _r('gimme yapper\nyap yapper.sarcasm_case("hello")\n') == "hElLo\n"


def test_yapper_title_case():
    assert _r('gimme yapper\nyap yapper.title_case("hello world")\n') == "Hello World\n"


def test_yapper_is_numba():
    assert _r('gimme yapper\nyap yapper.is_numba("3.5")\nyap yapper.is_numba("abc")\n') == "fax\ncap\n"


def test_yapper_pad():
    assert _r('gimme yapper\nyap yapper.pad_left("5", 3, "0")\nyap yapper.pad_right("5", 3, "0")\n') == "005\n500\n"


def test_yapper_ord_chr():
    assert _r('gimme yapper\nyap yapper.ord_of("A")\nyap yapper.chr_of(65)\n') == "65\nA\n"


def test_yapper_words_lines():
    assert _r('gimme yapper\nyap yapper.words("a b c")\n') == '["a", "b", "c"]\n'


def test_string_instance_methods():
    src = 'yap "hello".at(1)\nyap "hello".code_at(0)\nyap "hello".pad_left(7, "*")\n'
    assert _r(src) == "e\n104\n**hello\n"


# -- stash ---------------------------------------------------------------


def test_stash_sort_by():
    assert _r("gimme stash\nyap stash.sort_by([3,1,2], lowkey (x) => -x)\n") == "[3, 2, 1]\n"


def test_stash_group_by():
    src = "gimme stash\nyap stash.group_by([1,2,3,4], lowkey (x) => x % 2)\n"
    assert _r(src) == '{1: [1, 3], 0: [2, 4]}\n'


def test_stash_unique():
    assert _r("gimme stash\nyap stash.unique([1,2,2,3,1])\n") == "[1, 2, 3]\n"


def test_stash_flatten():
    assert _r("gimme stash\nyap stash.flatten([1, [2, 3], [4, [5]]])\n") == "[1, 2, 3, 4, 5]\n"


def test_stash_chunk():
    assert _r("gimme stash\nyap stash.chunk([1,2,3,4,5], 2)\n") == "[[1, 2], [3, 4], [5]]\n"


def test_stash_sum_up():
    assert _r("gimme stash\nyap stash.sum_up([1,2,3,4])\n") == "10\n"


def test_stash_free_function_forms():
    assert _r("gimme stash\nyap stash.how_thicc([1,2,3])\n") == "3\n"
    assert _r("gimme stash\nyo a = [1,2]\nstash.yeet_in(a, 3)\nyap a\n") == "[1, 2, 3]\n"


def test_stash_method_any_all():
    src = "yap [1,2,3].any(lowkey (x) => x > 2)\nyap [1,2,3].all(lowkey (x) => x > 0)\n"
    assert _r(src) == "fax\nfax\n"


def test_stash_method_first_last():
    assert _r("yap [1,2,3].first()\nyap [1,2,3].last()\n") == "1\n3\n"


def test_stash_method_clone_independent():
    src = "yo a = [1,2]\nyo b = a.clone()\nb.yeet_in(3)\nyap a\nyap b\n"
    assert _r(src) == "[1, 2]\n[1, 2, 3]\n"


# -- groupchat ---------------------------------------------------------------


def test_groupchat_invert():
    assert _r('gimme groupchat\nyap groupchat.invert({"a": 1, "b": 2})\n') == '{1: "a", 2: "b"}\n'


def test_groupchat_from_pairs():
    assert _r('gimme groupchat\nyap groupchat.from_pairs([["a", 1], ["b", 2]])\n') == '{"a": 1, "b": 2}\n'


def test_groupchat_method_forms():
    src = 'yo m = {"a": 1}\nyap m.get("a")\nyap m.get("z", 99)\nyap m.pairs()\n'
    assert _r(src) == '1\n99\n[["a", 1]]\n'


# -- rizz ---------------------------------------------------------------


def test_rizz_roll_in_range():
    src = "gimme rizz\nrizz.seed(42)\nyo r = rizz.roll(1, 6)\nyap r >= 1 && r <= 6\n"
    assert _r(src) == "fax\n"


def test_rizz_pick_from_stash():
    src = 'gimme rizz\nrizz.seed(1)\nyo x = rizz.pick(["a","b","c"])\nyap x same_energy "a" || x same_energy "b" || x same_energy "c"\n'
    assert _r(src) == "fax\n"


def test_rizz_coinflip_is_boolean():
    src = "gimme rizz\nrizz.seed(1)\nyo x = rizz.coinflip()\nyap what_is_it(x)\n"
    assert _r(src) == "boolski\n"


def test_rizz_gamble_zero_never():
    assert _r("gimme rizz\nyap rizz.gamble(0)\n") == "cap\n"


def test_rizz_gamble_one_always():
    assert _r("gimme rizz\nyap rizz.gamble(1)\n") == "fax\n"


def test_rizz_uuid_format():
    out = _r("gimme rizz\nyap how_thicc(rizz.uuid())\n")
    assert out.strip() == "36"


# -- filez ---------------------------------------------------------------


def test_filez_roundtrip(tmp_path):
    p = str(tmp_path / "out.txt").replace("\\", "/")
    src = f'gimme filez\nfilez.yeet_out("{p}", "hello")\nyap filez.slurp("{p}")\nyap filez.exists("{p}")\n'
    assert _r(src) == "hello\nfax\n"


def test_filez_append(tmp_path):
    p = str(tmp_path / "out.txt").replace("\\", "/")
    src = f'gimme filez\nfilez.yeet_out("{p}", "a")\nfilez.append_to("{p}", "b")\nyap filez.slurp("{p}")\n'
    assert _r(src) == "ab\n"


def test_filez_path_helpers():
    src = 'gimme filez\nyap filez.base_of("a/b/c.txt")\nyap filez.ext_of("a/b/c.txt")\n'
    assert _r(src) == "c.txt\n.txt\n"


def test_filez_not_exists():
    assert _r('gimme filez\nyap filez.exists("definitely_missing_xyz.txt")\n') == "cap\n"


# -- clock ---------------------------------------------------------------


def test_clock_now_is_numeric():
    out = _r("gimme clock\nyap what_is_it(clock.now())\n")
    assert out == "numba\n"


def test_clock_stopwatch_returns_callable():
    out = _r("gimme clock\nyo sw = clock.stopwatch()\nyap what_is_it(sw())\n")
    assert out == "numba\n"


# -- sus ---------------------------------------------------------------


def test_sus_type_of():
    assert _r('gimme sus\nyap sus.type_of("x")\n') == "yapstring\n"


def test_sus_is_a():
    assert _r('gimme sus\nyap sus.is_a(5, "numba")\n') == "fax\n"


def test_sus_dump_returns_value():
    assert _r("gimme sus\nyap sus.dump(5) + 1\n") == "5\n6\n"


# -- computer (no exit-triggering calls here; those are in test_errors.py) --


def test_computer_yeet_to_void():
    assert _r("gimme computer\nyap computer.yeet_to_void(123)\n") == "ghost\n"


def test_computer_ram_is_numeric():
    out = _r("gimme computer\nyap what_is_it(computer.ram())\n")
    assert out == "numba\n"


# -- internet (network disabled in tests) ---------------------------------


def test_internet_disabled_raises_clean_skill_issue(monkeypatch):
    monkeypatch.setenv("FUNNY_NO_NET", "1")
    from funnylang.errors import SkillIssue
    from conftest import expect_error
    err = expect_error('gimme internet\ninternet.go_brrrr("https://example.com")\n')
    assert isinstance(err, SkillIssue)


def test_internet_is_it_up_false_when_disabled(monkeypatch):
    monkeypatch.setenv("FUNNY_NO_NET", "1")
    assert _r('gimme internet\nyap internet.is_it_up("https://example.com")\n') == "cap\n"
