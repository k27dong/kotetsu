#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>

#include "unity.h"

// Unity's assertions only work on the test's own thread, so the server thread uses g_assert_true.

struct fake_route {
    const char *path_prefix;
    int status;
    const char *fixture; // a file under fixtures/, or nullptr for no file
    size_t filler_bytes; // without a file, a body of this many bytes
};

struct fake_server {
    int listener;
    int port;
    char base[32];
    const struct fake_route *routes;
    size_t route_count;
    GThread *thread;
    gint stopping;
    GMutex lock;
    GPtrArray *requests; // GString *, head and body exactly as received
    bool chunked;        // bodies go out with Transfer-Encoding: chunked instead of a length
};

static constexpr size_t FAKE_CHUNK_BYTES = 64 * 1024;

[[maybe_unused]] static int fake_listen(int *port) {
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_NOT_EQUAL_INT(-1, listener);
    struct sockaddr_in address = {.sin_family = AF_INET};
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    TEST_ASSERT_EQUAL_INT(0, bind(listener, (struct sockaddr *)&address, sizeof address));
    TEST_ASSERT_EQUAL_INT(0, listen(listener, 8));
    socklen_t length = sizeof address;
    TEST_ASSERT_EQUAL_INT(0, getsockname(listener, (struct sockaddr *)&address, &length));
    *port = ntohs(address.sin_port);
    return listener;
}

[[maybe_unused]] static size_t fake_content_length(const char *head, size_t head_length) {
    gchar *copy = g_strndup(head, head_length);
    gchar **lines = g_strsplit(copy, "\r\n", -1);
    size_t length = 0;
    for (gchar **line = lines; *line != nullptr; line++) {
        if (g_ascii_strncasecmp(*line, "Content-Length:", strlen("Content-Length:")) == 0) {
            length = (size_t)g_ascii_strtoull(*line + strlen("Content-Length:"), nullptr, 10);
        }
    }
    g_strfreev(lines);
    g_free(copy);
    return length;
}

[[maybe_unused]] static GString *fake_read_request(int connection) {
    GString *request = g_string_new(nullptr);
    char buffer[4096];
    for (;;) {
        const ssize_t received = recv(connection, buffer, sizeof buffer, 0);
        if (received <= 0) {
            break;
        }
        g_string_append_len(request, buffer, received);
        const char *head_end = g_strstr_len(request->str, (gssize)request->len, "\r\n\r\n");
        if (head_end != nullptr) {
            const size_t head_length = (size_t)(head_end - request->str) + 4;
            if (request->len >= head_length + fake_content_length(request->str, head_length)) {
                break;
            }
        }
    }
    return request;
}

[[maybe_unused]] static void fake_send_all(int connection, const char *data, size_t length) {
    while (length > 0) {
        const ssize_t sent = send(connection, data, length, 0);
        if (sent <= 0) {
            return; // the client hung up, as it should on a body over its limit
        }
        data += sent;
        length -= (size_t)sent;
    }
}

