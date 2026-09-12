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
        int left = 0;
        while (*format == '-' || *format == '+' || *format == ' '
               || *format == '#' || *format == '0') {
            if (*format == '-') left = 1;
            format++;
        }
        int width = 0;
        if (*format == '*') {
            width = va_arg(arguments, int);
            format++;
            if (width < 0) { left = 1; width = width == INT32_MIN ? INT32_MAX : -width; }
        } else {
            while (*format >= '0' && *format <= '9') {
                if (width < 100000) width = width * 10 + (*format - '0');
                format++;
            }
        }
        int precision = -1;
        if (*format == '.') {
            format++;
            precision = 0;
            if (*format == '*') { precision = va_arg(arguments, int); format++; }
            else while (*format >= '0' && *format <= '9') {
                if (precision < 100000) precision = precision * 10 + (*format - '0');
                format++;
            }
        }
        int long_value = 0;
        while (*format == 'l' || *format == 'j' || *format == 'z'
               || *format == 't') {
            long_value = 1;
            format++;
        }
        char conversion = *format ? *format++ : '\0';
        if (conversion == 's') {
            const char* value = va_arg(arguments, const char*);
            if (!value) value = "(null)";
            size_t length = 0;
            while ((precision < 0 || length < (size_t)precision) && value[length]) length++;
            size_t padding = width > 0 && (size_t)width > length ? (size_t)width - length : 0;
            if (!left) for (size_t index = 0; index < padding; index++) append_character(&buffer, ' ');
            for (size_t index = 0; index < length; index++) append_character(&buffer, value[index]);
            if (left) for (size_t index = 0; index < padding; index++) append_character(&buffer, ' ');
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

int snprintf(char* output, size_t capacity, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(output, capacity, format, arguments);
    va_end(arguments);
    return result;
}
