#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    FILE opaque;
    int descriptor;
    int error;
} StandardStream;

static StandardStream g_streams[3] = {
    { .descriptor = 0 }, { .descriptor = 1 }, { .descriptor = 2 },
};
FILE* stdin = &g_streams[0].opaque;
FILE* stdout = &g_streams[1].opaque;
FILE* stderr = &g_streams[2].opaque;

// Only standard streams are currently constructed by Silt. FILE remains
// opaque: no newlib stream member or reentrancy object is consulted.
static StandardStream* stream_state(FILE* stream) {
    for (unsigned index = 0; index < 3; index++) {
        if (stream == &g_streams[index].opaque) return &g_streams[index];
    }
    abort();
}

int fileno(FILE* stream) {
    if (stream_state(stream)->descriptor < 0) { errno = EBADF; return -1; }
    return stream_state(stream)->descriptor;
}

int ferror(FILE* stream) { return stream_state(stream)->error != 0; }
void clearerr(FILE* stream) { stream_state(stream)->error = 0; }

int fflush(FILE* stream) {
    // There is no pending buffer. A prior write error remains sticky, but
    // does not itself constitute a new fflush failure.
    if (stream && stream_state(stream)->descriptor < 0) { errno = EBADF; return EOF; }
    return 0;
}

int fclose(FILE* stream) {
    int result = close(stream_state(stream)->descriptor);
    stream_state(stream)->descriptor = -1;
    if (result < 0) { stream_state(stream)->error = 1; return EOF; }
    return 0;
}

size_t fwrite(const void* data, size_t size, size_t count, FILE* stream) {
    if (size == 0 || count == 0) return 0;
    if (count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        stream_state(stream)->error = 1;
        return 0;
    }
    size_t bytes = size * count;
    size_t written = 0;
    while (written < bytes) {
        ssize_t result = write(stream_state(stream)->descriptor, (const char*)data + written, bytes - written);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) {
            if (result == 0) errno = EIO;
            stream_state(stream)->error = 1;
            break;
        }
        written += (size_t)result;
    }
    return written / size;
}

int fputc(int character, FILE* stream) {
    unsigned char byte = (unsigned char)character;
    return fwrite(&byte, 1, 1, stream) == 1 ? byte : EOF;
}

int putchar(int character) { return fputc(character, stdout); }
int fputs(const char* text, FILE* stream) {
    size_t size = strlen(text);
    return fwrite(text, 1, size, stream) == size ? 0 : EOF;
}

int puts(const char* text) {
    if (fputs(text, stdout) == EOF) return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}

int vfprintf(FILE* stream, const char* format, va_list arguments) {
    va_list copy;
    va_copy(copy, arguments);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0) { stream_state(stream)->error = 1; return -1; }
    char local[256];
    char* output = (size_t)length < sizeof(local) ? local : malloc((size_t)length + 1U);
    if (!output) { errno = ENOMEM; stream_state(stream)->error = 1; return -1; }
    vsnprintf(output, (size_t)length + 1U, format, arguments);
    size_t written = fwrite(output, 1, (size_t)length, stream);
    if (output != local) free(output);
    return written == (size_t)length ? length : -1;
}

int fprintf(FILE* stream, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int printf(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
}
