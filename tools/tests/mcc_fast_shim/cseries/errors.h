#define _error_silent 0
#define error(kind, ...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
