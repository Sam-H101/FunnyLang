/* native/platform.c -- see platform.h. The only file in native/ allowed
 * an #ifdef _WIN32 (ARCHITECTURE.md's platform boundary) -- everything
 * OS-specific (filesystem, timing, system info, sockets) lives behind the
 * portable functions platform.h declares. Two real build entry points
 * exist for this project (build.sh for gcc/clang, build.bat for MSVC's
 * cl.exe), so both branches below are real, compiled, and tested code,
 * not one live path and one aspirational stub.
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#else
/* Feature-test macros must come before any header (POSIX.1-2008: realpath,
   mkdir, rmdir, unlink, opendir/readdir/closedir, getcwd, clock_gettime,
   nanosleep, localtime_r) -- otherwise -std=c11 hides them, the same way
   it hid <math.h>'s M_PI/M_E for mafs.c. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#ifdef __APPLE__
/* ...and on Darwin, asking for POSIX *narrows* the namespace unless you also
   ask for the BSD one. <sys/cdefs.h> reads:

       _ANSI_SOURCE                          -> __DARWIN_C_ANSI
       _POSIX_C_SOURCE && !_DARWIN_C_SOURCE  -> that POSIX level
       otherwise                             -> __DARWIN_C_FULL

   and the short type names u_int/u_char/u_short are declared only at
   __DARWIN_C_FULL. <sys/sysctl.h> pulls in <sys/ucred.h> and <sys/proc.h>,
   both written in terms of them, so without this the *SDK header* fails to
   compile -- nothing in this file is even reached. */
#define _DARWIN_C_SOURCE
#endif
#endif

#include "platform.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <bcrypt.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
typedef SOCKET SockFd;
#define SOCK_INVALID INVALID_SOCKET
#else
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>   /* ASYNC_PLAN.md A0: threads behind platform.h */
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
typedef int SockFd;
#define SOCK_INVALID (-1)
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/SecureTransport.h>
#include <mach-o/dyld.h> /* _NSGetExecutablePath */
#include <sys/sysctl.h>  /* sysctlbyname: hw.memsize, hw.logicalcpu */
#else
#include <dlfcn.h>
#endif
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void set_errbuf(char *errbuf, size_t n, int err) {
    if (!errbuf || n == 0) return;
    snprintf(errbuf, n, "%s", strerror(err));
}

#ifdef _WIN32
static void set_errbuf_win32(char *errbuf, size_t n, DWORD err) {
    if (!errbuf || n == 0) return;
    char buf[256];
    DWORD len =
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, buf, sizeof(buf), NULL);
    if (len > 0) {
        while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) buf[--len] = '\0';
        snprintf(errbuf, n, "%s", buf);
    } else {
        snprintf(errbuf, n, "Windows error %lu", (unsigned long)err);
    }
}
#endif

/* -- portable case-insensitive helpers (replace strcasecmp/strcasestr,
   which MSVC doesn't provide) -- used only by the HTTP header parser
   below. */
static bool ci_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool ci_contains(const char *haystack, const char *needle) {
    size_t hn = strlen(haystack), nn = strlen(needle);
    if (nn == 0) return true;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t j = 0;
        for (; j < nn; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) break;
        }
        if (j == nn) return true;
    }
    return false;
}

#ifdef _WIN32
/* -- UTF-8 <-> UTF-16 at the Windows filesystem boundary -------------------
 *
 * Paths are UTF-8 `char *` everywhere else in the runtime, because that is
 * the only string representation FunnyLang has. Windows' `A` entry points
 * interpret those bytes in the active ANSI codepage, so a name with a
 * character that codepage cannot represent -- an accent, an emoji, any CJK --
 * addresses a different file, or none. The `W` entry points take UTF-16 and
 * have no such limit, so the conversion happens here, once, and nothing
 * outside this file has to know.
 *
 * Returns a malloc'd wide string the caller frees, or NULL if the input is
 * not valid UTF-8 (in which case the caller reports the operation as failing,
 * which is the truth: there is no file by that name).
 */
static wchar_t *widen(const char *path) {
    if (path == NULL) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *out = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (out == NULL) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, n) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

/* The other direction, for a name read back out of a directory listing. */
static char *narrow(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *out = (char *)malloc((size_t)n);
    if (out == NULL) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

/* fopen with a UTF-8 path. _wfopen is the whole point; the mode is ASCII so
   a fixed-size widening buffer is enough for it. */
static FILE *fopen_utf8(const char *path, const char *mode) {
    wchar_t *wpath = widen(path);
    if (wpath == NULL) {
        errno = ENOENT;
        return NULL;
    }
    wchar_t wmode[8];
    size_t i = 0;
    for (; mode[i] != '\0' && i < (sizeof wmode / sizeof wmode[0]) - 1; i++) wmode[i] = (wchar_t)mode[i];
    wmode[i] = L'\0';
    FILE *f = _wfopen(wpath, wmode);
    free(wpath);
    return f;
}

static DWORD attrs_utf8(const char *path) {
    wchar_t *wpath = widen(path);
    if (wpath == NULL) return INVALID_FILE_ATTRIBUTES;
    DWORD attrs = GetFileAttributesW(wpath);
    free(wpath);
    return attrs;
}
#else
/* Everywhere else a path is already UTF-8 bytes and the C library takes it
   as-is, so these are the identity. */
#define fopen_utf8(path, mode) fopen((path), (mode))
#endif

#ifdef _WIN32
static bool path_is_dir(const char *path) {
    DWORD attrs = attrs_utf8(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/* Python's Path.is_file() is S_ISREG; Windows has no such attribute, so
   "exists and isn't a directory" is the closest equivalent -- which is what
   Python itself reports for every ordinary file on this platform. */
static bool path_is_file(const char *path) {
    DWORD attrs = attrs_utf8(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
#else
static bool path_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool path_is_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
#endif

bool platform_path_is_dir(const char *path) { return path_is_dir(path); }
bool platform_path_is_file(const char *path) { return path_is_file(path); }

/* -- file I/O: plain standard-C stdio, portable without any #ifdef -----
   (open/read/write/close's own errno-setting behavior is standardized by
   the C runtime on both platforms, so strerror(errno) still gives the
   right human-readable reason either way). */

bool platform_read_file(const char *path, unsigned char **out_data, size_t *out_len, char *errbuf,
                         size_t errbuf_len) {
    if (path_is_dir(path)) {
        set_errbuf(errbuf, errbuf_len, EISDIR);
        return false;
    }
    FILE *f = fopen_utf8(path, "rb");
    if (!f) {
        set_errbuf(errbuf, errbuf_len, errno);
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        int e = errno;
        fclose(f);
        set_errbuf(errbuf, errbuf_len, e);
        return false;
    }
    long size = ftell(f);
    if (size < 0) {
        int e = errno;
        fclose(f);
        set_errbuf(errbuf, errbuf_len, e);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        int e = errno;
        fclose(f);
        set_errbuf(errbuf, errbuf_len, e);
        return false;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)size + 1);
    size_t total = size > 0 ? fread(buf, 1, (size_t)size, f) : 0;
    if (total != (size_t)size && ferror(f)) {
        int e = errno;
        free(buf);
        fclose(f);
        set_errbuf(errbuf, errbuf_len, e);
        return false;
    }
    fclose(f);
    buf[total] = '\0';
    *out_data = buf;
    *out_len = total;
    return true;
}

static bool write_impl(const char *path, const unsigned char *data, size_t len, const char *mode, char *errbuf,
                        size_t errbuf_len) {
    FILE *f = fopen_utf8(path, mode);
    if (!f) {
        set_errbuf(errbuf, errbuf_len, errno);
        return false;
    }
    size_t written = len > 0 ? fwrite(data, 1, len, f) : 0;
    if (written != len) {
        int e = errno;
        fclose(f);
        set_errbuf(errbuf, errbuf_len, e);
        return false;
    }
    if (fclose(f) != 0) {
        set_errbuf(errbuf, errbuf_len, errno);
        return false;
    }
    return true;
}

bool platform_write_file(const char *path, const unsigned char *data, size_t len, char *errbuf, size_t errbuf_len) {
    return write_impl(path, data, len, "wb", errbuf, errbuf_len);
}

bool platform_append_file(const char *path, const unsigned char *data, size_t len, char *errbuf, size_t errbuf_len) {
    return write_impl(path, data, len, "ab", errbuf, errbuf_len);
}

#ifdef _WIN32

bool platform_path_exists(const char *path) { return attrs_utf8(path) != INVALID_FILE_ATTRIBUTES; }

bool platform_remove_path(const char *path, char *errbuf, size_t errbuf_len) {
    if (!platform_path_exists(path)) {
        set_errbuf_win32(errbuf, errbuf_len, ERROR_FILE_NOT_FOUND);
        return false;
    }
    wchar_t *wpath = widen(path);
    if (wpath == NULL) {
        set_errbuf(errbuf, errbuf_len, ENOENT);
        return false;
    }
    BOOL ok = path_is_dir(path) ? RemoveDirectoryW(wpath) : DeleteFileW(wpath);
    free(wpath);
    if (!ok) {
        set_errbuf_win32(errbuf, errbuf_len, GetLastError());
        return false;
    }
    return true;
}

static bool make_one_dir(const char *path, char *errbuf, size_t errbuf_len) {
    wchar_t *wpath = widen(path);
    if (wpath == NULL) {
        set_errbuf(errbuf, errbuf_len, ENOENT);
        return false;
    }
    int rc = _wmkdir(wpath);
    free(wpath);
    if (rc != 0) {
        /* "Already there" is success for mkdir -p, and Windows spells it more
           than one way: EEXIST for an ordinary directory, EACCES for a drive
           root. Ask what is true rather than trusting the errno. */
        if (!path_is_dir(path)) {
            set_errbuf(errbuf, errbuf_len, errno);
            return false;
        }
    }
    return true;
}

#else

bool platform_path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

bool platform_remove_path(const char *path, char *errbuf, size_t errbuf_len) {
    struct stat st;
    if (stat(path, &st) != 0) {
        set_errbuf(errbuf, errbuf_len, errno);
        return false;
    }
    int rc = S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path);
    if (rc != 0) {
        set_errbuf(errbuf, errbuf_len, errno);
        return false;
    }
    return true;
}

static bool make_one_dir(const char *path, char *errbuf, size_t errbuf_len) {
    if (mkdir(path, 0755) != 0) {
        if (errno == EEXIST) {
            struct stat st;
            if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
                set_errbuf(errbuf, errbuf_len, EEXIST);
                return false;
            }
        } else {
            set_errbuf(errbuf, errbuf_len, errno);
            return false;
        }
    }
    return true;
}

#endif

/* '\' is a path separator on Windows and an ordinary (legal) filename
   character on POSIX, so it only counts as one where it actually is one. */
static bool is_path_sep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

bool platform_is_path_sep(char c) { return is_path_sep(c); }

char platform_path_sep(void) {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

size_t platform_drive_prefix_len(const char *path) {
#ifdef _WIN32
    /* "C:" -- one letter and a colon. UNC paths ("\\\\server\\share") are
       not special-cased: their leading separators already make them
       absolute, which is all the path helpers need from them. */
    if (path[0] != '\0' && path[1] == ':' &&
        ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))) {
        return 2;
    }
    return 0;
#else
    (void)path;
    return 0;
#endif
}

bool platform_mkdir_p(const char *path, char *errbuf, size_t errbuf_len) {
    size_t len = strlen(path);
    char buf[4096];
    if (len == 0 || len >= sizeof(buf)) {
        set_errbuf(errbuf, errbuf_len, ENAMETOOLONG);
        return false;
    }
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i <= len; i++) {
        if (!is_path_sep(buf[i]) && i != len) continue;
        char saved = buf[i];
        buf[i] = '\0';
        if (buf[0] != '\0' && !make_one_dir(buf, errbuf, errbuf_len)) return false;
        buf[i] = saved;
    }
    return true;
}

static int cmp_cstr(const void *a, const void *b) {
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;
    return strcmp(*sa, *sb);
}

#ifdef _WIN32

bool platform_list_dir(const char *path, char ***out_names, size_t *out_count, char *errbuf, size_t errbuf_len) {
    char pattern[4100];
    snprintf(pattern, sizeof(pattern), "%s/*", path);
    wchar_t *wpattern = widen(pattern);
    if (wpattern == NULL) {
        set_errbuf(errbuf, errbuf_len, ENOENT);
        return false;
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpattern, &fd);
    free(wpattern);
    if (h == INVALID_HANDLE_VALUE) {
        set_errbuf_win32(errbuf, errbuf_len, GetLastError());
        return false;
    }
    size_t cap = 16, count = 0;
    char **names = (char **)malloc(cap * sizeof(char *));
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        /* Back to UTF-8 immediately: every caller above this one, and the
           language itself, only knows UTF-8 `char *`. */
        char *name = narrow(fd.cFileName);
        if (name == NULL) continue;
        if (count == cap) {
            cap *= 2;
            names = (char **)realloc(names, cap * sizeof(char *));
        }
        names[count++] = name;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    /* Sorted by UTF-8 bytes, which is what the POSIX branch does too and what
       funnylang's own `sorted()` on a list of str produces for the ASCII
       names that make up every real corpus. */
    qsort(names, count, sizeof(char *), cmp_cstr);
    *out_names = names;
    *out_count = count;
    return true;
}

bool platform_abs_path(const char *path, char *out, size_t out_len, char *errbuf, size_t errbuf_len) {
    char resolved[4096];
    {
        wchar_t *wpath = widen(path);
        if (wpath == NULL) {
            set_errbuf(errbuf, errbuf_len, ENOENT);
            return false;
        }
        wchar_t wresolved[4096];
        wchar_t *ok = _wfullpath(wresolved, wpath, sizeof(wresolved) / sizeof(wresolved[0]));
        free(wpath);
        if (ok == NULL) {
            set_errbuf(errbuf, errbuf_len, errno);
            return false;
        }
        char *utf8 = narrow(wresolved);
        if (utf8 == NULL) {
            set_errbuf(errbuf, errbuf_len, EILSEQ);
            return false;
        }
        snprintf(resolved, sizeof(resolved), "%s", utf8);
        free(utf8);
    }
    /* _fullpath uses backslashes; normalize to '/' so the rest of the
       runtime's own path-string functions (filez.c) stay POSIX-style --
       Windows accepts '/' just as well as '\' in every API used here. */
    for (char *p = resolved; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    snprintf(out, out_len, "%s", resolved);
    return true;
}

#else

bool platform_list_dir(const char *path, char ***out_names, size_t *out_count, char *errbuf, size_t errbuf_len) {
    DIR *d = opendir(path);
    if (!d) {
        set_errbuf(errbuf, errbuf_len, errno);
        return false;
    }
    size_t cap = 16, count = 0;
    char **names = (char **)malloc(cap * sizeof(char *));
    struct dirent *ent;
    errno = 0;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (count == cap) {
            cap *= 2;
            names = (char **)realloc(names, cap * sizeof(char *));
        }
        names[count++] = strdup(ent->d_name);
        errno = 0;
    }
    if (errno != 0) {
        int e = errno;
        for (size_t i = 0; i < count; i++) free(names[i]);
        free(names);
        closedir(d);
        set_errbuf(errbuf, errbuf_len, e);
        return false;
    }
    closedir(d);
    qsort(names, count, sizeof(char *), cmp_cstr);
    *out_names = names;
    *out_count = count;
    return true;
}

