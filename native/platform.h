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

/* -- listening sockets, for `internet.open_shop` --------------------------
 *
 * Everything above is a *client*: it dials out. These are the other
 * direction -- bind a port, wait for somebody to dial in, read what they
 * say, say something back.
 *
 * Handles are `int64_t` rather than a struct, because they cross into
 * FunnyLang as a `numba` and stay out of the collector entirely (the same
 * reasoning `sus`'s REPL sessions and `interns`'s workers already record).
 * They are wide enough for a Win32 `SOCKET`, which is a `UINT_PTR` and does
 * not fit an `int` on a 64-bit build. A negative handle is never valid.
 *
 * `FUNNY_NO_NET=1` is *not* checked here: a listening socket is not reaching
 * out to the network, and a test runner that blocks outbound calls has no
 * reason to stop a program serving its own loopback. `internet.c` decides
 * that, per function, exactly as it does for the client half. */
#define PLATFORM_SOCKET_NONE ((int64_t)-1)
/* platform_tcp_accept / platform_socket_recv: nobody arrived, or nothing was
   said, within the timeout. Not an error -- the caller usually loops. */
#define PLATFORM_SOCKET_TIMEOUT ((int64_t)-1)
#define PLATFORM_SOCKET_ERROR ((int64_t)-2)

/* Binds `port` on `host` ("" or NULL for every interface) and starts
   listening. Returns a handle, or PLATFORM_SOCKET_NONE with a reason in
   `errbuf` -- "address already in use" is the one everybody hits, so it says
   which port. SO_REUSEADDR is set: without it a server restarted inside the
   TIME_WAIT window cannot rebind its own port, which makes development
   miserable for no safety gained. */
int64_t platform_tcp_listen(const char *host, int port, int backlog, char *errbuf, size_t errbuf_len);

/* Waits up to `timeoutMs` for a connection (negative: forever). Returns the
   new connection's handle, PLATFORM_SOCKET_TIMEOUT if nobody arrived, or
   PLATFORM_SOCKET_ERROR. `peerOut` gets the peer's address as text when it
   is not NULL. */
int64_t platform_tcp_accept(int64_t listener, int timeoutMs, char *peerOut, size_t peerOut_len);

/* Reads up to `len` bytes. >0 is a byte count; 0 is a clean close by the
   other end; PLATFORM_SOCKET_TIMEOUT is nothing said in time;
   PLATFORM_SOCKET_ERROR is a broken connection. */
int64_t platform_socket_recv(int64_t sock, char *buf, size_t len, int timeoutMs);

/* Writes all of `len` bytes, looping over short writes. True only if every
   byte went. */
bool platform_socket_send(int64_t sock, const char *buf, size_t len);

/* Dials out: a raw TCP connection, with none of the HTTP above it. The
   client half of the same handle type -- what comes back works with
   platform_socket_recv/send/close exactly like an accepted connection. */
int64_t platform_tcp_connect(const char *host, int port, int timeoutMs);

/* The port a listener actually ended up on. Asking for port 0 means "pick
   one nobody is using", which is how a test binds without gambling on a
   fixed number being free -- and then it has to be able to find out which. */
int platform_socket_port(int64_t sock);

/* Waits until at least one of `handles` has something to read -- for a
   listener, that means somebody is waiting to be accepted -- or `timeoutMs`
   elapses (negative: forever). `readyOut[i]` is set to 1 for each handle that
   is ready and 0 otherwise. Returns how many are ready, or -1 on error.

   This is what lets one thread serve several callers at once: the event loop
   asks it about every socket any task is blocked on, all in one call, instead
   of each task sitting in its own blocking read. */
int platform_poll_sockets(const int64_t *handles, int count, int timeoutMs, unsigned char *readyOut);

/* Closes a listener or a connection. A TLS connection sends close_notify
   first; a secure listener releases its identity. */
void platform_socket_close(int64_t sock);

/* -- TLS on the handles above (extensive_examples/web_server_https) --------
 *
 * The server side of TLS, and a client that can be told which one CA to
 * trust. Same OS-native backends as platform_http_request -- dlopen'd OpenSSL,
 * Schannel, Secure Transport -- and nothing above this header learns which.
 *
 * A TLS connection is still an ordinary handle: platform_socket_recv/send/
 * close and platform_poll_sockets consult a side table and do the right
 * thing, so code written for plain sockets works unchanged on encrypted ones.
 * recv returns plaintext; poll reports a TLS connection ready when the
 * session is already holding decrypted bytes, not only when the socket is
 * readable. One difference callers must allow for: a TLS recv can report
 * PLATFORM_SOCKET_TIMEOUT right after poll said "ready", when what arrived
 * was only part of a record. */

