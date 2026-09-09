/* native/main.c -- NATIVE_PLAN.md N0 task 2: proves the C toolchain builds
 * and runs, unmodified, on Windows/Linux/macOS. Nothing here is the real CLI
 * yet (that's N7) -- it exists only so build.sh/build.bat have something
 * true to build and CI has something true to run.
 *
 * Deliberately plain ASCII: the real banner (native/../funnylang/cli.py's
 * BANNER) is full box-drawing Unicode, which needs UTF-8 console setup on
 * Windows (SetConsoleOutputCP) before it's safe to print. That setup is
 * platform.c's job once it exists (NATIVE_PLAN.md §4: "the ONLY file with
 * #ifdef _WIN32") -- main.c doesn't get its own ad hoc ifdef for it.
 */

#include <stdio.h>

int main(void) {
    puts("FunnyLang (native) -- it compiles. somehow.");
    puts("N0: toolchain smoke test. The real CLI lands in N7.");
    return 0;
}