static void lexical_normalize(const char *abs_path, char *out, size_t out_len) {
    char *copy = strdup(abs_path);
    char *comps[256];
    int n = 0;
    char *tok = strtok(copy, "/");
    while (tok && n < 256) {
        if (strcmp(tok, ".") == 0) {
            /* dropped */
        } else if (strcmp(tok, "..") == 0) {
            if (n > 0) n--;
        } else {
            comps[n++] = tok;
        }
        tok = strtok(NULL, "/");
    }
    if (n == 0) {
        snprintf(out, out_len, "/");
    } else {
        size_t pos = 0;
        for (int i = 0; i < n; i++) {
            int written = snprintf(out + pos, pos < out_len ? out_len - pos : 0, "/%s", comps[i]);
            if (written > 0) pos += (size_t)written;
        }
    }
    free(copy);
}

bool platform_abs_path(const char *path, char *out, size_t out_len, char *errbuf, size_t errbuf_len) {
    char *raw_abs;
    if (path[0] == '/') {
        raw_abs = strdup(path);
    } else {
        char cwd[4096];
        if (!getcwd(cwd, sizeof(cwd))) {
            set_errbuf(errbuf, errbuf_len, errno);
            return false;
        }
        size_t needed = strlen(cwd) + 1 + strlen(path) + 1;
        raw_abs = (char *)malloc(needed);
        snprintf(raw_abs, needed, "%s/%s", cwd, path);
    }
    char resolved[4096];
    if (realpath(raw_abs, resolved) != NULL) {
        snprintf(out, out_len, "%s", resolved);
        free(raw_abs);
        return true;
    }
    lexical_normalize(raw_abs, out, out_len);
    free(raw_abs);
    return true;
}

#endif

/* -- timing ------------------------------------------------------------ */

double platform_now_seconds(void) {
    /* timespec_get is plain C11 (<time.h>), portable without any #ifdef --
       unlike a monotonic clock, wall-clock time has a standard cross-
       platform API. */
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

#ifdef _WIN32

double platform_monotonic_seconds(void) {
    static LARGE_INTEGER freq;
    static bool init = false;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = true;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}

void platform_sleep_seconds(double seconds) {
    if (seconds <= 0.0) return;
    Sleep((DWORD)(seconds * 1000.0));
}

size_t platform_strftime_now(const char *fmt, char *out, size_t out_len) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_s(&tmv, &t);
    return strftime(out, out_len, fmt, &tmv);
}

#else

double platform_monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void platform_sleep_seconds(double seconds) {
    if (seconds <= 0.0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

size_t platform_strftime_now(const char *fmt, char *out, size_t out_len) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    return strftime(out, out_len, fmt, &tmv);
}

#endif

/* -- system info --------------------------------------------------------
   Both branches mirror funnylang/stdlib/computer.py's own platform-
   specific fallbacks exactly (its Windows path already uses ctypes to
   call GlobalMemoryStatusEx/GetTickCount64; this is the same two calls,
   just from C instead of ctypes). Excluded from the byte-diffed suite
   either way (computer.flex()'s own AGENT CHOICE, logged in NATIVE_PLAN.
   md's §9), so exact parity with Python's formatting isn't the bar here
   -- just genuinely working values. */

#ifdef _WIN32

uint64_t platform_ram_bytes(void) {
    MEMORYSTATUSEX stat;
    stat.dwLength = sizeof(stat);
    if (!GlobalMemoryStatusEx(&stat)) return 0;
    return (uint64_t)stat.ullTotalPhys;
}

double platform_uptime_seconds(void) { return (double)GetTickCount64() / 1000.0; }

void platform_os_info(char *sysname_out, size_t sysname_len, char *release_out, size_t release_len) {
    snprintf(sysname_out, sysname_len, "Windows");
    if (release_len > 0) release_out[0] = '\0';
/* GetVersionExA is deprecated (and lies without an app manifest on 8.1+)
   but this is a "your rig: mid" joke line no test ever compares byte for
   byte -- the modern replacement (RtlGetVersion via a runtime GetProcAddress
   lookup) isn't worth the extra ceremony for a value nothing checks. */
#pragma warning(push)
#pragma warning(disable : 4996)
    OSVERSIONINFOA vi;
    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (GetVersionExA(&vi)) {
        snprintf(release_out, release_len, "%lu.%lu.%lu", (unsigned long)vi.dwMajorVersion,
                 (unsigned long)vi.dwMinorVersion, (unsigned long)vi.dwBuildNumber);
    }
#pragma warning(pop)
}

int platform_cpu_count(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
}

#else

/* `_SC_PHYS_PAGES` is a glibc extension and does not exist on Darwin, so this
   is one of the two functions in the POSIX branch that genuinely has to
   split. `hw.memsize` answers in bytes directly, with no page arithmetic. */
#ifdef __APPLE__

uint64_t platform_ram_bytes(void) {
    uint64_t bytes = 0;
    size_t len = sizeof bytes;
    if (sysctlbyname("hw.memsize", &bytes, &len, NULL, 0) != 0) return 0;
    return bytes;
}

#else

uint64_t platform_ram_bytes(void) {
    long pageSize = sysconf(_SC_PAGESIZE);
    long pages = sysconf(_SC_PHYS_PAGES);
    if (pageSize <= 0 || pages <= 0) return 0;
    return (uint64_t)pageSize * (uint64_t)pages;
}

#endif

double platform_uptime_seconds(void) {
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        double up;
        int ok = fscanf(f, "%lf", &up);
        fclose(f);
        if (ok == 1) return up;
    }
    static double processStart = 0.0;
    static bool started = false;
    if (!started) {
        processStart = platform_now_seconds();
        started = true;
    }
    return platform_now_seconds() - processStart;
}

void platform_os_info(char *sysname_out, size_t sysname_len, char *release_out, size_t release_len) {
    if (sysname_len > 0) sysname_out[0] = '\0';
    if (release_len > 0) release_out[0] = '\0';
    struct utsname u;
    if (uname(&u) == 0) {
        snprintf(sysname_out, sysname_len, "%s", u.sysname);
        snprintf(release_out, release_len, "%s", u.release);
    }
}

/* `_SC_NPROCESSORS_ONLN` is the other glibc extension Darwin lacks.
   `hw.logicalcpu` is the online logical count, which is what
   `_SC_NPROCESSORS_ONLN` means -- `hw.ncpu` is the older name for roughly
   the same thing and is kept as the fallback, since it is what older Darwin
   releases answer to. */
#ifdef __APPLE__

int platform_cpu_count(void) {
    int n = 0;
    size_t len = sizeof n;
    if (sysctlbyname("hw.logicalcpu", &n, &len, NULL, 0) == 0 && n > 0) return n;
    n = 0;
    len = sizeof n;
    if (sysctlbyname("hw.ncpu", &n, &len, NULL, 0) == 0 && n > 0) return n;
    return 0;
}

#else

int platform_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 0;
}

#endif

#endif

/* -- the running executable ---------------------------------------------- */

#ifdef _WIN32

bool platform_executable_path(char *out, size_t out_len) {
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)out_len);
    if (n == 0 || n >= out_len) return false;
    for (char *p = out; *p; p++) {
        if (*p == '\\') *p = '/'; /* keep one path flavour above this layer */
    }
    return true;
}

bool platform_temp_file(const char *prefix, char *out, size_t out_len) {
    char dir[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(dir), dir);
    if (n == 0 || n >= sizeof(dir)) return false;
    char path[MAX_PATH];
    if (GetTempFileNameA(dir, prefix, 0, path) == 0) return false; /* creates it */
    for (char *p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    if (strlen(path) >= out_len) return false;
    snprintf(out, out_len, "%s", path);
    return true;
}

bool platform_make_executable(const char *path) {
    (void)path; /* Windows decides by extension, not by a mode bit */
    return true;
}

bool platform_stdout_is_tty(void) { return _isatty(_fileno(stdout)) != 0; }

void platform_console_init(void) {
    /* The §4.2 renderer emits box-drawing characters and emoji as UTF-8
       and colour as ANSI. Neither survives a default Windows console:
       the output code page is whatever ANSI page the machine is set to,
       and VT sequences are printed literally unless the mode bit is on.
       Both failures are cosmetic-looking but would break N6's
       byte-identical-stderr acceptance outright. */
    SetConsoleOutputCP(CP_UTF8);
    HANDLE handles[2] = {GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE)};
    for (int i = 0; i < 2; i++) {
        DWORD mode = 0;
        if (handles[i] == INVALID_HANDLE_VALUE || !GetConsoleMode(handles[i], &mode)) continue;
        SetConsoleMode(handles[i], mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

#else

bool platform_executable_path(char *out, size_t out_len) {
#ifdef __APPLE__
    uint32_t size = (uint32_t)out_len;
    if (_NSGetExecutablePath(out, &size) != 0) return false;
    return true;
#else
    ssize_t n = readlink("/proc/self/exe", out, out_len - 1);
    if (n <= 0) return false;
    out[n] = '\0';
    return true;
#endif
}

bool platform_temp_file(const char *prefix, char *out, size_t out_len) {
    const char *dir = getenv("TMPDIR");
    if (!dir || dir[0] == '\0') dir = "/tmp";
    int written = snprintf(out, out_len, "%s/%sXXXXXX", dir, prefix);
    if (written < 0 || (size_t)written >= out_len) return false;
    int fd = mkstemp(out);
    if (fd < 0) return false;
    close(fd);
    return true;
}

bool platform_make_executable(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    /* +x wherever there's already an r, matching `chmod +x`'s own umask-
       respecting behaviour rather than forcing 0777. */
    mode_t mode = st.st_mode;
    if (mode & S_IRUSR) mode |= S_IXUSR;
    if (mode & S_IRGRP) mode |= S_IXGRP;
    if (mode & S_IROTH) mode |= S_IXOTH;
    return chmod(path, mode) == 0;
}

bool platform_stdout_is_tty(void) { return isatty(STDOUT_FILENO) != 0; }

void platform_console_init(void) { /* UTF-8 and ANSI both already work */ }

#endif

/* -- networking ---------------------------------------------------------
   The socket primitives just below (ensure_winsock/sock_*) are the only
   platform-conditional pieces; every algorithm above them in the call
   graph (URL parsing, the HTTP/1.1 request/response cycle, chunked
   decoding, redirect-following) is identical C compiled on both
   platforms, using these as its only OS-facing surface. */

bool platform_net_disabled(void) {
    const char *v = getenv("FUNNY_NO_NET");
    return v != NULL && strcmp(v, "1") == 0;
}

#ifdef _WIN32

static void ensure_winsock(void) {
    static bool started = false;
    if (!started) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        started = true;
    }
}

static void sock_set_nonblocking(SockFd fd, bool nonblocking) {
    u_long mode = nonblocking ? 1 : 0;
    ioctlsocket(fd, FIONBIO, &mode);
}

static bool sock_in_progress(void) {
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}

/* The accept or read that would have blocked, or a connection that was
   reset before it could be accepted: none of them is the listener failing. */
static bool sock_would_block(void) {
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR || e == WSAECONNRESET;
}

static void sock_close(SockFd fd) { closesocket(fd); }

static bool sock_set_recv_timeout(SockFd fd, int ms) {
    if (ms < 1) ms = 1;
    DWORD tv = (DWORD)ms;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv)) == 0;
}

static int sock_poll_writable(SockFd fd, int timeoutMs) {
    WSAPOLLFD pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    return WSAPoll(&pfd, 1, timeoutMs);
}

static int sock_poll_readable(SockFd fd, int timeoutMs) {
    WSAPOLLFD pfd;
    pfd.fd = fd;
    pfd.events = POLLRDNORM;
    pfd.revents = 0;
    return WSAPoll(&pfd, 1, timeoutMs);
}

static bool sock_set_reuseaddr(SockFd fd) {
    BOOL on = TRUE;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on)) == 0;
}

static const char *sock_last_error(char *buf, size_t len) {
    int e = WSAGetLastError();
    snprintf(buf, len, "winsock error %d", e);
    return buf;
}

static long sock_send(SockFd fd, const char *buf, size_t len) {
    int n = len > (size_t)INT_MAX ? INT_MAX : (int)len;
    return (long)send(fd, buf, n, 0);
}

static long sock_recv(SockFd fd, char *buf, size_t len) {
    int n = len > (size_t)INT_MAX ? INT_MAX : (int)len;
    return (long)recv(fd, buf, n, 0);
}

#else

/* Not Winsock, but the same "first time anything touches a socket" hook:
   a write to a connection the peer has already dropped raises SIGPIPE, whose
   default action kills the whole process. A server cannot let one impatient
   client take it down, so the signal is ignored and the write fails with
   EPIPE instead, which platform_socket_send already reports as a broken
   connection. Once per process, and before any worker thread can race it. */
static pthread_once_t g_sigpipeOnce = PTHREAD_ONCE_INIT;
static void ignore_sigpipe(void) { signal(SIGPIPE, SIG_IGN); }
static void ensure_winsock(void) { pthread_once(&g_sigpipeOnce, ignore_sigpipe); }

static void sock_set_nonblocking(SockFd fd, bool nonblocking) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (nonblocking) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    else fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

static bool sock_in_progress(void) { return errno == EINPROGRESS; }

/* The accept or read that would have blocked, or a connection that was
   reset before it could be accepted: none of them is the listener failing. */
static bool sock_would_block(void) {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ECONNABORTED;
}

static void sock_close(SockFd fd) { close(fd); }

static bool sock_set_recv_timeout(SockFd fd, int ms) {
    if (ms < 1) ms = 1;
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

static int sock_poll_writable(SockFd fd, int timeoutMs) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    return poll(&pfd, 1, timeoutMs);
}

