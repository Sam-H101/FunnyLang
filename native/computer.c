#include "computer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "platform.h"
#include "string.h"
#include "vm.h"

/* funnylang/stdlib/computer.py's own MUSHROOM_CLOUD raw string, verified
   byte-for-byte via a throwaway script that dumped it as an escaped C
   literal (build/n4/dump_mushroom_cloud.py) rather than hand-transcribed,
   since this needs to match the Python reference exactly for the
   differential suite. */
static const char *MUSHROOM_CLOUD =
    "\n"
    "                  _.-^^---....,,--\n"
    "              _--                  --_\n"
    "             <                        >)\n"
    "             |                         |\n"
    "              \\._                   _./\n"
    "                 ```--. . , ; .--'''\n"
    "                       | |   |\n"
    "                    .-=||  | |=-.\n"
    "                    `-=#$%&%$#=-'\n"
    "                       | ;  :|\n"
    "              _____.,-#%&$@%#&#~,._____\n";

static Value m_explode(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    fputs(MUSHROOM_CLOUD, vm->out);
    fputs("\n  \xF0\x9F\x8D\x84 KERNEL PANIC: user was cringe\n", vm->out);
    fputs("  computer.explode() called on purpose. couldn't be you.\n", vm->out);
    fputs("  it's over. exit code 69.\n", vm->out);
    /* ComputerExploded is an ordinary catchable FunnyError from the VM's
       own point of view (sketchy/my_bad catches it like anything else,
       unlike dip()'s uncatchable-everywhere SystemExit sentinel) -- it's
       only special at the top level: main.c maps an *uncaught* one to
       exit code 69 instead of the generic 1, matching funnylang/cli.py's
       own dedicated `except ComputerExploded` clause. */
    vm_throw_native(vm, "ComputerExploded", "computer.explode() was called.");
    return GHOST_VAL;
}

/* funnylang-no-python-runtime-dependency: the Python reference's own
   `flex()` prints a "python: {platform.python_version()}" line, which
   this native VM has no equivalent for -- it isn't running Python, and
   the shipped binary must not depend on Python being installed at all.
   AGENT CHOICE (logged in NATIVE_PLAN.md's own §9): that one line is
   replaced with a plain description of the native runtime instead of
   inventing a fake version number (no versioning scheme exists yet --
   that's N7's `--version` job). Every other line (OS/CPU/RAM) is a real
   platform.h query and matches the reference's own values on the same
   machine, though `flex()` as a whole is excluded from the byte-diffed
   differential suite specifically because of this one line -- see the
   module's own test file for how the rest gets verified instead. */
static Value m_flex(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    char sysname[128], release[128];
    platform_os_info(sysname, sizeof(sysname), release, sizeof(release));
    fprintf(vm->out, "OS: %s %s\n", sysname, release);
    fprintf(vm->out, "CPUs: %d\n", platform_cpu_count());
    fprintf(vm->out, "RAM: %llu bytes\n", (unsigned long long)platform_ram_bytes());
    fputs("runtime: FunnyLang native (C)\n", vm->out);
    fputs("your rig: mid\n", vm->out);
    return GHOST_VAL;
}

static Value m_ram(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)a;
    (void)argc;
    return INT_VAL((int64_t)platform_ram_bytes());
}

/* One line of stdin, or `ghost` at end of input.

   The builtin `ask()` returns "" for both an empty line and end of input,
   which a REPL cannot live with: Ctrl-D has to end the session and a blank
   line has to not. NATIVE_PLAN.md N8 task 5 named this as its prerequisite.
   Reads in chunks rather than assuming a line fits one buffer, so a pasted
   paragraph is not silently split. */
static Value m_readline(VM *vm, Value *a, int argc) {
    if (argc > 0 && !IS_GHOST(a[0])) {
        size_t promptLen;
        char *prompt = vm_value_to_display_len(vm, a[0], &promptLen);
        fwrite(prompt, 1, promptLen, vm->out);
        free(prompt);
        fflush(vm->out);
    }
    size_t cap = 256, len = 0;
    char *buf = (char *)malloc(cap);
    bool sawAny = false;
    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) break;
        sawAny = true;
        if (c == '\n') break;
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
        buf[len++] = (char)c;
    }
    if (!sawAny) {
        free(buf);
        return GHOST_VAL; /* end of input, distinct from an empty line */
    }
    while (len > 0 && buf[len - 1] == '\r') len--;
    Value result = OBJ_VAL(string_new(&vm->gc, buf, (uint32_t)len));
    free(buf);
    return result;
}

static Value m_yeet_to_void(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)a;
    (void)argc;
    return GHOST_VAL;
}

static Value m_beep(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    fputs("\a", vm->out);
    return GHOST_VAL;
}

static Value m_clear(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    fputs("\x1b[2J\x1b[H", vm->out);
    return GHOST_VAL;
}

static Value m_uptime(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)a;
    (void)argc;
    return FLOAT_VAL(platform_uptime_seconds());
}

static Value m_blue_screen(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    fputs("\x1b[44m\x1b[2J\x1b[H", vm->out);
    fputs("FUNNYLANG_FAULT_NOT_HANDLED\n", vm->out);
    fputs("\x1b[0m", vm->out);
    vm_throw_native_roast(vm, "SkillIssue", "FUNNYLANG_FAULT_NOT_HANDLED",
                           "computer.blue_screen() was called.");
    return GHOST_VAL;
}

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} ComputerEntry;

static const ComputerEntry COMPUTER_FUNCTIONS[] = {
    {"explode", m_explode, 0, 0},
    {"flex", m_flex, 0, 0},
    {"ram", m_ram, 0, 0},
    {"yeet_to_void", m_yeet_to_void, 0, 1},
    {"readline", m_readline, 0, 1},
    {"beep", m_beep, 0, 0},
    {"clear", m_clear, 0, 0},
    {"uptime", m_uptime, 0, 0},
    {"blue_screen", m_blue_screen, 0, 0},
};
#define COMPUTER_FUNCTIONS_COUNT (int)(sizeof(COMPUTER_FUNCTIONS) / sizeof(COMPUTER_FUNCTIONS[0]))

Value computer_build(VM *vm) {
    ObjGroupChat *members = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(members));
    for (int i = 0; i < COMPUTER_FUNCTIONS_COUNT; i++) {
        const ComputerEntry *e = &COMPUTER_FUNCTIONS[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    ObjString *moduleName = string_new(&vm->gc, "computer", 8);
    ObjModule *mod = module_new(&vm->gc, moduleName, OBJ_VAL(members));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(mod);
}
