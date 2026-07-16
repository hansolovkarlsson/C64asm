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

void strip_comment(char *line) {
    int in_str = 0;
    size_t n = strlen(line);
    for (size_t i = 0; i < n; i++) {
        if (line[i] == '"') in_str = !in_str;
        if (line[i] == ';' && !in_str) { line[i] = '\0'; return; }
    }
}

int split_args(const char *operand, char args[][MAX_LINE_LEN], int max_args) {
    int count = 0;
    int depth = 0, in_str = 0;
    char cur[MAX_LINE_LEN]; size_t cn = 0;
    cur[0] = '\0';
    for (const char *p = operand; *p; p++) {
        char ch = *p;
        if (ch == '"') { in_str = !in_str; if (cn+1<sizeof(cur)) cur[cn++]=ch; }
        else if (ch == '(' && !in_str) { depth++; if (cn+1<sizeof(cur)) cur[cn++]=ch; }
        else if (ch == ')' && !in_str) { depth--; if (cn+1<sizeof(cur)) cur[cn++]=ch; }
        else if (ch == ',' && depth == 0 && !in_str) {
            cur[cn] = '\0';
            trim(cur);
            if (count < max_args) strncpy(args[count++], cur, MAX_LINE_LEN - 1);
            cn = 0; cur[0] = '\0';
        } else {
            if (cn+1<sizeof(cur)) cur[cn++]=ch;
        }
    }
    cur[cn] = '\0';
    trim(cur);
    if (cur[0] != '\0' && count < max_args) strncpy(args[count++], cur, MAX_LINE_LEN - 1);
    return count;
}