static int sock_poll_readable(SockFd fd, int timeoutMs) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    return poll(&pfd, 1, timeoutMs);
}

static bool sock_set_reuseaddr(SockFd fd) {
    int on = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == 0;
}

static const char *sock_last_error(char *buf, size_t len) {
    snprintf(buf, len, "%s", strerror(errno));
    return buf;
}

static long sock_send(SockFd fd, const char *buf, size_t len) { return (long)send(fd, buf, len, 0); }

static long sock_recv(SockFd fd, char *buf, size_t len) { return (long)recv(fd, buf, len, 0); }

#endif

/* -- TLS (NATIVE_PLAN.md N5b / §3.1) ------------------------------------
   An open connection is a socket plus an optional TLS session layered on
   top of it. Everything above this layer -- the HTTP/1.1 request/response
   cycle, chunked decoding, redirect-following -- reads and writes through
   conn_send/conn_recv and never learns which of the two it got, so
   http:// and https:// share one implementation rather than two.

   Certificate verification is on unconditionally on every backend, and
   there is deliberately no way to turn it off: §3.1's "silently accepting
   bad certificates is the kind of joke that stops being funny". */

typedef struct {
    SockFd fd;
    void *tls; /* backend-specific session handle; NULL for plain http */
} HttpConn;

/* tls_start returns false on any failure. It only writes to `reason` for
   the one failure the caller must *name* rather than collapse into the
   generic "the internet said no": this machine has no usable TLS library
   at all. A handshake or certificate rejection leaves `reason` empty --
   §3.1 wants those to read as an ordinary network refusal. */
static bool tls_start(HttpConn *conn, const char *hostname, char *reason, size_t reasonLen);
static long tls_read(HttpConn *conn, char *buf, size_t len);
static long tls_write(HttpConn *conn, const char *buf, size_t len);
static void tls_finish(HttpConn *conn);

#if defined(_WIN32)

/* Windows never reaches these: https:// is handled end to end by WinHTTP
   (winhttp_request, further down), which does TLS, redirect-following and
   the system proxy itself, so http_once only ever runs plain http here. */
static bool tls_start(HttpConn *conn, const char *hostname, char *reason, size_t reasonLen) {
    (void)conn;
    (void)hostname;
    snprintf(reason, reasonLen, "https on Windows goes through WinHTTP, not this path.");
    return false;
}
static long tls_read(HttpConn *conn, char *buf, size_t len) {
    (void)conn;
    (void)buf;
    (void)len;
    return -1;
}
static long tls_write(HttpConn *conn, const char *buf, size_t len) {
    (void)conn;
    (void)buf;
    (void)len;
    return -1;
}
static void tls_finish(HttpConn *conn) { (void)conn; }

#elif defined(__APPLE__)

/* macOS: Secure Transport, part of Security.framework -- the OS *is* the
   dependency, so there's no bundled crypto and the system trust store and
   its settings apply automatically. Deprecated since 10.15 but still the
   only synchronous, C-callable TLS API on the platform (Network.
   framework's replacement is dispatch/async-only, which this blocking
   request path can't use), hence the local deprecation suppression --
   -Werror would otherwise reject the whole file on macOS. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static OSStatus st_sock_read(SSLConnectionRef c, void *data, size_t *dataLength) {
    int fd = (int)(intptr_t)c;
    size_t want = *dataLength, got = 0;
    while (got < want) {
        ssize_t n = recv(fd, (char *)data + got, want - got, 0);
        if (n > 0) {
            got += (size_t)n;
            continue;
        }
        *dataLength = got;
        if (n == 0) return errSSLClosedGraceful;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return errSSLWouldBlock;
        return errSSLInternal;
    }
    *dataLength = got;
    return noErr;
}

static OSStatus st_sock_write(SSLConnectionRef c, const void *data, size_t *dataLength) {
    int fd = (int)(intptr_t)c;
    size_t want = *dataLength, sent = 0;
    while (sent < want) {
        ssize_t n = send(fd, (const char *)data + sent, want - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        *dataLength = sent;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return errSSLWouldBlock;
        return errSSLInternal;
    }
    *dataLength = sent;
    return noErr;
}

static bool tls_start(HttpConn *conn, const char *hostname, char *reason, size_t reasonLen) {
    (void)reason;
    (void)reasonLen; /* Security.framework ships with the OS: never missing */
    SSLContextRef ctx = SSLCreateContext(NULL, kSSLClientSide, kSSLStreamType);
    if (!ctx) return false;
    if (SSLSetIOFuncs(ctx, st_sock_read, st_sock_write) != noErr ||
        SSLSetConnection(ctx, (SSLConnectionRef)(intptr_t)conn->fd) != noErr ||
        /* Both SNI and the name checked against the certificate: a chain
           that's valid for some *other* host must not be accepted. */
        SSLSetPeerDomainName(ctx, hostname, strlen(hostname)) != noErr) {
        CFRelease(ctx);
        return false;
    }
    OSStatus status;
    do {
        status = SSLHandshake(ctx);
    } while (status == errSSLWouldBlock);
    /* Left at its default, Secure Transport evaluates the server's trust
       chain during the handshake and fails here on a bad one -- the
       verification opt-out (kSSLSessionOptionBreakOnServerAuth) is simply
       never set. */
    if (status != noErr) {
        CFRelease(ctx);
        return false;
    }
    conn->tls = ctx;
    return true;
}

static long tls_read(HttpConn *conn, char *buf, size_t len) {
    size_t processed = 0;
    OSStatus status = SSLRead((SSLContextRef)conn->tls, buf, len, &processed);
    if (processed > 0) return (long)processed;
    return (status == errSSLClosedGraceful) ? 0 : -1;
}

static long tls_write(HttpConn *conn, const char *buf, size_t len) {
    size_t processed = 0;
    OSStatus status = SSLWrite((SSLContextRef)conn->tls, buf, len, &processed);
    if (processed > 0) return (long)processed;
    return status == noErr ? 0 : -1;
}

static void tls_finish(HttpConn *conn) {
    if (!conn->tls) return;
    SSLClose((SSLContextRef)conn->tls);
    CFRelease((SSLContextRef)conn->tls);
    conn->tls = NULL;
}

#pragma clang diagnostic pop

#else

/* Linux/BSD: OpenSSL, resolved at *run* time through dlopen rather than
   linked at build time (§3.1) -- which is what keeps the "one C compiler,
   zero dependencies" build promise: this binary still compiles, links and
   runs on a machine with no OpenSSL anywhere, it just can't do https
   there, and says so. Nothing below includes an OpenSSL header; the four
   constants are ABI values, unchanged across every 1.1.0/3.x release. */

#define FUNNY_SSL_VERIFY_PEER 0x01
#define FUNNY_SSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define FUNNY_TLSEXT_NAMETYPE_host_name 0
#define FUNNY_X509_V_OK 0

typedef struct funny_ssl_st FunnySSL;
typedef struct funny_ssl_ctx_st FunnySSLCtx;
typedef struct funny_ssl_method_st FunnySSLMethod;

static struct {
    bool tried;
    void *handle;
    FunnySSLCtx *ctx;
    const FunnySSLMethod *(*TLS_client_method)(void);
    FunnySSLCtx *(*SSL_CTX_new)(const FunnySSLMethod *);
    int (*SSL_CTX_set_default_verify_paths)(FunnySSLCtx *);
    FunnySSL *(*SSL_new)(FunnySSLCtx *);
    void (*SSL_free)(FunnySSL *);
    int (*SSL_set_fd)(FunnySSL *, int);
    int (*SSL_connect)(FunnySSL *);
    int (*SSL_read)(FunnySSL *, void *, int);
    int (*SSL_write)(FunnySSL *, const void *, int);
    int (*SSL_shutdown)(FunnySSL *);
    long (*SSL_get_verify_result)(const FunnySSL *);
    long (*SSL_ctrl)(FunnySSL *, int, long, void *);
    void (*SSL_set_verify)(FunnySSL *, int, void *);
    int (*SSL_set1_host)(FunnySSL *, const char *); /* OpenSSL 1.1.0+; optional */
} g_ssl;

static const char *const OPENSSL_SONAMES[] = {"libssl.so.3", "libssl.so.1.1", "libssl.so"};
#define OPENSSL_SONAME_COUNT (int)(sizeof(OPENSSL_SONAMES) / sizeof(OPENSSL_SONAMES[0]))
#define NO_OPENSSL_MSG \
    "https needs OpenSSL, and none could be loaded here (tried libssl.so.3, libssl.so.1.1, libssl.so)."

/* Loaded once per process under pthread_once: `interns` workers can reach
   this from several threads at the same moment, and a second thread seeing a
   half-filled g_ssl would be a crash rather than an error. */
static pthread_once_t g_sslOnce = PTHREAD_ONCE_INIT;
static void openssl_load(void);

static bool ensure_openssl(char *reason, size_t reasonLen) {
    pthread_once(&g_sslOnce, openssl_load);
    if (!g_ssl.handle) {
        snprintf(reason, reasonLen, "%s", NO_OPENSSL_MSG);
        return false;
    }
    return true;
}

static void openssl_load(void) {
    {
        g_ssl.tried = true;
        for (int i = 0; i < OPENSSL_SONAME_COUNT && !g_ssl.handle; i++) {
            g_ssl.handle = dlopen(OPENSSL_SONAMES[i], RTLD_LAZY | RTLD_LOCAL);
        }
        if (g_ssl.handle) {
            void *h = g_ssl.handle;
            g_ssl.TLS_client_method = (const FunnySSLMethod *(*)(void))dlsym(h, "TLS_client_method");
            g_ssl.SSL_CTX_new = (FunnySSLCtx * (*)(const FunnySSLMethod *)) dlsym(h, "SSL_CTX_new");
            g_ssl.SSL_CTX_set_default_verify_paths =
                (int (*)(FunnySSLCtx *))dlsym(h, "SSL_CTX_set_default_verify_paths");
            g_ssl.SSL_new = (FunnySSL * (*)(FunnySSLCtx *)) dlsym(h, "SSL_new");
            g_ssl.SSL_free = (void (*)(FunnySSL *))dlsym(h, "SSL_free");
            g_ssl.SSL_set_fd = (int (*)(FunnySSL *, int))dlsym(h, "SSL_set_fd");
            g_ssl.SSL_connect = (int (*)(FunnySSL *))dlsym(h, "SSL_connect");
            g_ssl.SSL_read = (int (*)(FunnySSL *, void *, int))dlsym(h, "SSL_read");
            g_ssl.SSL_write = (int (*)(FunnySSL *, const void *, int))dlsym(h, "SSL_write");
            g_ssl.SSL_shutdown = (int (*)(FunnySSL *))dlsym(h, "SSL_shutdown");
            g_ssl.SSL_get_verify_result = (long (*)(const FunnySSL *))dlsym(h, "SSL_get_verify_result");
            g_ssl.SSL_ctrl = (long (*)(FunnySSL *, int, long, void *))dlsym(h, "SSL_ctrl");
            g_ssl.SSL_set_verify = (void (*)(FunnySSL *, int, void *))dlsym(h, "SSL_set_verify");
            g_ssl.SSL_set1_host = (int (*)(FunnySSL *, const char *))dlsym(h, "SSL_set1_host");
            bool complete = g_ssl.TLS_client_method && g_ssl.SSL_CTX_new && g_ssl.SSL_CTX_set_default_verify_paths &&
                            g_ssl.SSL_new && g_ssl.SSL_free && g_ssl.SSL_set_fd && g_ssl.SSL_connect &&
                            g_ssl.SSL_read && g_ssl.SSL_write && g_ssl.SSL_shutdown && g_ssl.SSL_get_verify_result &&
                            g_ssl.SSL_ctrl && g_ssl.SSL_set_verify;
            if (!complete) {
                dlclose(h);
                g_ssl.handle = NULL;
            }
        }
    }
}

static bool tls_start(HttpConn *conn, const char *hostname, char *reason, size_t reasonLen) {
    if (!ensure_openssl(reason, reasonLen)) return false;
    if (!g_ssl.ctx) {
        const FunnySSLMethod *method = g_ssl.TLS_client_method();
        if (!method) return false;
        g_ssl.ctx = g_ssl.SSL_CTX_new(method);
        if (!g_ssl.ctx) return false;
        /* The system trust store. Verification is not optional here and
           there is no flag to disable it (§3.1). */
        g_ssl.SSL_CTX_set_default_verify_paths(g_ssl.ctx);
    }
    FunnySSL *ssl = g_ssl.SSL_new(g_ssl.ctx);
    if (!ssl) return false;
    char host[256];
    snprintf(host, sizeof(host), "%s", hostname);
    /* SNI. SSL_set_tlsext_host_name() is a macro over SSL_ctrl(), so a
       header-free dlsym build has to spell the control code out. */
    g_ssl.SSL_ctrl(ssl, FUNNY_SSL_CTRL_SET_TLSEXT_HOSTNAME, FUNNY_TLSEXT_NAMETYPE_host_name, host);
    /* Hostname verification on top of chain verification: without
       SSL_set1_host, a certificate that chains to a real CA but was issued
       for some entirely different domain would be accepted. */
    if (g_ssl.SSL_set1_host) g_ssl.SSL_set1_host(ssl, host);
    g_ssl.SSL_set_verify(ssl, FUNNY_SSL_VERIFY_PEER, NULL);
    g_ssl.SSL_set_fd(ssl, (int)conn->fd);
    if (g_ssl.SSL_connect(ssl) != 1 || g_ssl.SSL_get_verify_result(ssl) != FUNNY_X509_V_OK) {
        g_ssl.SSL_free(ssl);
        return false;
    }
    conn->tls = ssl;
    return true;
}

static long tls_read(HttpConn *conn, char *buf, size_t len) {
    int n = len > (size_t)INT_MAX ? INT_MAX : (int)len;
    return (long)g_ssl.SSL_read((FunnySSL *)conn->tls, buf, n);
}

static long tls_write(HttpConn *conn, const char *buf, size_t len) {
    int n = len > (size_t)INT_MAX ? INT_MAX : (int)len;
    return (long)g_ssl.SSL_write((FunnySSL *)conn->tls, buf, n);
}

static void tls_finish(HttpConn *conn) {
    if (!conn->tls) return;
    g_ssl.SSL_shutdown((FunnySSL *)conn->tls);
    g_ssl.SSL_free((FunnySSL *)conn->tls);
    conn->tls = NULL;
}

#endif

