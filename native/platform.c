/* native/platform.c -- see platform.h. Feature-test macros come first,
 * before any header, to pull POSIX.1-2008 (realpath, strdup, mkdir,
 * rmdir, unlink, opendir/readdir/closedir, getcwd) back into scope under
 * `-std=c11`, which otherwise hides them exactly the way it hid <math.h>'s
 * M_PI/M_E for mafs.c -- the one exception build.sh's -std=c11 allows,
 * confined to this file per ARCHITECTURE.md's platform-boundary rule. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "platform.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static void set_errbuf(char *errbuf, size_t n, int err) {
    if (!errbuf || n == 0) return;
    snprintf(errbuf, n, "%s", strerror(err));
}

bool platform_read_file(const char *path, unsigned char **out_data, size_t *out_len, char *errbuf,
                         size_t errbuf_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        set_errbuf(errbuf, errbuf_len, errno);
        return false;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        int e = errno;
        close(fd);
        set_errbuf(errbuf, errbuf_len, e);
        return false;
    }
    if (S_ISDIR(st.st_mode)) {
        close(fd);
        set_errbuf(errbuf, errbuf_len, EISDIR);
        return false;
    }
    size_t size = (size_t)st.st_size;
    unsigned char *buf = (unsigned char *)malloc(size + 1);
    size_t total = 0;
    while (total < size) {
        ssize_t n = read(fd, buf + total, size - total);
        if (n < 0) {
            int e = errno;
            free(buf);
            close(fd);
            set_errbuf(errbuf, errbuf_len, e);
            return false;
        }
        if (n == 0) break; /* file shrank underneath us mid-read */
        total += (size_t)n;
    }
    close(fd);
    buf[total] = '\0';
    *out_data = buf;
    *out_len = total;
    return true;
}

static bool write_impl(const char *path, const unsigned char *data, size_t len, int flags, char *errbuf,
                        size_t errbuf_len) {
    int fd = open(path, flags, 0644);
    if (fd < 0) {
        set_errbuf(errbuf, errbuf_len, errno);
        return false;
    }
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, data + total, len - total);
        if (n < 0) {
            int e = errno;
            close(fd);
            set_errbuf(errbuf, errbuf_len, e);
            return false;
        }
        total += (size_t)n;
    }
    if (close(fd) != 0) {
        set_errbuf(errbuf, errbuf_len, errno);
        return false;
    }
    return true;
}

bool platform_write_file(const char *path, const unsigned char *data, size_t len, char *errbuf,
                          size_t errbuf_len) {
    return write_impl(path, data, len, O_WRONLY | O_CREAT | O_TRUNC, errbuf, errbuf_len);
}

bool platform_append_file(const char *path, const unsigned char *data, size_t len, char *errbuf,
                           size_t errbuf_len) {
    return write_impl(path, data, len, O_WRONLY | O_CREAT | O_APPEND, errbuf, errbuf_len);
}

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

bool platform_mkdir_p(const char *path, char *errbuf, size_t errbuf_len) {
    size_t len = strlen(path);
    char buf[4096];
    if (len == 0 || len >= sizeof(buf)) {
        set_errbuf(errbuf, errbuf_len, ENAMETOOLONG);
        return false;
    }
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i <= len; i++) {
        if (buf[i] != '/' && i != len) continue;
        char saved = buf[i];
        buf[i] = '\0';
        if (buf[0] != '\0') {
            if (mkdir(buf, 0755) != 0) {
                if (errno == EEXIST) {
                    struct stat st;
                    if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) {
                        set_errbuf(errbuf, errbuf_len, EEXIST);
                        return false;
                    }
                } else {
                    set_errbuf(errbuf, errbuf_len, errno);
                    return false;
                }
            }
        }
        buf[i] = saved;
    }
    return true;
}

static int cmp_cstr(const void *a, const void *b) {
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;
    return strcmp(*sa, *sb);
}

bool platform_list_dir(const char *path, char ***out_names, size_t *out_count, char *errbuf,
                        size_t errbuf_len) {
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

double platform_now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

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

uint64_t platform_ram_bytes(void) {
    long pageSize = sysconf(_SC_PAGESIZE);
    long pages = sysconf(_SC_PHYS_PAGES);
    if (pageSize <= 0 || pages <= 0) return 0;
    return (uint64_t)pageSize * (uint64_t)pages;
}

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

int platform_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 0;
}

/* -- networking -------------------------------------------------------- */

