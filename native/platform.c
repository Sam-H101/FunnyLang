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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
