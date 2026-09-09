/* native/internet.h -- NATIVE_PLAN.md N5 task 5, ported from
 * funnylang/stdlib/internet.py: real HTTP/1.1 over raw sockets (plain
 * HTTP only -- HTTPS is N5b's job, over OS-native TLS via platform.c).
 * `FUNNY_NO_NET=1` makes every network call raise a clean SkillIssue
 * instead of touching the network, exactly like the Python reference.
 */
#ifndef FUNNY_INTERNET_H
#define FUNNY_INTERNET_H

#include "value.h"

struct VM;

Value internet_build(struct VM *vm);

#endif /* FUNNY_INTERNET_H */