static long conn_send(HttpConn *conn, const char *buf, size_t len) {
    return conn->tls ? tls_write(conn, buf, len) : sock_send(conn->fd, buf, len);
}

static long conn_recv(HttpConn *conn, char *buf, size_t len) {
    return conn->tls ? tls_read(conn, buf, len) : sock_recv(conn->fd, buf, len);
}

static void conn_close(HttpConn *conn) {
    tls_finish(conn);
    if (conn->fd != SOCK_INVALID) sock_close(conn->fd);
    conn->fd = SOCK_INVALID;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} ByteBuf;

static void bb_append(ByteBuf *b, const char *p, size_t n) {
    if (n == 0) return; /* avoid memcpy(NULL, ..., 0) -- UB even at length 0 */
    if (b->len + n > b->cap) {
        size_t newCap = b->cap == 0 ? 4096 : b->cap * 2;
        while (newCap < b->len + n) newCap *= 2;
        b->data = (char *)realloc(b->data, newCap);
        b->cap = newCap;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void bb_free(ByteBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

typedef struct {
    char scheme[8];
    char host[256];
    int port;
    char path[2048]; /* includes the query string */
} ParsedUrl;

static bool parse_url(const char *url, ParsedUrl *out) {
    out->port = 0;
    const char *schemeEnd = strstr(url, "://");
    if (!schemeEnd) return false;
    size_t schemeLen = (size_t)(schemeEnd - url);
    if (schemeLen == 0 || schemeLen >= sizeof(out->scheme)) return false;
    memcpy(out->scheme, url, schemeLen);
    out->scheme[schemeLen] = '\0';
    const char *p = schemeEnd + 3;
    const char *pathStart = strchr(p, '/');
    const char *hostPortEnd = pathStart ? pathStart : p + strlen(p);
    size_t hostPortLen = (size_t)(hostPortEnd - p);
    char hostPort[300];
    if (hostPortLen == 0 || hostPortLen >= sizeof(hostPort)) return false;
    memcpy(hostPort, p, hostPortLen);
    hostPort[hostPortLen] = '\0';
    char *at = strrchr(hostPort, '@'); /* userinfo, if any -- just skip it */
    char *hostPortReal = at ? at + 1 : hostPort;
    char *colon = strrchr(hostPortReal, ':');
    if (colon) {
        *colon = '\0';
        out->port = atoi(colon + 1);
    }
    size_t hostLen = strlen(hostPortReal);
    if (hostLen == 0 || hostLen >= sizeof(out->host)) return false;
    memcpy(out->host, hostPortReal, hostLen + 1);
    if (out->port <= 0) out->port = strcmp(out->scheme, "https") == 0 ? 443 : 80;
    const char *pathSrc = pathStart ? pathStart : "/";
    size_t pathLen = strlen(pathSrc);
    if (pathLen >= sizeof(out->path)) pathLen = sizeof(out->path) - 1;
    memcpy(out->path, pathSrc, pathLen);
    out->path[pathLen] = '\0';
    return true;
}

static SockFd connect_with_timeout(const char *host, int port, int timeoutMs) {
    ensure_winsock();
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portStr, &hints, &res) != 0 || !res) return SOCK_INVALID;
    SockFd fd = SOCK_INVALID;
    for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == SOCK_INVALID) continue;
        sock_set_nonblocking(fd, true);
        int rc = connect(fd, rp->ai_addr, (int)rp->ai_addrlen);
        bool connected = false;
        if (rc == 0) {
            connected = true;
        } else if (sock_in_progress()) {
            int pr = sock_poll_writable(fd, timeoutMs);
            if (pr > 0) {
                int soErr = 0;
                socklen_t elen = sizeof(soErr);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&soErr, &elen) == 0 && soErr == 0) connected = true;
            }
        }
        if (connected) {
            sock_set_nonblocking(fd, false); /* back to blocking for the data phase */
            break;
        }
        sock_close(fd);
        fd = SOCK_INVALID;
    }
    freeaddrinfo(res);
    return fd;
}

/* Reads whatever's available into `buf`, bounded by `deadline` (an
   absolute platform_monotonic_seconds() time). Returns false on timeout,
   error, or a clean EOF (0 bytes read) -- callers distinguish "EOF" from
   "error" by checking how much of the response they'd already parsed. */
static bool recv_some(HttpConn *conn, ByteBuf *buf, double deadline) {
    double now = platform_monotonic_seconds();
    if (now >= deadline) return false;
    sock_set_recv_timeout(conn->fd, (int)((deadline - now) * 1000.0));
    char tmp[8192];
    long n = conn_recv(conn, tmp, sizeof(tmp));
    if (n <= 0) return false;
    bb_append(buf, tmp, (size_t)n);
    return true;
}

/* Decodes a (possibly still-incomplete) chunked body starting at `data`
   into `out`. Returns true once the terminating 0-length chunk has been
   seen (trailers, if any, are ignored); false if more data is still
   needed (not an error -- the caller keeps reading and retries). */
static bool decode_chunked(const char *data, size_t len, ByteBuf *out) {
    size_t pos = 0;
    for (;;) {
        size_t lineEnd = pos;
        while (lineEnd + 1 < len && !(data[lineEnd] == '\r' && data[lineEnd + 1] == '\n')) lineEnd++;
        if (lineEnd + 1 >= len) return false;
        size_t sizeLen = lineEnd - pos;
        char sizeBuf[32];
        if (sizeLen == 0 || sizeLen >= sizeof(sizeBuf)) return false;
        memcpy(sizeBuf, data + pos, sizeLen);
        sizeBuf[sizeLen] = '\0';
        char *semi = strchr(sizeBuf, ';'); /* chunk extensions -- discard */
        if (semi) *semi = '\0';
        char *endptr = NULL;
        long chunkSize = strtol(sizeBuf, &endptr, 16);
        if (endptr == sizeBuf || chunkSize < 0) return false;
        pos = lineEnd + 2;
        if (chunkSize == 0) return true;
        if (pos + (size_t)chunkSize + 2 > len) return false;
        bb_append(out, data + pos, (size_t)chunkSize);
        pos += (size_t)chunkSize + 2;
    }
}

static void free_headers(PlatformHttpHeader *headers, int count) {
    for (int i = 0; i < count; i++) {
        free(headers[i].name);
        free(headers[i].value);
    }
    free(headers);
}

static char *dup_range(const char *start, size_t len) {
    char *s = (char *)malloc(len + 1);
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

static char *trim_dup(const char *start, size_t len) {
    while (len > 0 && (start[0] == ' ' || start[0] == '\t')) {
        start++;
        len--;
    }
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) len--;
    return dup_range(start, len);
}

typedef struct {
    int status;
    bool chunked;
    long contentLength; /* -1 if absent */
    char *location;      /* malloc'd, NULL if absent */
    PlatformHttpHeader *headers;
    int headerCount;
    size_t headerBlockLen; /* bytes consumed by the status line + headers + blank line */
} ParsedResponseHead;

/* Parses the status line and headers out of `buf` (which may already
   contain some of the body too); returns false if the blank line hasn't
   arrived yet. */
static bool parse_response_head(const ByteBuf *buf, ParsedResponseHead *out) {
    const char *sep = NULL;
    for (size_t i = 0; i + 3 < buf->len; i++) {
        if (buf->data[i] == '\r' && buf->data[i + 1] == '\n' && buf->data[i + 2] == '\r' && buf->data[i + 3] == '\n') {
            sep = buf->data + i;
            break;
        }
    }
    if (!sep) return false;
    memset(out, 0, sizeof(*out));
    out->contentLength = -1;
    out->headerBlockLen = (size_t)(sep - buf->data) + 4;

    const char *line = buf->data;
    const char *lineEnd = memchr(line, '\r', buf->len);
    if (!lineEnd) return false;
    /* "HTTP/1.1 200 OK" -- skip to the first space, parse the 3-digit code. */
    const char *sp = memchr(line, ' ', (size_t)(lineEnd - line));
    if (!sp) return false;
    out->status = atoi(sp + 1);

    const char *p = lineEnd + 2;
    int cap = 8, count = 0;
    PlatformHttpHeader *headers = (PlatformHttpHeader *)malloc((size_t)cap * sizeof(PlatformHttpHeader));
    while (p < sep) {
        const char *hEnd = memchr(p, '\r', (size_t)(sep - p));
        if (!hEnd) break;
        const char *colon = memchr(p, ':', (size_t)(hEnd - p));
        if (colon) {
            char *name = trim_dup(p, (size_t)(colon - p));
            char *value = trim_dup(colon + 1, (size_t)(hEnd - colon - 1));
            if (count == cap) {
                cap *= 2;
                headers = (PlatformHttpHeader *)realloc(headers, (size_t)cap * sizeof(PlatformHttpHeader));
            }
            headers[count].name = name;
            headers[count].value = value;
            count++;
            if (ci_eq(name, "transfer-encoding") && ci_contains(value, "chunked")) {
                out->chunked = true;
            } else if (ci_eq(name, "content-length")) {
                out->contentLength = atol(value);
            } else if (ci_eq(name, "location") && !out->location) {
                out->location = dup_range(value, strlen(value));
            }
        }
        p = hEnd + 2;
    }
    out->headers = headers;
    out->headerCount = count;
    return true;
}

static void free_response_head(ParsedResponseHead *h) {
    free_headers(h->headers, h->headerCount);
    free(h->location);
}

/* One HTTP/1.1 request/response cycle, no redirect-following -- the
   caller (platform_http_request) loops this for redirects. `deadline` is
   shared across the whole redirect chain, matching "timeoutMs bounds the
   connect phase and the overall remaining read budget together". */
static bool http_once(const char *method, const ParsedUrl *u, const PlatformHttpHeader *reqHeaders, int reqHeaderCount,
                       const char *body, size_t bodyLen, double deadline, ParsedResponseHead *outHead, ByteBuf *outBody,
                       char *failReason, size_t failReasonLen) {
    double now = platform_monotonic_seconds();
    if (now >= deadline) return false;
    SockFd fd = connect_with_timeout(u->host, u->port, (int)((deadline - now) * 1000.0));
    if (fd == SOCK_INVALID) return false;

    HttpConn conn;
    conn.fd = fd;
    conn.tls = NULL;
    if (strcmp(u->scheme, "https") == 0) {
        /* Bound the handshake's own reads by the same deadline -- without
           this a connected-but-silent peer would hang the whole request,
           since the socket is blocking and recv_some hasn't set its
           per-read timeout yet. */
        double beforeHandshake = platform_monotonic_seconds();
        if (beforeHandshake >= deadline) {
            sock_close(fd);
            return false;
        }
        sock_set_recv_timeout(fd, (int)((deadline - beforeHandshake) * 1000.0));
        if (!tls_start(&conn, u->host, failReason, failReasonLen)) {
            sock_close(fd);
            return false;
        }
    }

    ByteBuf req;
    memset(&req, 0, sizeof(req));
    char line[4096];
    int n = snprintf(line, sizeof(line), "%s %s HTTP/1.1\r\n", method, u->path);
    bb_append(&req, line, (size_t)n);
    n = snprintf(line, sizeof(line), "Host: %s\r\n", u->host);
    bb_append(&req, line, (size_t)n);
    bb_append(&req, "Connection: close\r\n", strlen("Connection: close\r\n"));
    bool sawContentLength = false, sawHost = false;
    for (int i = 0; i < reqHeaderCount; i++) {
        if (ci_eq(reqHeaders[i].name, "host")) sawHost = true;
        if (ci_eq(reqHeaders[i].name, "content-length")) sawContentLength = true;
        n = snprintf(line, sizeof(line), "%s: %s\r\n", reqHeaders[i].name, reqHeaders[i].value);
        bb_append(&req, line, (size_t)n);
    }
    (void)sawHost; /* Host is always sent above regardless; harmless if duplicated */
    if (body && bodyLen > 0 && !sawContentLength) {
        n = snprintf(line, sizeof(line), "Content-Length: %zu\r\n", bodyLen);
        bb_append(&req, line, (size_t)n);
    }
    bb_append(&req, "\r\n", 2);
    if (body && bodyLen > 0) bb_append(&req, body, bodyLen);

    size_t sent = 0;
    bool sendOk = true;
    while (sent < req.len) {
        double nowSend = platform_monotonic_seconds();
        if (nowSend >= deadline) {
            sendOk = false;
            break;
        }
        long w = conn_send(&conn, req.data + sent, req.len - sent);
        if (w <= 0) {
            sendOk = false;
            break;
        }
        sent += (size_t)w;
    }
    bb_free(&req);
    if (!sendOk) {
        conn_close(&conn);
        return false;
    }

    ByteBuf raw;
    memset(&raw, 0, sizeof(raw));
    ParsedResponseHead head;
    memset(&head, 0, sizeof(head)); /* parse_response_head only fills it in once it succeeds */
    bool haveHead = false;
    while (!haveHead) {
        if (!parse_response_head(&raw, &head)) {
            if (!recv_some(&conn, &raw, deadline)) {
                bb_free(&raw);
                conn_close(&conn);
                return false;
            }
            continue;
        }
        haveHead = true;
    }

    ByteBuf outBuf;
    memset(&outBuf, 0, sizeof(outBuf));
    const char *bodyStart = raw.data + head.headerBlockLen;
    size_t bodyAvail = raw.len - head.headerBlockLen;
    bool bodyOk = true;
    if (head.chunked) {
        while (!decode_chunked(bodyStart, bodyAvail, &outBuf)) {
            outBuf.len = 0; /* decode_chunked restarts from scratch each call */
            if (!recv_some(&conn, &raw, deadline)) {
                bodyOk = false;
                break;
            }
            bodyStart = raw.data + head.headerBlockLen;
            bodyAvail = raw.len - head.headerBlockLen;
        }
    } else if (head.contentLength >= 0) {
        while (bodyAvail < (size_t)head.contentLength) {
            if (!recv_some(&conn, &raw, deadline)) break; /* short read tolerated below EOF */
            bodyStart = raw.data + head.headerBlockLen;
            bodyAvail = raw.len - head.headerBlockLen;
        }
        bb_append(&outBuf, bodyStart, bodyAvail < (size_t)head.contentLength ? bodyAvail : (size_t)head.contentLength);
    } else {
        bb_append(&outBuf, bodyStart, bodyAvail);
        while (recv_some(&conn, &raw, deadline)) {
            bodyStart = raw.data + head.headerBlockLen;
            bodyAvail = raw.len - head.headerBlockLen;
            outBuf.len = 0;
            bb_append(&outBuf, bodyStart, bodyAvail);
        }
    }
    conn_close(&conn);
    bb_free(&raw);
    if (!bodyOk) {
        bb_free(&outBuf);
        free_response_head(&head);
        return false;
    }
    *outHead = head;
    *outBody = outBuf;
    return true;
}