/* Binds and listens exactly like platform_tcp_listen, and loads a server
   identity from a PKCS#12 file. Every connection accepted from the returned
   listener carries a TLS session that has not handshaken yet. TLS 1.2 is the
   minimum; renegotiation and compression are off. PLATFORM_SOCKET_NONE with a
   reason in errbuf if the file cannot be read, the password is wrong, there
   is no TLS library, or the port cannot be bound. */
int64_t platform_tls_listen(const char *host, int port, int backlog, const char *pfxPath, const char *password,
                            char *errbuf, size_t errbuf_len);

/* Whether a handle carries a TLS session (a connection) or identity (a
   listener). */
bool platform_socket_is_tls(int64_t sock);

/* One step of a server-side handshake, without blocking: 1 when it is done
   (also for a plain connection, which has nothing to do), 0 when it needs
   more bytes from the peer -- wait for the socket to be readable and call
   again -- and -1 when it failed, with a reason in errbuf. */
int platform_tls_handshake(int64_t sock, char *errbuf, size_t errbuf_len);

/* The negotiated protocol ("TLSv1.3") and cipher suite, for logs. False for a
   plain connection or one that has not finished its handshake. */
bool platform_tls_info(int64_t sock, char *version, size_t version_len, char *cipher, size_t cipher_len);

/* Dials out and completes a TLS handshake, verifying the peer's chain *and*
   that it was issued for `serverName`. With `caPath` (a PEM file) the chain
   must lead to exactly that CA and nothing else is trusted; without it, the
   system trust store is used. There is deliberately no way to skip
   verification. PLATFORM_SOCKET_NONE with a reason in errbuf on failure. */
int64_t platform_tls_connect(const char *host, int port, int timeoutMs, const char *serverName, const char *caPath,
                             char *errbuf, size_t errbuf_len);

/* -- randomness ------------------------------------------------------------ */

/* `n` bytes from the operating system's CSPRNG (BCryptGenRandom,
   arc4random_buf, /dev/urandom). False only if the OS refused. */
bool platform_random_bytes(unsigned char *out, size_t n);


/* -- cryptography (RUNTIME_PLAN.md R3, `gimme vault`) ----------------------
 *
 * Password hashing and authenticated encryption, from whatever the OS
 * already has: the dlopen'd OpenSSL that `https` uses on Linux/BSD, CNG on
 * Windows, CommonCrypto on macOS. Nothing here is implemented in this
 * repository, and that is the point -- a hand-written AES is a liability, and
 * every platform ships a reviewed one.
 *
 * Each returns false with a sentence in `errbuf` rather than a bare failure.
 * On a machine with no crypto library at all (only possible on Linux/BSD,
 * where OpenSSL is loaded at run time), that sentence is the same "needs
 * OpenSSL" one `https` gives, and `vault` turns it into a SkillIssue.
 */

/* PBKDF2-HMAC-SHA256 into `out`. `iterations` is the caller's: `vault`
   defaults to 600,000 and records what it used alongside the hash. */
bool platform_pbkdf2_sha256(const unsigned char *password, size_t passwordLen, const unsigned char *salt,
                            size_t saltLen, int iterations, unsigned char *out, size_t outLen, char *errbuf,
                            size_t errbuf_len);

/* SHA-256 of `data`, 32 bytes into `out`. */
bool platform_sha256(const unsigned char *data, size_t len, unsigned char *out, char *errbuf, size_t errbuf_len);

/* HMAC-SHA256, 32 bytes into `out`. */
bool platform_hmac_sha256(const unsigned char *key, size_t keyLen, const unsigned char *data, size_t len,
                          unsigned char *out, char *errbuf, size_t errbuf_len);

/* AES-256-GCM. `key` is 32 bytes, `nonce` 12, `tag` 16, and `cipherOut` holds
   `plainLen` bytes. `aad` is authenticated but not encrypted, so a sealed
   value can be bound to the record it belongs to and not be movable to
   another. */
bool platform_aes_gcm_seal(const unsigned char *key, const unsigned char *nonce, const unsigned char *aad,
                           size_t aadLen, const unsigned char *plain, size_t plainLen, unsigned char *cipherOut,
                           unsigned char *tagOut, char *errbuf, size_t errbuf_len);

