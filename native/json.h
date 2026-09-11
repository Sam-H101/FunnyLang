/* native/json.h -- RUNTIME_PLAN.md R7: `gimme json`.
 *
 * JSON in both directions, ported from the one
 * `extensive_examples/web_server_https/json.funny` grew: a server reads it
 * from request bodies, from a worker's messages and from its own data files,
 * and every program that talks to anything else eventually needs it. Being in
 * the standard library means one implementation rather than one per program,
 * and a C one means a request body is parsed without the parser itself being
 * a performance question.
 *
 * The parser assumes hostile input, because a request body is written by
 * whoever is on the other end of the socket: it never nests deeper than
 * MAX_DEPTH, never half-builds a value it then hands back, and reports every
 * failure as one `SkillIssue` naming the character it gave up at.
 *
 * The mapping, which is the whole specification:
 *
 *     null            ghost            an integer   a numba, still an integer
 *     true / false    boolski          2.5, 1e3     a numba, a float
 *     "text"          yapstring        [...]        stash
 *     {...}           groupchat, in the order the keys appeared
 *
 * Going out, a `blob` spills as base64 text -- JSON has no bytes, and the
 * alternative is refusing to write a value somebody put in a record. It does
 * not come back as a blob: nothing in the text says it was one.
 */
#ifndef FUNNY_JSON_H
#define FUNNY_JSON_H

#include "value.h"
#include "vm.h"

struct VM;

Value json_build(struct VM *vm);

#endif /* FUNNY_JSON_H */
