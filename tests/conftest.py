"""Shared pytest fixtures/helpers for the FunnyLang test suite."""
from __future__ import annotations


def pytest_sessionfinish(session, exitstatus):
    # An empty test suite (M0, before M1 lands) should not fail CI.
    if exitstatus == 5:  # NO_TESTS_COLLECTED
        session.exitstatus = 0