static bool url_join_location(const ParsedUrl *base, const char *location, ParsedUrl *out) {
    (void)base;
    /* Only absolute Location targets are supported -- see NATIVE_PLAN.md
       §9's own log entry for why a relative redirect target is treated
       as a failure rather than resolved against the base URL. */
    return parse_url(location, out);
}

#ifdef _WIN32

static wchar_t *utf8_to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w || MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n) <= 0) {
        free(w);
        return NULL;
    }
    return w;
}

static char *wide_to_utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = (char *)malloc((size_t)n);
    if (!s || WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL) <= 0) {
        free(s);
        return NULL;
    }
    return s;
}

/* Windows https://: WinHTTP runs the entire request -- TLS against the
   system trust store (verification on, with no option here that turns it
   off), redirect-following and the system proxy are all the OS's job
   rather than this file's, exactly as NATIVE_PLAN.md §3.1 intends.
   Plain http:// deliberately stays on the shared raw-socket path, so the
   behaviour the differential suite actually exercises is the same code on
   every platform. */
static void winhttp_request(const char *method, const ParsedUrl *u, const PlatformHttpHeader *headers,
                             int headerCount, const char *body, size_t bodyLen, int timeoutMs,
                             PlatformHttpResponse *out) {
    HINTERNET session = NULL, connection = NULL, request = NULL;
    wchar_t *wheaders = NULL;
    wchar_t *whost = utf8_to_wide(u->host);
    wchar_t *wpath = utf8_to_wide(u->path);
    wchar_t *wmethod = utf8_to_wide(method);
    if (!whost || !wpath || !wmethod) goto cleanup;

    session = WinHttpOpen(L"FunnyLang/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) goto cleanup;
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    connection = WinHttpConnect(session, whost, (INTERNET_PORT)u->port, 0);
    if (!connection) goto cleanup;
    request = WinHttpOpenRequest(connection, wmethod, wpath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  WINHTTP_FLAG_SECURE);
    if (!request) goto cleanup;

    for (int i = 0; i < headerCount; i++) {
        char headerLine[4096];
        snprintf(headerLine, sizeof(headerLine), "%s: %s\r\n", headers[i].name, headers[i].value);
        wchar_t *wline = utf8_to_wide(headerLine);
        if (!wline) continue;
        WinHttpAddRequestHeaders(request, wline, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        free(wline);
    }

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)(void *)body, (DWORD)bodyLen,
                             (DWORD)bodyLen, 0))
        goto cleanup;
    if (!WinHttpReceiveResponse(request, NULL)) goto cleanup;

    DWORD status = 0, statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
        goto cleanup;

    /* The raw header block already ends with the blank line
       parse_response_head looks for, so the socket path's own parser
       handles it unchanged. */
    DWORD headerBytes = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &headerBytes,
                        WINHTTP_NO_HEADER_INDEX);
    if (headerBytes > 0) {
        wheaders = (wchar_t *)malloc(headerBytes + sizeof(wchar_t));
        if (wheaders && WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                             wheaders, &headerBytes, WINHTTP_NO_HEADER_INDEX)) {
            char *utf8Headers = wide_to_utf8(wheaders);
            if (utf8Headers) {
                ByteBuf hb;
                memset(&hb, 0, sizeof(hb));
                bb_append(&hb, utf8Headers, strlen(utf8Headers));
                ParsedResponseHead head;
                memset(&head, 0, sizeof(head));
                if (parse_response_head(&hb, &head)) {
                    out->headers = head.headers;
                    out->headerCount = head.headerCount;
                    free(head.location);
                }
                bb_free(&hb);
                free(utf8Headers);
            }
        }
    }

    ByteBuf bodyBuf;
    memset(&bodyBuf, 0, sizeof(bodyBuf));
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
        char *chunk = (char *)malloc(available);
        if (!chunk) break;
        DWORD got = 0;
        if (!WinHttpReadData(request, chunk, available, &got) || got == 0) {
            free(chunk);
            break;
        }
        bb_append(&bodyBuf, chunk, got);
        free(chunk);
    }

    out->ok = true;
    out->status = (int)status;
    out->body = bodyBuf.data;
    out->bodyLen = bodyBuf.len;

cleanup:
    free(wheaders);
    free(whost);
    free(wpath);
    free(wmethod);
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
}

#endif /* _WIN32 */

PlatformHttpResponse platform_http_request(const char *method, const char *url, const PlatformHttpHeader *headers,
                                            int headerCount, const char *body, size_t bodyLen, int timeoutMs) {
    PlatformHttpResponse result;
    memset(&result, 0, sizeof(result));

    ParsedUrl u;
    if (!parse_url(url, &u)) return result;
    if (strcmp(u.scheme, "http") != 0 && strcmp(u.scheme, "https") != 0) return result;

    double deadline = platform_monotonic_seconds() + (double)timeoutMs / 1000.0;
    const char *curMethod = method;
    const char *curBody = body;
    size_t curBodyLen = bodyLen;

    for (int hop = 0; hop < 10; hop++) {
#ifdef _WIN32
        /* Checked per hop, not just up front, so an http:// -> https://
           redirect hands off to WinHTTP too instead of failing. */
        if (strcmp(u.scheme, "https") == 0) {
            winhttp_request(curMethod, &u, headers, headerCount, curBody, curBodyLen, timeoutMs, &result);
            return result;
        }
#endif
        ParsedResponseHead head;
        ByteBuf respBody;
        if (!http_once(curMethod, &u, headers, headerCount, curBody, curBodyLen, deadline, &head, &respBody,
                        result.failReason, sizeof(result.failReason))) {
            return result;
        }
        bool isRedirect = (head.status == 301 || head.status == 302 || head.status == 303 || head.status == 307 ||
                            head.status == 308);
        if (isRedirect && head.location != NULL) {
            ParsedUrl next;
            bool joined = url_join_location(&u, head.location, &next);
            free_response_head(&head);
            bb_free(&respBody);
            if (!joined || (strcmp(next.scheme, "http") != 0 && strcmp(next.scheme, "https") != 0)) return result;
            u = next;
            if (head.status == 301 || head.status == 302 || head.status == 303) {
                curMethod = "GET";
                curBody = NULL;
                curBodyLen = 0;
            }
            continue;
        }
        result.ok = true;
        result.status = head.status;
        result.headers = head.headers;
        result.headerCount = head.headerCount;
        free(head.location);
        result.body = respBody.data;
        result.bodyLen = respBody.len;
        return result;
    }
    return result; /* too many redirects */
}

void platform_http_response_free(PlatformHttpResponse *resp) {
    free_headers(resp->headers, resp->headerCount);
    free(resp->body);
    memset(resp, 0, sizeof(*resp));
}

bool platform_tcp_ping(const char *host, int port, int timeoutMs, double *outMs) {
    double start = platform_monotonic_seconds();
    SockFd fd = connect_with_timeout(host, port, timeoutMs);
    if (fd == SOCK_INVALID) return false;
    sock_close(fd);
    *outMs = (platform_monotonic_seconds() - start) * 1000.0;
    return true;
}

/* -- TLS side table: forward declarations ----------------------------------
 * Defined further down, after the listener code: each backend needs things
 * above (g_ssl), and the listener code just below needs to consult it. */
typedef struct TlsConn TlsConn;
typedef struct TlsServer TlsServer;
static TlsConn *tls_conn_find(int64_t handle);
static bool tls_attach_accepted(int64_t listener, int64_t conn, SockFd fd);
static int64_t tlsb_recv(TlsConn *c, char *buf, size_t len, int timeoutMs);
static bool tlsb_send(TlsConn *c, const char *buf, size_t len);
static bool tlsb_pending(TlsConn *c);
static void tls_forget(int64_t handle);

/* -- listening sockets ----------------------------------------------------
 *
 * See platform.h. Handles are int64_t and the two backends differ only in
 * the helpers above, which is the whole point of having them.
 */

/* SockFd is unsigned on Windows (`SOCKET` is a `UINT_PTR`), so INVALID_SOCKET
   cast to int64_t is a huge positive number rather than -1. Everything here
   normalises through these two, so a handle that crosses into FunnyLang is
   always either >= 0 or one of the named negatives. */
static int64_t sock_to_handle(SockFd fd) {
    return fd == SOCK_INVALID ? PLATFORM_SOCKET_NONE : (int64_t)fd;
}

static SockFd handle_to_sock(int64_t h) {
    return h < 0 ? SOCK_INVALID : (SockFd)h;
}

int64_t platform_tcp_listen(const char *host, int port, int backlog, char *errbuf, size_t errbuf_len) {
    ensure_winsock();
    if (errbuf != NULL && errbuf_len > 0) errbuf[0] = '\0';
    if (port < 0 || port > 65535) {
        snprintf(errbuf, errbuf_len, "%d isn't a port number.", port);
        return PLATFORM_SOCKET_NONE;
    }

    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; /* NULL host then means "every interface" */
    struct addrinfo *res = NULL;
    const char *node = (host != NULL && host[0] != '\0') ? host : NULL;
    if (getaddrinfo(node, portStr, &hints, &res) != 0 || res == NULL) {
        snprintf(errbuf, errbuf_len, "can't resolve '%s'.", node != NULL ? node : "*");
        return PLATFORM_SOCKET_NONE;
    }

    SockFd fd = SOCK_INVALID;
    char why[128];
    why[0] = '\0';
    for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == SOCK_INVALID) continue;
        /* Without SO_REUSEADDR a server restarted inside the TIME_WAIT window
           cannot rebind its own port, which makes development miserable and
           buys nothing: the socket is ours either way. */
        sock_set_reuseaddr(fd);
        if (bind(fd, rp->ai_addr, (int)rp->ai_addrlen) == 0 && listen(fd, backlog > 0 ? backlog : 16) == 0) break;
        sock_last_error(why, sizeof why);
        sock_close(fd);
        fd = SOCK_INVALID;
    }
    freeaddrinfo(res);

    if (fd == SOCK_INVALID) {
        snprintf(errbuf, errbuf_len, "couldn't listen on port %d: %s", port, why[0] != '\0' ? why : "no usable address");
        return PLATFORM_SOCKET_NONE;
    }
    /* Non-blocking, because more than one thread may be accepting from it.
       The kernel wakes every thread polling a listener and gives the
       connection to one; on a blocking socket each of the others would then
       sit in accept() until the *next* connection -- and on an `interns`
       worker that freezes every task that thread is serving. Non-blocking,
       the losers get EWOULDBLOCK and go back to their event loops. */
    sock_set_nonblocking(fd, true);
    return sock_to_handle(fd);
}

