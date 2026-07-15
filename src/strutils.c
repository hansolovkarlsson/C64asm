/*
 * strutils.c - see strutils.h.
 */

#include <string.h>
#include <ctype.h>
#include "strutils.h"

void trim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n-1])) s[--n] = '\0';
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start > 0) memmove(s, s + start, strlen(s + start) + 1);
}

int is_ident_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
int is_ident_char(char c)  { return isalnum((unsigned char)c) || c == '_'; }

void ascii_to_petscii(const char *s, unsigned char *out, int *outlen) {
    int n = 0;
    for (const char *p = s; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch >= 'a' && ch <= 'z')
            out[n++] = (unsigned char)(ch - 'a' + 'A');   /* fold lower -> upper */
        else
            out[n++] = ch;   /* already correct PETSCII: uppercase A-Z ($41-$5A)
                                 display as uppercase letters on the default C64
                                 charset, exactly like plain ASCII, and so do
                                 digits/punctuation. */
    }
    *outlen = n;
}
