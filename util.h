/* See LICENSE for license details. */
#define LEN(a) (sizeof(a) / sizeof(*a))

void die(const char *, ...);
void *xmalloc(size_t);
void *xreallocarray(void *, size_t, size_t);