int64_t platform_tcp_accept(int64_t listener, int timeoutMs, char *peerOut, size_t peerOut_len) {
    SockFd lfd = handle_to_sock(listener);
    if (lfd == SOCK_INVALID) return PLATFORM_SOCKET_ERROR;
    if (peerOut != NULL && peerOut_len > 0) peerOut[0] = '\0';

    /* Polled rather than a blocking accept, so a server can have a loop that
       does something else between callers -- checking a shutdown flag, firing
       a timer -- instead of being parked in the kernel forever.

       The listener is non-blocking (see platform_tcp_listen), so "readable"
       followed by EWOULDBLOCK means another thread took this one. That is a
       lost race, not a failure: poll again until the deadline. */
    double deadline = timeoutMs >= 0 ? platform_monotonic_seconds() + (double)timeoutMs / 1000.0 : 0.0;
    struct sockaddr_storage addr;
    socklen_t addrLen;
    SockFd fd;
    for (;;) {
        int wait = -1;
        if (timeoutMs >= 0) {
            double left = (deadline - platform_monotonic_seconds()) * 1000.0;
            wait = left > 0.0 ? (int)left : 0;
        }
        int pr = sock_poll_readable(lfd, wait);
        if (pr == 0) return PLATFORM_SOCKET_TIMEOUT;
        if (pr < 0) return PLATFORM_SOCKET_ERROR;
        addrLen = sizeof(addr);
        fd = accept(lfd, (struct sockaddr *)&addr, &addrLen);
        if (fd != SOCK_INVALID) break;
        if (!sock_would_block()) return PLATFORM_SOCKET_ERROR;
        if (timeoutMs >= 0 && platform_monotonic_seconds() >= deadline) return PLATFORM_SOCKET_TIMEOUT;
    }
    /* Windows and the BSDs hand the listener's non-blocking flag on to the
       accepted socket and Linux does not. Say which one we mean. */
    sock_set_nonblocking(fd, false);

    if (peerOut != NULL && peerOut_len > 0) {
        char hostBuf[NI_MAXHOST];
        char servBuf[NI_MAXSERV];
        if (getnameinfo((struct sockaddr *)&addr, addrLen, hostBuf, sizeof hostBuf, servBuf, sizeof servBuf,
                        NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            snprintf(peerOut, peerOut_len, "%s:%s", hostBuf, servBuf);
        }
    }
    int64_t handle = sock_to_handle(fd);
    /* A connection from a secure listener carries a TLS session from here
       on. If one cannot be made, the caller never sees this connection. */
    if (!tls_attach_accepted(listener, handle, fd)) {
        sock_close(fd);
        return PLATFORM_SOCKET_TIMEOUT;
    }
    return handle;
}

int64_t platform_socket_recv(int64_t sock, char *buf, size_t len, int timeoutMs) {
    SockFd fd = handle_to_sock(sock);
    if (fd == SOCK_INVALID) return PLATFORM_SOCKET_ERROR;
    TlsConn *tc = tls_conn_find(sock);
    if (tc != NULL) return tlsb_recv(tc, buf, len, timeoutMs);
    if (timeoutMs >= 0) {
        int pr = sock_poll_readable(fd, timeoutMs);
        if (pr == 0) return PLATFORM_SOCKET_TIMEOUT;
        if (pr < 0) return PLATFORM_SOCKET_ERROR;
    }
    long n = sock_recv(fd, buf, len);
    if (n < 0) return PLATFORM_SOCKET_ERROR;
    return (int64_t)n; /* 0 is a clean close, and the caller wants to know */
}

bool platform_socket_send(int64_t sock, const char *buf, size_t len) {
    SockFd fd = handle_to_sock(sock);
    if (fd == SOCK_INVALID) return false;
    TlsConn *tc = tls_conn_find(sock);
    if (tc != NULL) return tlsb_send(tc, buf, len);
    size_t sent = 0;
    while (sent < len) {
        long n = sock_send(fd, buf + sent, len - sent);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

int64_t platform_tcp_connect(const char *host, int port, int timeoutMs) {
    SockFd fd = connect_with_timeout(host, port, timeoutMs);
    return sock_to_handle(fd);
}

int platform_poll_sockets(const int64_t *handles, int count, int timeoutMs, unsigned char *readyOut) {
    if (count <= 0) return 0;
    /* A TLS session can already hold decrypted bytes the socket knows nothing
       about -- the whole request arrived in one record and the library has
       it. Such a connection is ready now, whatever the socket says, and the
       poll below must not wait on anybody's behalf. */
    unsigned char *buffered = (unsigned char *)calloc((size_t)count, 1);
    int bufferedCount = 0;
    for (int i = 0; i < count; i++) {
        TlsConn *tc = tls_conn_find(handles[i]);
        if (tc != NULL && tlsb_pending(tc)) {
            buffered[i] = 1;
            bufferedCount++;
        }
    }
    if (bufferedCount > 0) timeoutMs = 0;
#ifdef _WIN32
    WSAPOLLFD *pfds = (WSAPOLLFD *)calloc((size_t)count, sizeof(WSAPOLLFD));
#else
    struct pollfd *pfds = (struct pollfd *)calloc((size_t)count, sizeof(struct pollfd));
#endif
    int live = 0;
    for (int i = 0; i < count; i++) {
        readyOut[i] = 0;
        SockFd fd = handle_to_sock(handles[i]);
        if (fd == SOCK_INVALID) continue;
        pfds[live].fd = fd;
#ifdef _WIN32
        pfds[live].events = POLLRDNORM;
#else
        pfds[live].events = POLLIN;
#endif
        pfds[live].revents = 0;
        live++;
    }
    if (live == 0) {
        free(pfds);
        free(buffered);
        return 0;
    }

#ifdef _WIN32
    int rc = WSAPoll(pfds, (ULONG)live, timeoutMs);
#else
    int rc = poll(pfds, (nfds_t)live, timeoutMs);
#endif
    if (rc < 0 && bufferedCount == 0) {
        free(pfds);
        free(buffered);
        return rc; /* an error */
    }
    if (rc <= 0) {
        free(pfds);
        for (int i = 0; i < count; i++) readyOut[i] = buffered[i];
        free(buffered);
        return bufferedCount; /* 0 is a timeout */
    }

    /* Map back, skipping the invalid handles that were not polled. Anything
       with revents at all counts as ready: a hangup or an error is something
       to notice, and the read that follows is what reports which. */
    int at = 0;
    int ready = 0;
    for (int i = 0; i < count; i++) {
        if (handle_to_sock(handles[i]) == SOCK_INVALID) continue;
        if (pfds[at].revents != 0 || buffered[i]) {
            readyOut[i] = 1;
            ready++;
        }
        at++;
    }
    free(pfds);
    free(buffered);
    return ready;
}

int platform_socket_port(int64_t sock) {
    SockFd fd = handle_to_sock(sock);
    if (fd == SOCK_INVALID) return -1;
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &len) != 0) return -1;
    if (addr.ss_family == AF_INET) return (int)ntohs(((struct sockaddr_in *)&addr)->sin_port);
    if (addr.ss_family == AF_INET6) return (int)ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
    return -1;
}

void platform_socket_close(int64_t sock) {
    SockFd fd = handle_to_sock(sock);
    /* Out of the TLS table *before* the descriptor is released, so a new
       socket that reuses the number can never find this one's session. */
    tls_forget(sock);
    if (fd != SOCK_INVALID) sock_close(fd);
}

/* -- randomness ------------------------------------------------------------ */

#ifdef _WIN32
bool platform_random_bytes(unsigned char *out, size_t n) {
    if (n > (size_t)0xFFFFFFFFu) return false;
    return BCryptGenRandom(NULL, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}
#elif defined(__APPLE__)
bool platform_random_bytes(unsigned char *out, size_t n) {
    arc4random_buf(out, n); /* the kernel's CSPRNG; cannot fail */
    return true;
}
#else
bool platform_random_bytes(unsigned char *out, size_t n) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, out + got, n - got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) {
            close(fd);
            return false;
        }
        got += (size_t)r;
    }
    close(fd);
    return true;
}
#endif

/* -- TLS on listening and dialled sockets (web_server_https PLAN.md §3) ----
 *
 * Two pieces. A side table, shared by every thread, mapping a handle to the
 * TLS state that goes with it -- a server identity for a secure listener, a
 * session for a connection. And one backend per OS behind the tlsb_*
 * functions, so everything outside the backend #if is written once.
 *
 * A session belongs to the thread that owns its connection: only that thread
 * reads, writes, handshakes or closes it. So the table's lock guards the
 * table, not the sessions, and is never held across I/O.
 */

typedef struct TlsEntry {
    int64_t handle;
    bool isServer;
    void *ptr;
    struct TlsEntry *next;
} TlsEntry;

#define TLS_BUCKETS 256
static TlsEntry *g_tlsBuckets[TLS_BUCKETS];
static PlatformMutex g_tlsLock;

#ifdef _WIN32
static INIT_ONCE g_tlsOnce = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK tls_table_init_cb(PINIT_ONCE once, PVOID param, PVOID *context) {
    (void)once;
    (void)param;
    (void)context;
    platform_mutex_init(&g_tlsLock);
    return TRUE;
}
static void tls_table_init(void) { InitOnceExecuteOnce(&g_tlsOnce, tls_table_init_cb, NULL, NULL); }
#else
static pthread_once_t g_tlsOnce = PTHREAD_ONCE_INIT;
static void tls_table_init_cb(void) { platform_mutex_init(&g_tlsLock); }
static void tls_table_init(void) { pthread_once(&g_tlsOnce, tls_table_init_cb); }
#endif

/* Win32 SOCKET values are multiples of four and POSIX descriptors are small
   consecutive integers; a multiplicative hash spreads both. */
static unsigned tls_bucket(int64_t h) {
    uint64_t x = (uint64_t)h * 0x9E3779B97F4A7C15ULL;
    return (unsigned)(x >> 56);
}

static void tls_register(int64_t h, bool isServer, void *ptr) {
    tls_table_init();
    TlsEntry *e = (TlsEntry *)malloc(sizeof(TlsEntry));
    e->handle = h;
    e->isServer = isServer;
    e->ptr = ptr;
    unsigned b = tls_bucket(h);
    platform_mutex_lock(&g_tlsLock);
    e->next = g_tlsBuckets[b];
    g_tlsBuckets[b] = e;
    platform_mutex_unlock(&g_tlsLock);
}

static void *tls_find(int64_t h, bool isServer) {
    tls_table_init();
    void *found = NULL;
    unsigned b = tls_bucket(h);
    platform_mutex_lock(&g_tlsLock);
    for (TlsEntry *e = g_tlsBuckets[b]; e != NULL; e = e->next) {
        if (e->handle == h) {
            if (e->isServer == isServer) found = e->ptr;
            break;
        }
    }
    platform_mutex_unlock(&g_tlsLock);
    return found;
}

static TlsEntry *tls_take(int64_t h) {
    tls_table_init();
    unsigned b = tls_bucket(h);
    platform_mutex_lock(&g_tlsLock);
    TlsEntry **link = &g_tlsBuckets[b];
    TlsEntry *e = *link;
    while (e != NULL && e->handle != h) {
        link = &e->next;
        e = e->next;
    }
    if (e != NULL) *link = e->next;
    platform_mutex_unlock(&g_tlsLock);
    return e;
}

/* Milliseconds until `deadline` (platform_monotonic_seconds terms), rounded
   up, never negative. */
static int tls_ms_left(double deadline) {
    double left = (deadline - platform_monotonic_seconds()) * 1000.0;
    if (left <= 0.0) return 0;
    if (left > 600000.0) return 600000;
    return (int)left + 1;
}

/* The backend, one per OS. Each defines struct TlsServer and struct TlsConn
   however it likes and implements these, plus tlsb_recv/tlsb_send/
   tlsb_pending declared further up. */
static TlsServer *tlsb_server_load(const char *pfxPath, const char *password, char *err, size_t errLen);
static void tlsb_server_free(TlsServer *s);
static TlsConn *tlsb_conn_accept(TlsServer *s, SockFd fd);
static int tlsb_handshake_step(TlsConn *c, char *err, size_t errLen);
static TlsConn *tlsb_conn_connect(SockFd fd, const char *serverName, const char *caPath, double deadline, char *err,
                                  size_t errLen);
static bool tlsb_info(TlsConn *c, char *version, size_t versionLen, char *cipher, size_t cipherLen);
static void tlsb_conn_free(TlsConn *c);

static TlsConn *tls_conn_find(int64_t handle) { return (TlsConn *)tls_find(handle, false); }

static bool tls_attach_accepted(int64_t listener, int64_t conn, SockFd fd) {
    TlsServer *s = (TlsServer *)tls_find(listener, true);
    if (s == NULL) return true; /* a plain listener: nothing to attach */
    TlsConn *c = tlsb_conn_accept(s, fd);
    if (c == NULL) return false;
    tls_register(conn, false, c);
    return true;
}

static void tls_forget(int64_t handle) {
    TlsEntry *e = tls_take(handle);
    if (e == NULL) return;
    if (e->isServer) tlsb_server_free((TlsServer *)e->ptr);
    else tlsb_conn_free((TlsConn *)e->ptr);
    free(e);
}

int64_t platform_tls_listen(const char *host, int port, int backlog, const char *pfxPath, const char *password,
                            char *errbuf, size_t errbuf_len) {
    if (errbuf != NULL && errbuf_len > 0) errbuf[0] = '\0';
    /* The identity first: a server that cannot prove who it is should not
       have bound a port it will only refuse connections on. */
    TlsServer *s = tlsb_server_load(pfxPath, password != NULL ? password : "", errbuf, errbuf_len);
    if (s == NULL) return PLATFORM_SOCKET_NONE;
    int64_t listener = platform_tcp_listen(host, port, backlog, errbuf, errbuf_len);
    if (listener == PLATFORM_SOCKET_NONE) {
        tlsb_server_free(s);
        return PLATFORM_SOCKET_NONE;
    }
    tls_register(listener, true, s);
    return listener;
}

bool platform_socket_is_tls(int64_t sock) { return tls_find(sock, false) != NULL || tls_find(sock, true) != NULL; }

int platform_tls_handshake(int64_t sock, char *errbuf, size_t errbuf_len) {
    if (errbuf != NULL && errbuf_len > 0) errbuf[0] = '\0';
    TlsConn *c = tls_conn_find(sock);
    if (c == NULL) return 1;
    return tlsb_handshake_step(c, errbuf, errbuf_len);
}

bool platform_tls_info(int64_t sock, char *version, size_t version_len, char *cipher, size_t cipher_len) {
    TlsConn *c = tls_conn_find(sock);
    if (c == NULL) return false;
    return tlsb_info(c, version, version_len, cipher, cipher_len);
}

int64_t platform_tls_connect(const char *host, int port, int timeoutMs, const char *serverName, const char *caPath,
                             char *errbuf, size_t errbuf_len) {
    if (errbuf != NULL && errbuf_len > 0) errbuf[0] = '\0';
    if (timeoutMs <= 0) timeoutMs = 10000;
    double deadline = platform_monotonic_seconds() + (double)timeoutMs / 1000.0;
    SockFd fd = connect_with_timeout(host, port, timeoutMs);
    if (fd == SOCK_INVALID) {
        snprintf(errbuf, errbuf_len, "couldn't get through to %s:%d.", host, port);
        return PLATFORM_SOCKET_NONE;
    }
    const char *name = (serverName != NULL && serverName[0] != '\0') ? serverName : host;
    TlsConn *c = tlsb_conn_connect(fd, name, caPath, deadline, errbuf, errbuf_len);
    if (c == NULL) {
        sock_close(fd);
        return PLATFORM_SOCKET_NONE;
    }
    int64_t handle = sock_to_handle(fd);
    tls_register(handle, false, c);
    return handle;
}

/* An IP literal gets no SNI (RFC 6066 forbids it) -- the name check still
   runs against the certificate's IP entries. */
static bool tls_name_is_ip(const char *name) {
    if (strchr(name, ':') != NULL) return true;
    for (const char *p = name; *p != '\0'; p++) {
        if (!(isdigit((unsigned char)*p) || *p == '.')) return false;
    }
    return true;
}

#if !defined(_WIN32) && !defined(__APPLE__)

/* Linux/BSD: the same dlopen'd OpenSSL as the client above, plus what a
   server and a pinned-CA client need. PKCS12_parse and friends live in
   libcrypto; dlsym on the libssl handle finds them, because a handle's
   lookup scope is the library *and* the dependencies it was loaded with.
   Every constant here is an ABI value, unchanged from 1.1.1 through 3.x. */

typedef struct funny_x509_st FunnyX509;
typedef struct funny_evp_pkey_st FunnyEvpPkey;
typedef struct funny_pkcs12_st FunnyPKCS12;

#define FUNNY_SSL_ERROR_WANT_READ 2
#define FUNNY_SSL_ERROR_WANT_WRITE 3
#define FUNNY_SSL_ERROR_ZERO_RETURN 6
#define FUNNY_SSL_CTRL_EXTRA_CHAIN_CERT 14
#define FUNNY_SSL_CTRL_SET_MIN_PROTO_VERSION 123
#define FUNNY_TLS1_2_VERSION 0x0303
#define FUNNY_SSL_OP_NO_COMPRESSION 0x00020000ULL
#define FUNNY_SSL_OP_CIPHER_SERVER_PREFERENCE 0x00400000ULL
#define FUNNY_SSL_OP_NO_RENEGOTIATION 0x40000000ULL
/* TLS 1.2: forward secrecy and AEAD only. TLS 1.3 suites are configured
   separately by OpenSSL and are all AEAD already. */
#define FUNNY_TLS12_CIPHERS                                                                             \
    "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:"          \
    "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305"

