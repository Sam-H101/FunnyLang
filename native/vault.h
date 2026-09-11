/* native/vault.h -- RUNTIME_PLAN.md R3: `gimme vault`, the module for
 * storing a password and for keeping a value secret at rest.
 *
 * Every primitive here comes from the operating system's own crypto library
 * through `platform.h` (CNG, CommonCrypto, or the dlopen'd OpenSSL that
 * `https` already uses). Nothing in this repository implements a cipher or a
 * hash, deliberately: a hand-written AES is a liability, and every platform
 * ships a reviewed one.
 *
 * WHAT THIS IS FOR, and what it is not. `hash_password` is one-way: it exists
 * so that a stolen database is not a stolen list of passwords, and there is
 * no function here that turns one back. `seal` is two-way, and needs a key --
 * which the caller keeps, somewhere the sealed data is not. A key in the same
 * file as the data it seals is not encryption; it is a rearrangement. The
 * docs say so in bold, and the HTTPS example reads its key from a path given
 * on the command line rather than from its own data directory.
 *
 * The two string formats are self-describing, so a stored value can still be
 * read after the defaults change:
 *
 *     pbkdf2-sha256$600000$<salt base64>$<hash base64>
 *     v1$<nonce base64>$<ciphertext+tag base64>       (a yapstring, sealed)
 *     v1b$<nonce base64>$<ciphertext+tag base64>      (a blob, sealed)
 *
 * `check_password` reads the iteration count out of the stored string rather
 * than assuming today's default, so raising the default does not invalidate
 * anybody's existing password.
 */
#ifndef FUNNY_VAULT_H
#define FUNNY_VAULT_H

#include "value.h"
#include "vm.h"

struct VM;

Value vault_build(struct VM *vm);

#endif /* FUNNY_VAULT_H */
