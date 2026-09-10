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

/* The separator this OS's own path APIs produce: '\\' on Windows, '/'
   elsewhere. `filez.join_path` and friends must build paths with it, since
   Python's `Path.__truediv__` -- the reference they match -- does. */
char platform_path_sep(void);
/* Whether `c` separates path components *on this OS*. Both '/' and '\\' do
   on Windows; only '/' does on POSIX, where '\\' is a legal filename
   character. */
bool platform_is_path_sep(char c);
/* The length of `path`'s drive prefix ("C:" and the like), always 0 off
   Windows. pathlib treats a component carrying one as a new root, exactly
   like a leading separator. */
size_t platform_drive_prefix_len(const char *path);

bool platform_path_exists(const char *path);
/* Both answer false for a path that does not exist, rather than failing --
   same as Python's Path.is_dir()/is_file(), which filez.is_dir/is_file
   mirror. A recursive directory walk needs these; `exists` alone cannot
   tell a file from a directory. */
bool platform_path_is_dir(const char *path);
bool platform_path_is_file(const char *path);

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

/* -- the running executable (NATIVE_PLAN.md N7) ---------------------- */

/* Absolute path of the running binary itself. Two callers need it and
   neither can use argv[0]: `funny` locates its toolchain bundle beside
   itself (argv[0] may be a bare name found on PATH), and the yeet stub
   reads its *own* file to find the payload appended to it (PLAN.md §5.4).
   False if the OS won't say, leaving `out` untouched. */
bool platform_executable_path(char *out, size_t out_len);

/* A path in the system temp directory that nothing else is using, for the
   bytecode `funny run <file.funny>` compiles to before running it. The
   file is created empty so the name can't be handed out twice; the caller
   removes it when done. */
bool platform_temp_file(const char *prefix, char *out, size_t out_len);

/* Marks a file executable (POSIX: adds the x bits the way `chmod +x`
   does). A no-op on Windows, where being runnable is decided by the
   extension rather than by a mode bit -- `funny yeet` calls it
   unconditionally rather than branching at the call site. */
bool platform_make_executable(const char *path);

/* -- console (NATIVE_PLAN.md N6, diagnostics) ------------------------ */

/* True when *stdout* is a terminal. Deliberately stdout and not stderr,
   even though diagnostics go to stderr: funnylang/errors.py's own
   `_use_color` checks `sys.stdout.isatty()`, and N6's acceptance is
   byte-identical stderr, so the colour decision has to be made the same
   (slightly odd) way. */
bool platform_stdout_is_tty(void);

/* Prepares the console for the §4.2 renderer's output: on Windows, sets
   the output code page to UTF-8 so the box-drawing and emoji don't get
   mangled by the ANSI code page, and enables VT processing so ANSI colour
   is interpreted rather than printed literally. A no-op everywhere else,
   where both are already true. Safe to call once at startup regardless of
   whether anything is later written. */
void platform_console_init(void);

/* -- networking (NATIVE_PLAN.md N5 task 5, `internet`) --------------- */

/* True when FUNNY_NO_NET=1 -- every internet.* function checks this
   itself (funnylang/stdlib/internet.py's own _net_disabled(), checked at
   the top of every function with per-function handling of what "disabled"
   means: most raise, is_it_up() just returns false), so it isn't baked
   into platform_http_request/platform_tcp_ping themselves. */
bool platform_net_disabled(void);

typedef struct {
    char *name;  /* malloc'd */
    char *value; /* malloc'd */
} PlatformHttpHeader;

typedef struct {
    bool ok;
    int status;
    char *body; /* malloc'd; may contain embedded NULs, use bodyLen */
    size_t bodyLen;
    PlatformHttpHeader *headers;
    int headerCount;
    /* Empty for every ordinary failure (the caller then raises the same
       generic "the internet said no" SkillIssue Python always raises).
       Non-empty only when a failure needs naming rather than collapsing:
       today that is exactly the "this machine has no TLS library" case,
       which NATIVE_PLAN.md §3.1 requires be "a clean SkillIssue naming
       the missing library, never a crash" -- a diagnostic Python's own
       urllib can never produce, so nothing differential-tests it. */
    char failReason[256];
} PlatformHttpResponse;

/* A full HTTP/1.1 request/response cycle -- `http://` over a raw socket,
   `https://` over the OS's own TLS (NATIVE_PLAN.md N5b §3.1: dlopen'd
   OpenSSL on Linux/BSD, WinHTTP on Windows, Security.framework on macOS;
   no bundled crypto, no third-party dependency, and certificate
   verification unconditionally on with no verify=false escape hatch).
   A machine with no TLS backend at all still builds and runs; `https://`
   there fails with `failReason` set rather than crashing or silently
   downgrading to plain HTTP.
   Follows up to 10 redirects (a Location header on 301/302/303/307/308;
   301/302/303 switch to GET and drop the body, 307/308 preserve both,
   matching ordinary browser/urllib redirect behavior; a relative
   Location is treated as a failure -- see NATIVE_PLAN.md's own §9 log
   for why). Decodes `Transfer-Encoding: chunked`, honors `Content-
   Length`, and falls back to read-until-EOF when neither header is
   present. `timeoutMs` bounds the connect phase and the overall
   remaining read budget together, not each independently.
   On any failure (network error, TLS handshake or certificate
   rejection, malformed response, too many redirects, an unknown scheme,
   a relative Location) returns ok=false with every other field zeroed --
   the caller raises the same generic "the internet said no" SkillIssue,
   exactly matching funnylang/stdlib/internet.py's own single
   _no_net_error() catch-all around every possible urllib/socket
   exception, unless `failReason` says otherwise. */
PlatformHttpResponse platform_http_request(const char *method, const char *url,
                                            const PlatformHttpHeader *headers, int headerCount,
                                            const char *body, size_t bodyLen, int timeoutMs);
void platform_http_response_free(PlatformHttpResponse *resp);

/* Raw TCP connect-and-time (no HTTP involved) for internet.ping(). */
bool platform_tcp_ping(const char *host, int port, int timeoutMs, double *outMs);

#endif /* FUNNY_PLATFORM_H */