static struct {
    const FunnySSLMethod *(*TLS_server_method)(void);
    void (*SSL_CTX_free)(FunnySSLCtx *);
    long (*SSL_CTX_ctrl)(FunnySSLCtx *, int, long, void *);
    uint64_t (*SSL_CTX_set_options)(FunnySSLCtx *, uint64_t);
    int (*SSL_CTX_set_cipher_list)(FunnySSLCtx *, const char *);
    int (*SSL_CTX_use_certificate)(FunnySSLCtx *, FunnyX509 *);
    int (*SSL_CTX_use_PrivateKey)(FunnySSLCtx *, FunnyEvpPkey *);
    int (*SSL_CTX_check_private_key)(const FunnySSLCtx *);
    int (*SSL_CTX_load_verify_locations)(FunnySSLCtx *, const char *, const char *);
    void (*SSL_set_accept_state)(FunnySSL *);
    void (*SSL_set_connect_state)(FunnySSL *);
    int (*SSL_do_handshake)(FunnySSL *);
    int (*SSL_get_error)(const FunnySSL *, int);
    int (*SSL_pending)(const FunnySSL *);
    const char *(*SSL_get_version)(const FunnySSL *);
    const void *(*SSL_get_current_cipher)(const FunnySSL *);
    const char *(*SSL_CIPHER_get_name)(const void *);
    FunnyPKCS12 *(*d2i_PKCS12_fp)(FILE *, FunnyPKCS12 **);
    int (*PKCS12_parse)(FunnyPKCS12 *, const char *, FunnyEvpPkey **, FunnyX509 **, void **);
    void (*PKCS12_free)(FunnyPKCS12 *);
    void (*X509_free)(FunnyX509 *);
    void (*EVP_PKEY_free)(FunnyEvpPkey *);
    int (*OPENSSL_sk_num)(const void *);
    void *(*OPENSSL_sk_value)(const void *, int);
    void (*OPENSSL_sk_free)(void *);
    void (*ERR_clear_error)(void);
    unsigned long (*ERR_get_error)(void);
    void (*ERR_error_string_n)(unsigned long, char *, size_t);
    const char *(*X509_verify_cert_error_string)(long);
} g_sslx;
static bool g_sslxOk = false;
static pthread_once_t g_sslxOnce = PTHREAD_ONCE_INIT;

#define SSLX_SYM(field) (g_sslx.field = (__typeof__(g_sslx.field))dlsym(g_ssl.handle, #field))

static void sslx_load(void) {
    char ignored[256]; /* sslx_ready reports a missing library itself */
    if (!ensure_openssl(ignored, sizeof ignored)) return;
    g_sslxOk = SSLX_SYM(TLS_server_method) && SSLX_SYM(SSL_CTX_free) && SSLX_SYM(SSL_CTX_ctrl) &&
               SSLX_SYM(SSL_CTX_set_options) && SSLX_SYM(SSL_CTX_set_cipher_list) &&
               SSLX_SYM(SSL_CTX_use_certificate) && SSLX_SYM(SSL_CTX_use_PrivateKey) &&
               SSLX_SYM(SSL_CTX_check_private_key) && SSLX_SYM(SSL_CTX_load_verify_locations) &&
               SSLX_SYM(SSL_set_accept_state) && SSLX_SYM(SSL_set_connect_state) && SSLX_SYM(SSL_do_handshake) &&
               SSLX_SYM(SSL_get_error) && SSLX_SYM(SSL_pending) && SSLX_SYM(SSL_get_version) &&
               SSLX_SYM(SSL_get_current_cipher) && SSLX_SYM(SSL_CIPHER_get_name) && SSLX_SYM(d2i_PKCS12_fp) &&
               SSLX_SYM(PKCS12_parse) && SSLX_SYM(PKCS12_free) && SSLX_SYM(X509_free) && SSLX_SYM(EVP_PKEY_free) &&
               SSLX_SYM(OPENSSL_sk_num) && SSLX_SYM(OPENSSL_sk_value) && SSLX_SYM(OPENSSL_sk_free) &&
               SSLX_SYM(ERR_clear_error) && SSLX_SYM(ERR_get_error) && SSLX_SYM(ERR_error_string_n) &&
               SSLX_SYM(X509_verify_cert_error_string);
}

static bool sslx_ready(char *err, size_t errLen) {
    pthread_once(&g_sslxOnce, sslx_load);
    if (g_ssl.handle == NULL) {
        snprintf(err, errLen, "%s", NO_OPENSSL_MSG);
        return false;
    }
    if (!g_sslxOk) {
        snprintf(err, errLen, "this OpenSSL is missing something a TLS server needs (1.1.1 or newer has it all).");
        return false;
    }
    return true;
}

/* `what`, plus OpenSSL's own reason when it left one. */
static void sslx_error(char *err, size_t errLen, const char *what) {
    unsigned long code = g_sslx.ERR_get_error();
    if (code != 0) {
        char detail[256];
        g_sslx.ERR_error_string_n(code, detail, sizeof detail);
        snprintf(err, errLen, "%s (%s).", what, detail);
    } else {
        snprintf(err, errLen, "%s.", what);
    }
    g_sslx.ERR_clear_error();
}

static bool sslx_configure(FunnySSLCtx *ctx) {
    if (g_sslx.SSL_CTX_ctrl(ctx, FUNNY_SSL_CTRL_SET_MIN_PROTO_VERSION, FUNNY_TLS1_2_VERSION, NULL) != 1) return false;
    g_sslx.SSL_CTX_set_options(ctx, FUNNY_SSL_OP_NO_COMPRESSION | FUNNY_SSL_OP_NO_RENEGOTIATION |
                                        FUNNY_SSL_OP_CIPHER_SERVER_PREFERENCE);
    return g_sslx.SSL_CTX_set_cipher_list(ctx, FUNNY_TLS12_CIPHERS) == 1;
}

struct TlsServer {
    FunnySSLCtx *ctx;
};

struct TlsConn {
    FunnySSL *ssl;
    FunnySSLCtx *ownCtx; /* a client's private context; NULL for a server's session */
    SockFd fd;
    bool done;
};

static TlsServer *tlsb_server_load(const char *pfxPath, const char *password, char *err, size_t errLen) {
    if (!sslx_ready(err, errLen)) return NULL;
    FILE *f = fopen(pfxPath, "rb");
    if (f == NULL) {
        snprintf(err, errLen, "can't read the certificate file '%s': %s.", pfxPath, strerror(errno));
        return NULL;
    }
    g_sslx.ERR_clear_error();
    FunnyPKCS12 *p12 = g_sslx.d2i_PKCS12_fp(f, NULL);
    fclose(f);
    if (p12 == NULL) {
        sslx_error(err, errLen, "that certificate file isn't PKCS#12");
        return NULL;
    }
    FunnyEvpPkey *key = NULL;
    FunnyX509 *cert = NULL;
    void *chain = NULL;
    int parsed = g_sslx.PKCS12_parse(p12, password, &key, &cert, &chain);
    g_sslx.PKCS12_free(p12);
    if (!parsed || key == NULL || cert == NULL) {
        sslx_error(err, errLen, "couldn't open the PKCS#12 file -- wrong password, or no key and certificate in it");
        if (key != NULL) g_sslx.EVP_PKEY_free(key);
        if (cert != NULL) g_sslx.X509_free(cert);
        if (chain != NULL) g_sslx.OPENSSL_sk_free(chain);
        return NULL;
    }

    FunnySSLCtx *ctx = g_ssl.SSL_CTX_new(g_sslx.TLS_server_method());
    bool ok = ctx != NULL && sslx_configure(ctx) && g_sslx.SSL_CTX_use_certificate(ctx, cert) == 1 &&
              g_sslx.SSL_CTX_use_PrivateKey(ctx, key) == 1 && g_sslx.SSL_CTX_check_private_key(ctx) == 1;
    if (!ok) sslx_error(err, errLen, "couldn't set up TLS with that certificate and key");
    /* The rest of the chain, so a client that trusts only the root can build
       the path. EXTRA_CHAIN_CERT takes ownership of a certificate it accepts. */
    if (chain != NULL) {
        int n = g_sslx.OPENSSL_sk_num(chain);
        for (int i = 0; i < n; i++) {
            FunnyX509 *x = (FunnyX509 *)g_sslx.OPENSSL_sk_value(chain, i);
            if (!ok || g_sslx.SSL_CTX_ctrl(ctx, FUNNY_SSL_CTRL_EXTRA_CHAIN_CERT, 0, x) != 1) g_sslx.X509_free(x);
        }
        g_sslx.OPENSSL_sk_free(chain);
    }
    /* use_certificate/use_PrivateKey took their own references. */
    g_sslx.X509_free(cert);
    g_sslx.EVP_PKEY_free(key);
    if (!ok) {
        if (ctx != NULL) g_sslx.SSL_CTX_free(ctx);
        return NULL;
    }
    TlsServer *s = (TlsServer *)calloc(1, sizeof(TlsServer));
    s->ctx = ctx;
    return s;
}

static void tlsb_server_free(TlsServer *s) {
    if (s == NULL) return;
    g_sslx.SSL_CTX_free(s->ctx);
    free(s);
}

static TlsConn *tlsb_conn_accept(TlsServer *s, SockFd fd) {
    FunnySSL *ssl = g_ssl.SSL_new(s->ctx);
    if (ssl == NULL) return NULL;
    if (g_ssl.SSL_set_fd(ssl, (int)fd) != 1) {
        g_ssl.SSL_free(ssl);
        return NULL;
    }
    g_sslx.SSL_set_accept_state(ssl);
    /* Non-blocking for the life of the session, so a handshake step or a
       read with nothing to do returns instead of parking the thread. */
    sock_set_nonblocking(fd, true);
    TlsConn *c = (TlsConn *)calloc(1, sizeof(TlsConn));
    c->ssl = ssl;
    c->fd = fd;
    return c;
}

static int tlsb_handshake_step(TlsConn *c, char *err, size_t errLen) {
    if (c->done) return 1;
    /* WANT_WRITE means our own send buffer is full, which the caller's "wait
       until readable" cannot fix; wait for it here, briefly, instead. */
    double giveUp = platform_monotonic_seconds() + 5.0;
    for (;;) {
        g_sslx.ERR_clear_error();
        int r = g_sslx.SSL_do_handshake(c->ssl);
        if (r == 1) {
            c->done = true;
            return 1;
        }
        int e = g_sslx.SSL_get_error(c->ssl, r);
        if (e == FUNNY_SSL_ERROR_WANT_READ) return 0;
        if (e == FUNNY_SSL_ERROR_WANT_WRITE && platform_monotonic_seconds() < giveUp) {
            sock_poll_writable(c->fd, 100);
            continue;
        }
        sslx_error(err, errLen, "the TLS handshake failed");
        return -1;
    }
}

static TlsConn *tlsb_conn_connect(SockFd fd, const char *serverName, const char *caPath, double deadline, char *err,
                                  size_t errLen) {
    if (!sslx_ready(err, errLen)) return NULL;
    if (g_ssl.SSL_set1_host == NULL) {
        snprintf(err, errLen, "this OpenSSL can't check host names, so it can't verify anybody.");
        return NULL;
    }
    FunnySSLCtx *ctx = g_ssl.SSL_CTX_new(g_ssl.TLS_client_method());
    if (ctx == NULL || !sslx_configure(ctx)) {
        sslx_error(err, errLen, "couldn't set up TLS");
        if (ctx != NULL) g_sslx.SSL_CTX_free(ctx);
        return NULL;
    }
    if (caPath != NULL && caPath[0] != '\0') {
        /* Exactly this CA and nothing else: the system store is never loaded. */
        if (g_sslx.SSL_CTX_load_verify_locations(ctx, caPath, NULL) != 1) {
            sslx_error(err, errLen, "couldn't load the CA file");
            g_sslx.SSL_CTX_free(ctx);
            return NULL;
        }
    } else {
        g_ssl.SSL_CTX_set_default_verify_paths(ctx);
    }
    FunnySSL *ssl = g_ssl.SSL_new(ctx);
    if (ssl == NULL) {
        sslx_error(err, errLen, "couldn't set up TLS");
        g_sslx.SSL_CTX_free(ctx);
        return NULL;
    }
    char name[256];
    snprintf(name, sizeof name, "%s", serverName);
    if (!tls_name_is_ip(name)) g_ssl.SSL_ctrl(ssl, FUNNY_SSL_CTRL_SET_TLSEXT_HOSTNAME, FUNNY_TLSEXT_NAMETYPE_host_name, name);
    g_ssl.SSL_set1_host(ssl, name);
    g_ssl.SSL_set_verify(ssl, FUNNY_SSL_VERIFY_PEER, NULL);
    g_ssl.SSL_set_fd(ssl, (int)fd);
    g_sslx.SSL_set_connect_state(ssl);
    sock_set_nonblocking(fd, true);

    bool ok = false;
    for (;;) {
        g_sslx.ERR_clear_error();
        int r = g_sslx.SSL_do_handshake(ssl);
        if (r == 1) {
            ok = true;
            break;
        }
        int e = g_sslx.SSL_get_error(ssl, r);
        if (e == FUNNY_SSL_ERROR_WANT_READ || e == FUNNY_SSL_ERROR_WANT_WRITE) {
            int wait = tls_ms_left(deadline);
            int pr = wait <= 0 ? 0
                               : (e == FUNNY_SSL_ERROR_WANT_READ ? sock_poll_readable(fd, wait)
                                                                 : sock_poll_writable(fd, wait));
            if (pr > 0) continue;
            snprintf(err, errLen, "the TLS handshake with %s timed out.", name);
            break;
        }
        long verify = g_ssl.SSL_get_verify_result(ssl);
        if (verify != FUNNY_X509_V_OK) {
            snprintf(err, errLen, "%s's certificate didn't check out: %s.", name,
                     g_sslx.X509_verify_cert_error_string(verify));
            g_sslx.ERR_clear_error();
        } else {
            sslx_error(err, errLen, "the TLS handshake failed");
        }
        break;
    }
    if (ok && g_ssl.SSL_get_verify_result(ssl) != FUNNY_X509_V_OK) {
        snprintf(err, errLen, "%s's certificate didn't check out.", name);
        ok = false;
    }
    if (!ok) {
        g_ssl.SSL_free(ssl);
        g_sslx.SSL_CTX_free(ctx);
        return NULL;
    }
    TlsConn *c = (TlsConn *)calloc(1, sizeof(TlsConn));
    c->ssl = ssl;
    c->ownCtx = ctx;
    c->fd = fd;
    c->done = true;
    return c;
}

