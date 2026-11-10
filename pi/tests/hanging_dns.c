/* test_http.c preloads this so one host's lookup hangs like a dead DNS server */
#include <netdb.h>
#include <string.h>
#include <time.h>

static void hang_on_test_host(const char *node) {
    if (node != nullptr && strcmp(node, "dns-hangs.kotetsu.test") == 0) {
        const struct timespec nap = {.tv_sec = 12};
        nanosleep(&nap, nullptr);
    }
}

#ifdef __APPLE__

static int hanging_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                               struct addrinfo **result) {
    hang_on_test_host(node);
    return getaddrinfo(node, service, hints, result);
}

[[gnu::used, gnu::section("__DATA,__interpose")]] static const struct {
    const void *replacement;
    const void *replacee;
} INTERPOSED = {(const void *)hanging_getaddrinfo, (const void *)getaddrinfo};

#else

#include <dlfcn.h>

typedef int lookup(const char *, const char *, const struct addrinfo *, struct addrinfo **);

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                struct addrinfo **result) {
    hang_on_test_host(node);
    lookup *const real = (lookup *)dlsym(RTLD_NEXT, "getaddrinfo"); // skips our preloaded copy
    return real(node, service, hints, result);
}

#endif
