@echo off
setlocal enabledelayedexpansion
rem NATIVE_PLAN.md N0 task 1: one compiler invocation, no build system.
rem
rem Usage:
rem   build.bat          release build -> funny.exe
rem   build.bat debug    /fsanitize=address /DFUNNY_DEBUG -> funny.exe
rem
rem Requires cl.exe on PATH -- run from a "Developer Command Prompt for VS",
rem or call vcvarsall.bat first. MSVC has no UBSan equivalent, so the debug
rem build only gets ASan (build.sh's debug mode gets both, via gcc/clang).

set SRCS=
for %%f in (native\*.c) do set SRCS=!SRCS! %%f
if exist native\stdlib (
    for %%f in (native\stdlib\*.c) do set SRCS=!SRCS! %%f
)

if "%SRCS%"=="" (
    echo build.bat: no sources found under native\ 1>&2
    exit /b 1
)

rem /D_CRT_SECURE_NO_WARNINGS: MSVC's CRT flags plain-C11 getenv()/fopen()
rem (gc.c, main.c) as C4996 "deprecated, use _dupenv_s/fopen_s instead" --
rem real under /W4 /WX, since both calls are used correctly here (getenv
rem once at startup, fopen with an explicit "rb" mode, no attacker-controlled
rem format string). This is the standard, portable way to keep those calls
rem plain C11 rather than forking them onto MSVC-only _s variants -- it only
rem suppresses that one warning category, nothing else /W4 would still catch.
if "%1"=="debug" (
    set FLAGS=/std:c11 /Zi /Od /W4 /WX /D_CRT_SECURE_NO_WARNINGS /fsanitize=address /DFUNNY_DEBUG
) else (
    set FLAGS=/std:c11 /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS
)

echo + cl %FLAGS% /Fe:funny.exe %SRCS%
cl %FLAGS% /Fe:funny.exe %SRCS%