/* The other direction. False -- with nothing written to `plainOut` that a
   caller should look at -- when the key, the nonce, the aad or a single bit
   of the ciphertext is wrong: GCM cannot tell those apart, and neither
   should the caller. */
bool platform_aes_gcm_open(const unsigned char *key, const unsigned char *nonce, const unsigned char *aad,
                           size_t aadLen, const unsigned char *cipher, size_t cipherLen, const unsigned char *tag,
                           unsigned char *plainOut, char *errbuf, size_t errbuf_len);

/* -- interrupts (RUNTIME_PLAN.md R5) --------------------------------------
 *
 * `computer.until_ctrl_c()` is an `otw` that settles the first time somebody
 * presses Ctrl-C, so a server can shut down tidily instead of being killed
 * mid-write. The handler itself does one thing -- set a flag -- because
 * almost nothing else is safe to do inside a signal handler; the event loop
 * notices the flag on its next turn, which is never more than one wait cap
 * away.
 */

/* Starts watching for SIGINT / CTRL_C_EVENT. Idempotent. The *second*
   interrupt is left to the default action, so a program that hangs during its
   own shutdown can still be stopped with another Ctrl-C. */
void platform_on_interrupt(void);

/* Has an interrupt arrived since watching began? Never blocks, and stays true
   once set -- it is a latch, not a queue. */
bool platform_interrupt_seen(void);

/* -- threads, mutexes, condition variables (ASYNC_PLAN.md A0) -------------
 *
 * `interns` runs each worker on a real OS thread, in its own VM with its own
 * heap; nothing above this header knows whether that is pthreads or Win32.
 *
 * The three types are opaque *fixed-size storage*, not pointers to something
 * malloc'd. Two reasons. A mutex that has to be allocated has an allocation
 * failure path, and a mutex you cannot create is not a failure any caller can
 * do anything useful about. And these want to sit inside other structs -- an
 * `otw` needs one to be settled from a worker thread -- where a bare member
 * is far easier to reason about than an owned pointer with a lifetime.
 *
 * The sizes below are deliberately generous, and platform.c asserts each one
 * against the real underlying type at compile time. Getting this wrong is
 * therefore a build error on the platform in question, not a stack smash
 * discovered later. */

typedef struct { _Alignas(16) unsigned char opaque[64]; } PlatformThread;
typedef struct { _Alignas(16) unsigned char opaque[64]; } PlatformMutex;
typedef struct { _Alignas(16) unsigned char opaque[64]; } PlatformCond;

/* The body a thread runs. Returns nothing: a worker's *result* travels back
   through memory the caller owns, guarded by the caller's own mutex, because
   a thread return value would have to be a void* and this layer does not
   want an ownership question it cannot answer. */
typedef void (*PlatformThreadFn)(void *userdata);

/* Starts `fn(userdata)` on a new thread. False if the OS refused, with a
   reason in errbuf. `userdata` must outlive the thread -- nothing is copied.
   Every started thread must be joined; there is deliberately no detach, so
   a leaked thread is a bug rather than a supported mode. */
bool platform_thread_start(PlatformThread *thread, PlatformThreadFn fn, void *userdata,
                           char *errbuf, size_t errbuf_len);

/* Blocks until the thread's function has returned, then releases the OS
   handle. Calling it twice on one thread is undefined; call it exactly once. */
void platform_thread_join(PlatformThread *thread);

/* An identifier for the calling thread, equal across calls on one thread and
   different between live threads. Only ever compared, never interpreted --
   it exists so an assertion can say "this must run on the loop thread". */
uint64_t platform_thread_id(void);

void platform_mutex_init(PlatformMutex *m);
void platform_mutex_destroy(PlatformMutex *m);
void platform_mutex_lock(PlatformMutex *m);
void platform_mutex_unlock(PlatformMutex *m);

void platform_cond_init(PlatformCond *c);
void platform_cond_destroy(PlatformCond *c);
/* Atomically releases `m`, waits for a signal, and reacquires `m` before
   returning. Spurious wakeups are permitted by both backends, so every
   caller must re-check its predicate in a loop -- this layer does not
   pretend otherwise. */
void platform_cond_wait(PlatformCond *c, PlatformMutex *m);
/* As above, bounded. Returns false if the deadline passed without a signal.
   A spurious wakeup can still return true, so the predicate loop applies
   here too. */
bool platform_cond_wait_ms(PlatformCond *c, PlatformMutex *m, int timeoutMs);
void platform_cond_signal(PlatformCond *c);
void platform_cond_broadcast(PlatformCond *c);

#endif /* FUNNY_PLATFORM_H */
