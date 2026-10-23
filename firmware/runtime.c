#include <stddef.h>
void *memset(void *dst, int c, size_t n) {
    unsigned char *p = dst; while (n--) *p++ = (unsigned char)c; return dst;
}
void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
    unsigned char *d = dst; const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst; const unsigned char *s = src;
    if ((size_t)d < (size_t)s) { for (size_t i=0;i<n;++i) d[i]=s[i]; }
    else { while (n--) d[n]=s[n]; }
    return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p=a, *q=b;
    for (size_t i=0;i<n;++i) if (p[i]!=q[i]) return p[i]<q[i] ? -1:1;
    return 0;
}
