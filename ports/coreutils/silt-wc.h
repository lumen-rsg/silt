#pragma once
typedef uint32_t char32_t;

// Match gnulib's single-byte C locale, including GNU wc's default NBSP
// separator at byte 0xa0. POSIXLY_CORRECT disables that extra separator.
#undef MB_CUR_MAX
#define MB_CUR_MAX 1
#define IO_BUFSIZE 1024
static inline char32_t btoc32(int byte) { return (char32_t)byte; }
static inline bool c32isnbspace(char32_t c) { return c == 0xa0 || c == 0x2007 || c == 0x202f || c == 0x2060; }
enum argv_iter_err { AI_ERR_OK, AI_ERR_EOF, AI_ERR_MEM, AI_ERR_READ };
struct argv_iterator { char** values; size_t next; };
static inline struct argv_iterator* argv_iter_init_argv(char** values) {
    struct argv_iterator* result = xmalloc(sizeof(*result));
    *result = (struct argv_iterator){ .values = values };
    return result;
}
static inline char* argv_iter(struct argv_iterator* iterator, enum argv_iter_err* error) {
    char* value = iterator->values[iterator->next];
    *error = value ? AI_ERR_OK : AI_ERR_EOF;
    if (value) iterator->next++;
    return value;
}
static inline size_t argv_iter_n_args(struct argv_iterator* iterator) { return iterator->next; }
static inline void argv_iter_free(struct argv_iterator* iterator) { free(iterator); }
