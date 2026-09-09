/* native/platform.h -- NATIVE_PLAN.md N5 task 2 (`filez`) / ARCHITECTURE.md
 * "The platform boundary": platform.c is the only file in native/ allowed
 * an #ifdef _WIN32 (or any other platform conditional). Filesystem access
 * goes through the functions declared here; nothing above this layer
 * (filez.c included) touches dirent.h, sys/stat.h, unistd.h, or any other
 * OS header directly.
 *
 * Only the POSIX implementation exists so far (build.sh only targets
 * Linux/macOS today) -- the same deferral native/main.c already applies
 * to its own Unicode banner (real Win32 filesystem calls land whenever a
 * Windows build actually exists to compile and test them against, not
 * before; see ARCHITECTURE.md's own note on why main.c's banner is
 * plain-ASCII for now).
 *
 * Every function below returns true on success. On failure they return
 * false and copy a human-readable reason into errbuf (sized errbuf_len)
 * -- the same strerror() text an OSError.strerror would carry on the
 * same OS, since funnylang/stdlib/filez.py's own error wrapping surfaces
 * that exact string and the differential suite diffs against it byte for
 * byte.
 */
#ifndef FUNNY_PLATFORM_H
#define FUNNY_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Reads the whole file at `path` into a freshly malloc'd buffer (caller
   frees it). *out_len excludes the NUL terminator platform.c always adds
   for convenience, though callers dealing with arbitrary bytes (filez's
   own read_bytes) should only trust the first *out_len bytes. */
bool platform_read_file(const char *path, unsigned char **out_data, size_t *out_len, char *errbuf,
                         size_t errbuf_len);

bool platform_write_file(const char *path, const unsigned char *data, size_t len, char *errbuf,
                          size_t errbuf_len);
bool platform_append_file(const char *path, const unsigned char *data, size_t len, char *errbuf,
                           size_t errbuf_len);

bool platform_path_exists(const char *path);

/* Removes a file, or an empty directory (dispatched the same way
   filez.py's own `obliterate` picks between unlink() and rmdir()). */
bool platform_remove_path(const char *path, char *errbuf, size_t errbuf_len);

/* `mkdir -p` semantics: creates every missing intermediate component,
   succeeds silently if `path` already exists as a directory (matches
   Python's Path.mkdir(parents=True, exist_ok=True)). */
bool platform_mkdir_p(const char *path, char *errbuf, size_t errbuf_len);

/* *out_names is a malloc'd array of malloc'd strings (caller frees each
   string, then the array), sorted byte-wise -- equal to Unicode-codepoint
   order for well-formed UTF-8, which is what Python's own sorted() gives
   filez.py's list_dir. "." and ".." are never included. */
bool platform_list_dir(const char *path, char ***out_names, size_t *out_count, char *errbuf,
                        size_t errbuf_len);

/* Absolute, normalized form of `path` (relative paths resolved against
   the current working directory). Resolves symlinks when the path fully
   exists, same as realpath(3)/Python's Path.resolve(); falls back to
   plain lexical normalization (collapsing "." and "..") for a path that
   doesn't exist yet, since Path.resolve()'s default strict=False doesn't
   require existence either. */
bool platform_abs_path(const char *path, char *out, size_t out_len, char *errbuf, size_t errbuf_len);

/* -- timing (NATIVE_PLAN.md N5 task 2, `clock`) --------------------- */

/* Wall-clock time, seconds since the Unix epoch -- matches Python's
   time.time(). */
double platform_now_seconds(void);
/* A monotonic clock unaffected by wall-clock adjustments -- matches
   Python's time.perf_counter(); only ever used for measuring elapsed
   time between two calls, never compared against platform_now_seconds's
   own epoch. */
double platform_monotonic_seconds(void);
/* Blocks the calling thread for `seconds` (no-op for <= 0) -- matches
   Python's time.sleep(). */
void platform_sleep_seconds(double seconds);
/* Formats the current local time with a strftime(3)-style `fmt` into
   `out` (out_len bytes); returns the byte count written, same convention
   as strftime itself (0 for either an empty format or a buffer too
   small -- out_len is generous enough in every caller that the two never
   need distinguishing). Matches Python's time.strftime(fmt). */
size_t platform_strftime_now(const char *fmt, char *out, size_t out_len);

/* -- system info (NATIVE_PLAN.md N5 task 2, `computer`) -------------- */

/* Total physical RAM in bytes -- matches Python's own
   sysconf(SC_PAGE_SIZE) * sysconf(SC_PHYS_PAGES); 0 if either sysconf
   query is unsupported, same graceful fallback funnylang/stdlib/
   computer.py's own _ram_bytes() uses. */
uint64_t platform_ram_bytes(void);
/* Seconds since boot -- matches Python's own /proc/uptime read on
   Linux; falls back to elapsed process time (platform_now_seconds()
   minus a start time captured on first call) if /proc/uptime can't be
   read, the same fallback computer.py's own _uptime_seconds() uses. */
double platform_uptime_seconds(void);
/* uname(2)'s sysname/release fields (e.g. "Linux"/"5.15.0-91-generic")
   -- matches Python's platform.system()/platform.release() on POSIX,
   which read the exact same fields. Either buffer left as "" on
   failure. */
void platform_os_info(char *sysname_out, size_t sysname_len, char *release_out, size_t release_len);
/* Number of logical CPUs -- matches Python's os.cpu_count(); 0 if
   unknown. */
int platform_cpu_count(void);

#endif /* FUNNY_PLATFORM_H */