[[maybe_unused]] static void fake_respond(const struct fake_server *server, int connection,
                                          const GString *request) {
    const char *path = strchr(request->str, ' ');
    g_assert_nonnull(path);
    path++;
    const struct fake_route *route = nullptr;
    for (size_t i = 0; i < server->route_count && route == nullptr; i++) {
        if (g_str_has_prefix(path, server->routes[i].path_prefix)) {
            route = &server->routes[i];
        }
    }
    gchar *body = nullptr;
    gsize length = 0;
    const int status = route == nullptr ? 404 : route->status;
    if (route != nullptr && route->fixture != nullptr) {
        g_assert_true(g_file_get_contents(route->fixture, &body, &length, nullptr));
    } else if (route != nullptr && route->filler_bytes > 0) {
        length = route->filler_bytes;
        body = g_malloc(length);
        memset(body, 'x', length);
    }
    if (server->chunked) {
        gchar *head = g_strdup_printf("HTTP/1.1 %d Fake\r\nContent-Type: application/json\r\n"
                                      "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n",
                                      status);
        fake_send_all(connection, head, strlen(head));
        for (size_t sent = 0; sent < length; sent += FAKE_CHUNK_BYTES) {
            const size_t chunk = MIN(FAKE_CHUNK_BYTES, length - sent);
            gchar *size_line = g_strdup_printf("%zx\r\n", chunk);
            fake_send_all(connection, size_line, strlen(size_line));
            fake_send_all(connection, body + sent, chunk);
            fake_send_all(connection, "\r\n", 2);
            g_free(size_line);
        }
        fake_send_all(connection, "0\r\n\r\n", 5);
        g_free(head);
    } else {
        gchar *head = g_strdup_printf("HTTP/1.1 %d Fake\r\nContent-Type: application/json\r\n"
                                      "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                                      status, (size_t)length);
        fake_send_all(connection, head, strlen(head));
        fake_send_all(connection, body, length);
        g_free(head);
    }
    g_free(body);
}

[[maybe_unused]] static gpointer fake_serve(gpointer data) {
    struct fake_server *server = data;
    while (!g_atomic_int_get(&server->stopping)) {
        struct pollfd waiting = {.fd = server->listener, .events = POLLIN};
        if (poll(&waiting, 1, 50) <= 0) {
            continue;
        }
        const int connection = accept(server->listener, nullptr, nullptr);
        g_assert_true(connection >= 0);
        GString *request = fake_read_request(connection);
        g_mutex_lock(&server->lock);
        g_ptr_array_add(server->requests, request);
        g_mutex_unlock(&server->lock);
        fake_respond(server, connection, request);
        close(connection);
    }
    return nullptr;
}

[[maybe_unused]] static void fake_free_request(gpointer request) {
    g_string_free(request, TRUE);
}

[[maybe_unused]] static void fake_server_launch(struct fake_server *server,
                                                const struct fake_route *routes, size_t count,
                                                bool chunked) {
    signal(SIGPIPE, SIG_IGN); // a client that gave up must not kill the test
    *server = (struct fake_server){.routes = routes, .route_count = count, .chunked = chunked};
    server->listener = fake_listen(&server->port);
    snprintf(server->base, sizeof server->base, "http://127.0.0.1:%d", server->port);
    server->requests = g_ptr_array_new_with_free_func(fake_free_request);
    g_mutex_init(&server->lock);
    server->thread = g_thread_new("fake-server", fake_serve, server);
}

[[maybe_unused]] static void fake_server_start(struct fake_server *server,
                                               const struct fake_route *routes, size_t count) {
    fake_server_launch(server, routes, count, false);
}

[[maybe_unused]] static void fake_server_start_chunked(struct fake_server *server,
                                                       const struct fake_route *routes,
                                                       size_t count) {
    fake_server_launch(server, routes, count, true);
}

[[maybe_unused]] static void fake_server_stop(struct fake_server *server) {
    g_atomic_int_set(&server->stopping, 1);
    g_thread_join(server->thread);
    close(server->listener);
    g_ptr_array_unref(server->requests);
    g_mutex_clear(&server->lock);
}

[[maybe_unused]] static size_t fake_server_request_count(struct fake_server *server) {
    g_mutex_lock(&server->lock);
    const size_t count = server->requests->len;
    g_mutex_unlock(&server->lock);
    return count;
}

[[maybe_unused]] static const GString *fake_server_request(struct fake_server *server,
                                                           size_t index) {
    g_mutex_lock(&server->lock);
    const GString *request =
            index < server->requests->len ? g_ptr_array_index(server->requests, index) : nullptr;
    g_mutex_unlock(&server->lock);
    TEST_ASSERT_NOT_NULL_MESSAGE(request, "the server got fewer requests");
    return request;
}

[[maybe_unused]] static void fake_assert_request_line(struct fake_server *server, size_t index,
                                                      const char *expected) {
    const GString *request = fake_server_request(server, index);
    const char *line_end = strstr(request->str, "\r\n");
    TEST_ASSERT_NOT_NULL(line_end);
    gchar *line = g_strndup(request->str, (gsize)(line_end - request->str));
    TEST_ASSERT_EQUAL_STRING(expected, line);
    g_free(line);
}
