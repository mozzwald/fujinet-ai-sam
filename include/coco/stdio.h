#ifndef STDIO_H
#define STDIO_H

#include <cmoc.h>
#include <coco.h>
#include <stdarg.h>

#define fflush(stream) /* no-op */
#define stdout 0
#define getchar() (char) waitkey(0);
#define cputc(c) putchar(c);
#define snprintf(buf, len, fmt, ...) sprintf(buf, fmt, __VA_ARGS__);

int snprintf(char *str, size_t n, const char *fmt, ...);

#endif /* STDIO_H */