static int64_t tlsb_recv(TlsConn *c, char *buf, size_t len, int timeoutMs) {
    if (!c->done) return PLATFORM_SOCKET_ERROR;
    double deadline = timeoutMs >= 0 ? platform_monotonic_seconds() + (double)timeoutMs / 1000.0 : 0.0;
    int n = len > (size_t)INT_MAX ? INT_MAX : (int)len;
    for (;;) {
        g_sslx.ERR_clear_error();
        int r = g_ssl.SSL_read(c->ssl, buf, n);
        if (r > 0) return (int64_t)r;
        int e = g_sslx.SSL_get_error(c->ssl, r);
        if (e == FUNNY_SSL_ERROR_ZERO_RETURN) return 0; /* close_notify: a clean close */
        if (e == FUNNY_SSL_ERROR_WANT_READ || e == FUNNY_SSL_ERROR_WANT_WRITE) {
            int wait = -1;
            if (timeoutMs >= 0) {
                wait = tls_ms_left(deadline);
                if (wait <= 0) return PLATFORM_SOCKET_TIMEOUT;
            }
            int pr = e == FUNNY_SSL_ERROR_WANT_READ ? sock_poll_readable(c->fd, wait) : sock_poll_writable(c->fd, wait);
            if (pr == 0) return PLATFORM_SOCKET_TIMEOUT;
            if (pr < 0) return PLATFORM_SOCKET_ERROR;
            continue;
        }
        g_sslx.ERR_clear_error();
        return PLATFORM_SOCKET_ERROR;
    }
}

static bool tlsb_send(TlsConn *c, const char *buf, size_t len) {
    if (!c->done) return false;
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > (size_t)1 << 30) chunk = (size_t)1 << 30;
        g_sslx.ERR_clear_error();
        int r = g_ssl.SSL_write(c->ssl, buf + sent, (int)chunk);
        if (r > 0) {
            sent += (size_t)r;
            continue;
        }
        /* A retry must pass the same buffer and length, which it does:
           `sent` has not moved. */
        int e = g_sslx.SSL_get_error(c->ssl, r);
        if (e == FUNNY_SSL_ERROR_WANT_WRITE && sock_poll_writable(c->fd, 30000) > 0) continue;
        if (e == FUNNY_SSL_ERROR_WANT_READ && sock_poll_readable(c->fd, 30000) > 0) continue;
        g_sslx.ERR_clear_error();
        return false;
    }
    return true;
}

static bool tlsb_pending(TlsConn *c) { return c->done && g_sslx.SSL_pending(c->ssl) > 0; }

static bool tlsb_info(TlsConn *c, char *version, size_t versionLen, char *cipher, size_t cipherLen) {
    if (!c->done) return false;
    const char *v = g_sslx.SSL_get_version(c->ssl);
    const void *ci = g_sslx.SSL_get_current_cipher(c->ssl);
    const char *cn = ci != NULL ? g_sslx.SSL_CIPHER_get_name(ci) : NULL;
    snprintf(version, versionLen, "%s", v != NULL ? v : "?");
    snprintf(cipher, cipherLen, "%s", cn != NULL ? cn : "?");
    return true;
}

static void tlsb_conn_free(TlsConn *c) {
    if (c == NULL) return;
    /* close_notify, best effort: on a non-blocking socket this sends ours and
       does not wait for theirs. */
    if (c->done) g_ssl.SSL_shutdown(c->ssl);
    g_ssl.SSL_free(c->ssl);
    if (c->ownCtx != NULL) g_sslx.SSL_CTX_free(c->ownCtx);
    free(c);
}

#elif defined(_WIN32)

/* Windows: Schannel -- web_server_https PLAN.md H4. */
struct TlsServer {
    int unused;
};
struct TlsConn {
    int unused;
};
static TlsServer *tlsb_server_load(const char *pfxPath, const char *password, char *err, size_t errLen) {
    (void)pfxPath;
    (void)password;
    snprintf(err, errLen, "a TLS server isn't built for Windows yet.");
    return NULL;
}
static void tlsb_server_free(TlsServer *s) { free(s); }
static TlsConn *tlsb_conn_accept(TlsServer *s, SockFd fd) {
    (void)s;
    (void)fd;
    return NULL;
}
static int tlsb_handshake_step(TlsConn *c, char *err, size_t errLen) {
    (void)c;
    snprintf(err, errLen, "a TLS server isn't built for Windows yet.");
    return -1;
}
static TlsConn *tlsb_conn_connect(SockFd fd, const char *serverName, const char *caPath, double deadline, char *err,
                                  size_t errLen) {
    (void)fd;
    (void)serverName;
    (void)caPath;
    (void)deadline;
    (void)tls_ms_left;
    (void)tls_name_is_ip;
    snprintf(err, errLen, "a pinned-CA TLS client isn't built for Windows yet.");
    return NULL;
}
static int64_t tlsb_recv(TlsConn *c, char *buf, size_t len, int timeoutMs) {
    (void)c;
    (void)buf;
    (void)len;
    (void)timeoutMs;
    return PLATFORM_SOCKET_ERROR;
}
static bool tlsb_send(TlsConn *c, const char *buf, size_t len) {
    (void)c;
    (void)buf;
    (void)len;
    return false;
}
static bool tlsb_pending(TlsConn *c) {
    (void)c;
    return false;
}
static bool tlsb_info(TlsConn *c, char *version, size_t versionLen, char *cipher, size_t cipherLen) {
    (void)c;
    (void)version;
    (void)versionLen;
    (void)cipher;
    (void)cipherLen;
    return false;
}
static void tlsb_conn_free(TlsConn *c) { free(c); }

#else

/* macOS: Secure Transport -- web_server_https PLAN.md H5. */
struct TlsServer {
    int unused;
};
struct TlsConn {
    int unused;
};
static TlsServer *tlsb_server_load(const char *pfxPath, const char *password, char *err, size_t errLen) {
    (void)pfxPath;
    (void)password;
    snprintf(err, errLen, "a TLS server isn't built for macOS yet.");
    return NULL;
}
static void tlsb_server_free(TlsServer *s) { free(s); }
static TlsConn *tlsb_conn_accept(TlsServer *s, SockFd fd) {
    (void)s;
    (void)fd;
    return NULL;
}
static int tlsb_handshake_step(TlsConn *c, char *err, size_t errLen) {
    (void)c;
    snprintf(err, errLen, "a TLS server isn't built for macOS yet.");
    return -1;
}
static TlsConn *tlsb_conn_connect(SockFd fd, const char *serverName, const char *caPath, double deadline, char *err,
                                  size_t errLen) {
    (void)fd;
    (void)serverName;
    (void)caPath;
    (void)deadline;
    (void)tls_ms_left;
    (void)tls_name_is_ip;
    snprintf(err, errLen, "a pinned-CA TLS client isn't built for macOS yet.");
    return NULL;
}
static int64_t tlsb_recv(TlsConn *c, char *buf, size_t len, int timeoutMs) {
    (void)c;
    (void)buf;
    (void)len;
    (void)timeoutMs;
    return PLATFORM_SOCKET_ERROR;
}
static bool tlsb_send(TlsConn *c, const char *buf, size_t len) {
    (void)c;
    (void)buf;
    (void)len;
    return false;
}
static bool tlsb_pending(TlsConn *c) {
    (void)c;
    return false;
}
static bool tlsb_info(TlsConn *c, char *version, size_t versionLen, char *cipher, size_t cipherLen) {
    (void)c;
    (void)version;
    (void)versionLen;
    (void)cipher;
    (void)cipherLen;
    return false;
}
static void tlsb_conn_free(TlsConn *c) { free(c); }

#endif

/* -- threads, mutexes, condition variables (ASYNC_PLAN.md A0) -------------
 *
 * platform.h declares these as opaque fixed-size storage. The static
 * assertions below are what makes that safe: if a platform's real handle
 * does not fit, the build stops here with a readable message instead of
 * writing past the struct at run time.
 */
#ifdef _WIN32

typedef struct {
    HANDLE handle;
    PlatformThreadFn fn;
    void *userdata;
} Win32Thread;

_Static_assert(sizeof(Win32Thread) <= sizeof(PlatformThread), "PlatformThread storage too small");
_Static_assert(sizeof(SRWLOCK) <= sizeof(PlatformMutex), "PlatformMutex storage too small");
_Static_assert(sizeof(CONDITION_VARIABLE) <= sizeof(PlatformCond), "PlatformCond storage too small");

/* SRWLOCK rather than CRITICAL_SECTION: it needs no initialisation call that
   can fail, is smaller, and this code never recurses on a lock -- a
   recursive acquire would be a bug worth crashing on rather than tolerating. */
static SRWLOCK *win_mutex(PlatformMutex *m) { return (SRWLOCK *)m->opaque; }
static CONDITION_VARIABLE *win_cond(PlatformCond *c) { return (CONDITION_VARIABLE *)c->opaque; }

static DWORD WINAPI win_thread_trampoline(LPVOID param) {
    Win32Thread *t = (Win32Thread *)param;
    t->fn(t->userdata);
    return 0;
}

bool platform_thread_start(PlatformThread *thread, PlatformThreadFn fn, void *userdata,
                           char *errbuf, size_t errbuf_len) {
    Win32Thread *t = (Win32Thread *)thread->opaque;
    t->fn = fn;
    t->userdata = userdata;
    t->handle = CreateThread(NULL, 0, win_thread_trampoline, t, 0, NULL);
    if (t->handle == NULL) {
        set_errbuf_win32(errbuf, errbuf_len, GetLastError());
        return false;
    }
    return true;
}

void platform_thread_join(PlatformThread *thread) {
    Win32Thread *t = (Win32Thread *)thread->opaque;
    if (t->handle == NULL) return;
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
    t->handle = NULL;
}

uint64_t platform_thread_id(void) { return (uint64_t)GetCurrentThreadId(); }

void platform_mutex_init(PlatformMutex *m) { InitializeSRWLock(win_mutex(m)); }
void platform_mutex_destroy(PlatformMutex *m) { (void)m; /* SRWLOCK needs none */ }
void platform_mutex_lock(PlatformMutex *m) { AcquireSRWLockExclusive(win_mutex(m)); }
void platform_mutex_unlock(PlatformMutex *m) { ReleaseSRWLockExclusive(win_mutex(m)); }

void platform_cond_init(PlatformCond *c) { InitializeConditionVariable(win_cond(c)); }
void platform_cond_destroy(PlatformCond *c) { (void)c; /* likewise */ }

void platform_cond_wait(PlatformCond *c, PlatformMutex *m) {
    SleepConditionVariableSRW(win_cond(c), win_mutex(m), INFINITE, 0);
}

bool platform_cond_wait_ms(PlatformCond *c, PlatformMutex *m, int timeoutMs) {
    if (timeoutMs < 0) timeoutMs = 0;
    if (SleepConditionVariableSRW(win_cond(c), win_mutex(m), (DWORD)timeoutMs, 0)) return true;
    return GetLastError() != ERROR_TIMEOUT;
}

void platform_cond_signal(PlatformCond *c) { WakeConditionVariable(win_cond(c)); }
void platform_cond_broadcast(PlatformCond *c) { WakeAllConditionVariable(win_cond(c)); }

#else

typedef struct {
    pthread_t handle;
    PlatformThreadFn fn;
    void *userdata;
    bool started;
} PosixThread;

_Static_assert(sizeof(PosixThread) <= sizeof(PlatformThread), "PlatformThread storage too small");
_Static_assert(sizeof(pthread_mutex_t) <= sizeof(PlatformMutex), "PlatformMutex storage too small");
_Static_assert(sizeof(pthread_cond_t) <= sizeof(PlatformCond), "PlatformCond storage too small");

static pthread_mutex_t *posix_mutex(PlatformMutex *m) { return (pthread_mutex_t *)m->opaque; }
static pthread_cond_t *posix_cond(PlatformCond *c) { return (pthread_cond_t *)c->opaque; }

static void *posix_thread_trampoline(void *param) {
    PosixThread *t = (PosixThread *)param;
    t->fn(t->userdata);
    return NULL;
}

bool platform_thread_start(PlatformThread *thread, PlatformThreadFn fn, void *userdata,
                           char *errbuf, size_t errbuf_len) {
    PosixThread *t = (PosixThread *)thread->opaque;
    t->fn = fn;
    t->userdata = userdata;
    t->started = false;
    int rc = pthread_create(&t->handle, NULL, posix_thread_trampoline, t);
    if (rc != 0) {
        set_errbuf(errbuf, errbuf_len, rc); /* pthread_create returns the errno itself */
        return false;
    }
    t->started = true;
    return true;
}

void platform_thread_join(PlatformThread *thread) {
    PosixThread *t = (PosixThread *)thread->opaque;
    if (!t->started) return;
    pthread_join(t->handle, NULL);
    t->started = false;
}

/* pthread_t is opaque and not required to be an integer, so it is hashed
   rather than cast. Only ever compared for equality, never interpreted. */
uint64_t platform_thread_id(void) {
    pthread_t self = pthread_self();
    uint64_t h = 1469598103934665603ULL;
    const unsigned char *bytes = (const unsigned char *)&self;
    for (size_t i = 0; i < sizeof self; i++) {
        h ^= bytes[i];
        h *= 1099511628211ULL;
    }
    return h;
}

void platform_mutex_init(PlatformMutex *m) { pthread_mutex_init(posix_mutex(m), NULL); }
void platform_mutex_destroy(PlatformMutex *m) { pthread_mutex_destroy(posix_mutex(m)); }
void platform_mutex_lock(PlatformMutex *m) { pthread_mutex_lock(posix_mutex(m)); }
void platform_mutex_unlock(PlatformMutex *m) { pthread_mutex_unlock(posix_mutex(m)); }

void platform_cond_init(PlatformCond *c) { pthread_cond_init(posix_cond(c), NULL); }
void platform_cond_destroy(PlatformCond *c) { pthread_cond_destroy(posix_cond(c)); }

void platform_cond_wait(PlatformCond *c, PlatformMutex *m) {
    pthread_cond_wait(posix_cond(c), posix_mutex(m));
}

/* CLOCK_REALTIME, not monotonic: pthread_cond_timedwait's deadline is
   against the condvar's clock attribute, which defaults to CLOCK_REALTIME on
   both Linux and macOS. Using a monotonic deadline here without also setting
   the attribute would make every wait return immediately. */
bool platform_cond_wait_ms(PlatformCond *c, PlatformMutex *m, int timeoutMs) {
    if (timeoutMs < 0) timeoutMs = 0;
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeoutMs / 1000;
    deadline.tv_nsec += (long)(timeoutMs % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(posix_cond(c), posix_mutex(m), &deadline) != ETIMEDOUT;
}

void platform_cond_signal(PlatformCond *c) { pthread_cond_signal(posix_cond(c)); }
void platform_cond_broadcast(PlatformCond *c) { pthread_cond_broadcast(posix_cond(c)); }

#endif
