#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    char* output;
    size_t capacity;
    size_t length;
} FormatBuffer;

static void append_character(FormatBuffer* buffer, char character) {
    if (buffer->capacity > 0 && buffer->length + 1U < buffer->capacity) {
        buffer->output[buffer->length] = character;
    }
    buffer->length++;
}

static void append_string(FormatBuffer* buffer, const char* string) {
    if (!string) string = "(null)";
    while (*string) append_character(buffer, *string++);
}

static void append_unsigned(FormatBuffer* buffer, uint64_t value,
                            unsigned base, int uppercase) {
    char digits[32];
    size_t length = 0;
    const char* alphabet = uppercase
        ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        digits[length++] = alphabet[value % base];
        value /= base;
    } while (value != 0);
    while (length > 0) append_character(buffer, digits[--length]);
}

int vsnprintf(char* output, size_t capacity, const char* format, va_list arguments) {
    FormatBuffer buffer = { .output = output, .capacity = capacity };
    while (*format) {
        if (*format++ != '%') {
            append_character(&buffer, format[-1]);
            continue;
        }
        if (*format == '%') {
            append_character(&buffer, *format++);
            continue;
        }
        while (*format == '-' || *format == '+' || *format == ' '
               || *format == '#' || *format == '0') format++;
        while (*format >= '0' && *format <= '9') format++;
        if (*format == '.') {
            format++;
            while (*format >= '0' && *format <= '9') format++;
        }
        int long_value = 0;
        while (*format == 'l' || *format == 'j' || *format == 'z'
               || *format == 't') {
            long_value = 1;
            format++;
        }
        char conversion = *format ? *format++ : '\0';
        if (conversion == 's') {
            append_string(&buffer, va_arg(arguments, const char*));
        } else if (conversion == 'c') {
            append_character(&buffer, (char)va_arg(arguments, int));
        } else if (conversion == 'd' || conversion == 'i') {
            int64_t value = long_value
                ? va_arg(arguments, int64_t) : va_arg(arguments, int);
            uint64_t magnitude = value < 0 ? 0ULL - (uint64_t)value : (uint64_t)value;
            if (value < 0) append_character(&buffer, '-');
            append_unsigned(&buffer, magnitude, 10, 0);
        } else if (conversion == 'u' || conversion == 'x'
                   || conversion == 'X' || conversion == 'o') {
            uint64_t value = long_value
                ? va_arg(arguments, uint64_t) : va_arg(arguments, unsigned);
            unsigned base = conversion == 'o' ? 8U
                : conversion == 'u' ? 10U : 16U;
            append_unsigned(&buffer, value, base, conversion == 'X');
        } else if (conversion == 'p') {
            append_string(&buffer, "0x");
            append_unsigned(&buffer, (uintptr_t)va_arg(arguments, void*), 16, 0);
        } else if (conversion != '\0') {
            append_character(&buffer, '%');
            append_character(&buffer, conversion);
        }
    }
    if (capacity > 0) {
        size_t terminal = buffer.length < capacity ? buffer.length : capacity - 1U;
        output[terminal] = '\0';
    }
    return (int)buffer.length;
}