bool platform_net_disabled(void) {
    const char *v = getenv("FUNNY_NO_NET");
    return v != NULL && strcmp(v, "1") == 0;
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

static int connect_with_timeout(const char *host, int port, int timeoutMs) {
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portStr, &hints, &res) != 0 || !res) return -1;
    int fd = -1;
    for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        bool connected = false;
        if (rc == 0) {
            connected = true;
        } else if (errno == EINPROGRESS) {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            int pr = poll(&pfd, 1, timeoutMs);
            if (pr > 0) {
                int soErr = 0;
                socklen_t elen = sizeof(soErr);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &elen) == 0 && soErr == 0) connected = true;
            }
        }
        if (connected) {
            fcntl(fd, F_SETFL, flags); /* back to blocking for the data phase */
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static bool set_recv_timeout(int fd, int ms) {
    if (ms < 1) ms = 1;
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

/* Reads whatever's available into `buf`, bounded by `deadline` (an
   absolute platform_monotonic_seconds() time). Returns false on timeout,
   error, or a clean EOF (0 bytes read) -- callers distinguish "EOF" from
   "error" by checking how much of the response they'd already parsed. */
static bool recv_some(int fd, ByteBuf *buf, double deadline) {
    double now = platform_monotonic_seconds();
    if (now >= deadline) return false;
    set_recv_timeout(fd, (int)((deadline - now) * 1000.0));
    char tmp[8192];
    ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
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
            if (strcasecmp(name, "transfer-encoding") == 0 && strcasestr(value, "chunked") != NULL) {
                out->chunked = true;
            } else if (strcasecmp(name, "content-length") == 0) {
                out->contentLength = atol(value);
            } else if (strcasecmp(name, "location") == 0 && !out->location) {
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
                       const char *body, size_t bodyLen, double deadline, ParsedResponseHead *outHead, ByteBuf *outBody) {
    double now = platform_monotonic_seconds();
    if (now >= deadline) return false;
    int fd = connect_with_timeout(u->host, u->port, (int)((deadline - now) * 1000.0));
    if (fd < 0) return false;

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
        if (strcasecmp(reqHeaders[i].name, "host") == 0) sawHost = true;
        if (strcasecmp(reqHeaders[i].name, "content-length") == 0) sawContentLength = true;
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
        ssize_t w = send(fd, req.data + sent, req.len - sent, 0);
        if (w <= 0) {
            sendOk = false;
            break;
        }
        sent += (size_t)w;
    }
    bb_free(&req);
    if (!sendOk) {
        close(fd);
        return false;
    }

    ByteBuf raw;
    memset(&raw, 0, sizeof(raw));
    ParsedResponseHead head;
    bool haveHead = false;
    while (!haveHead) {
        if (!parse_response_head(&raw, &head)) {
            if (!recv_some(fd, &raw, deadline)) {
                bb_free(&raw);
                close(fd);
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
            if (!recv_some(fd, &raw, deadline)) {
                bodyOk = false;
                break;
            }
            bodyStart = raw.data + head.headerBlockLen;
            bodyAvail = raw.len - head.headerBlockLen;
        }
    } else if (head.contentLength >= 0) {
        while (bodyAvail < (size_t)head.contentLength) {
            if (!recv_some(fd, &raw, deadline)) break; /* short read tolerated below EOF */
            bodyStart = raw.data + head.headerBlockLen;
            bodyAvail = raw.len - head.headerBlockLen;
        }
        bb_append(&outBuf, bodyStart, bodyAvail < (size_t)head.contentLength ? bodyAvail : (size_t)head.contentLength);
    } else {
        bb_append(&outBuf, bodyStart, bodyAvail);
        while (recv_some(fd, &raw, deadline)) {
            bodyStart = raw.data + head.headerBlockLen;
            bodyAvail = raw.len - head.headerBlockLen;
            outBuf.len = 0;
            bb_append(&outBuf, bodyStart, bodyAvail);
        }
    }
    close(fd);
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

PlatformHttpResponse platform_http_request(const char *method, const char *url, const PlatformHttpHeader *headers,
                                            int headerCount, const char *body, size_t bodyLen, int timeoutMs) {
    PlatformHttpResponse result;
    memset(&result, 0, sizeof(result));

    ParsedUrl u;
    if (!parse_url(url, &u)) return result;
    if (strcmp(u.scheme, "http") != 0) return result; /* https:// -- N5b's job */

    double deadline = platform_monotonic_seconds() + (double)timeoutMs / 1000.0;
    const char *curMethod = method;
    const char *curBody = body;
    size_t curBodyLen = bodyLen;

    for (int hop = 0; hop < 10; hop++) {
        ParsedResponseHead head;
        ByteBuf respBody;
        if (!http_once(curMethod, &u, headers, headerCount, curBody, curBodyLen, deadline, &head, &respBody)) {
            return result;
        }
        bool isRedirect = (head.status == 301 || head.status == 302 || head.status == 303 || head.status == 307 ||
                            head.status == 308);
        if (isRedirect && head.location != NULL) {
            ParsedUrl next;
            bool joined = url_join_location(&u, head.location, &next);
            free_response_head(&head);
            bb_free(&respBody);
            if (!joined || strcmp(next.scheme, "http") != 0) return result;
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
    int fd = connect_with_timeout(host, port, timeoutMs);
    if (fd < 0) return false;
    close(fd);
    *outMs = (platform_monotonic_seconds() - start) * 1000.0;
    return true;
}
