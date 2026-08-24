#ifndef BLUR_TLS_COMPAT_H
#define BLUR_TLS_COMPAT_H
#include <stddef.h>
void *PLUGIN_htps_TlsCalloc(size_t n, size_t size);
void PLUGIN_htps_TlsFree(void *p);
void *PLUGIN_htps_TlsMemcpy(void *dst, const void *src, size_t n);
void *PLUGIN_htps_TlsMemmove(void *dst, const void *src, size_t n);
void *PLUGIN_htps_TlsMemset(void *dst, int c, size_t n);
int PLUGIN_htps_TlsMemcmp(const void *a, const void *b, size_t n);
size_t PLUGIN_htps_TlsStrlen(const char *s);
int PLUGIN_htps_TlsStrcmp(const char *a, const char *b);
int PLUGIN_htps_TlsStrncmp(const char *a, const char *b, size_t n);
char *PLUGIN_htps_TlsStrchr(const char *s, int c);
#define calloc PLUGIN_htps_TlsCalloc
#define free PLUGIN_htps_TlsFree
#define memcpy PLUGIN_htps_TlsMemcpy
#define memmove PLUGIN_htps_TlsMemmove
#define memset PLUGIN_htps_TlsMemset
#define memcmp PLUGIN_htps_TlsMemcmp
#define strlen PLUGIN_htps_TlsStrlen
#define strcmp PLUGIN_htps_TlsStrcmp
#define strncmp PLUGIN_htps_TlsStrncmp
#define strchr PLUGIN_htps_TlsStrchr
#endif