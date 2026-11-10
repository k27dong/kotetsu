/* files: read whole, and replaced in one step */
#include "shared/file.h"

#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static constexpr size_t PATH_BYTES = 4'096; // the longest path Linux takes

bool file_read(const char *path, void *out, size_t size, size_t *length) {
    /* read up to size bytes */
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    const size_t count = fread(out, 1, size, file);
    const bool larger = count == size && fgetc(file) != EOF;
    const bool failed = ferror(file) != 0;
    const int reason = errno;
    fclose(file);

    /* check it was all read */
    if (larger || failed) {
        errno = larger ? EFBIG : reason;
        return false;
    }
    *length = count;
    return true;
}

static bool sync_directory_of(const char *path) {
    /* find the directory */
    char directory[PATH_BYTES] = "."; // a bare name's directory
    const char *slash = strrchr(path, '/');
    if (slash != nullptr) {
        const size_t length = slash == path ? 1 : (size_t)(slash - path); // keep the root's slash
        if (length >= sizeof directory) {
            errno = ENAMETOOLONG;
            return false;
        }
        memcpy(directory, path, length);
        directory[length] = '\0';
    }

    /* sync its entries */
    DIR *opened = opendir(directory);
    if (opened == nullptr) {
        return false;
    }
    const int handle = dirfd(opened);
    const bool synced = handle >= 0 && fsync(handle) == 0;
    const int reason = errno;
    closedir(opened);
    errno = reason;
    return synced;
}

bool file_write_atomic(const char *path, const void *data, size_t length) {
    /* open a temporary file */
    char temporary[PATH_BYTES];
    const int printed = snprintf(temporary, sizeof temporary, "%s.XXXXXX", path);
    if (printed < 0 || (size_t)printed >= sizeof temporary) {
        errno = ENAMETOOLONG;
        return false;
    }
    const int file = mkstemp(temporary); // beside the target, as rename can't cross file systems
    if (file < 0) {
        return false;
    }

    /* write and flush it */
    const uint8_t *bytes = data;
    size_t sent = 0;
    while (sent < length) {
        const ssize_t written = write(file, bytes + sent, length - sent);
        if (written <= 0) {
            break;
        }
        sent += (size_t)written;
    }
    const bool flushed = sent == length && fsync(file) == 0;
    const int write_error = errno;
    const bool closed = close(file) == 0;

    /* replace the target */
    if (!flushed || !closed || rename(temporary, path) != 0) {
        const int reason = flushed ? errno : write_error;
        unlink(temporary);
        errno = reason;
        return false;
    }

    /* sync the directory */
    return sync_directory_of(path); // a power cut could otherwise bring the old file back
}
