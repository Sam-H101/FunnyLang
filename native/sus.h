/* native/sus.h -- NATIVE_PLAN.md N5 task 2: `sus`, ported from
 * funnylang/stdlib/sus.py (reflection / debug helpers).
 */
#ifndef FUNNY_SUS_H
#define FUNNY_SUS_H

#include <stddef.h>
#include <stdint.h>

#include "value.h"

struct VM;

Value sus_build(struct VM *vm);

/* Hands the runtime the embedded toolchain bundle, so `sus.toolchain()` can
   return it. Registered by the caller rather than referenced directly,
   because the two entry points differ on exactly this: `main.c` links
   `toolchain_blob.c` and calls this; `stub_main.c` links neither, since a
   `yeet`ed executable carries a program and no compiler. A direct reference
   from sus.c would break the stub's link.

   Borrowed, never freed -- the only caller passes a `const` array in
   read-only memory that outlives the process. `sus.toolchain()` answers
   `ghost` until this is called, which is the honest answer in the stub. */
void sus_set_toolchain(const uint8_t *bytes, size_t len);

/* The same bytes, for a caller in C rather than in FunnyLang: `interns`
   compiles a worker's `.funny` source by running the toolchain, the way the
   command line does. NULL (and *outLen 0) when none was registered, which is
   the stub's situation. */
const uint8_t *sus_get_toolchain(size_t *outLen);

#endif /* FUNNY_SUS_H */
