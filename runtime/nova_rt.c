/* nova_rt.c — Nova++ runtime: strings and arrays. Linked with the .ll that novac emits:
 *   clang -Wno-override-module program.ll runtime/nova_rt.c -o program
 * Memory is never freed (the OS reclaims it at exit); a runtime error prints a message with
 * the Nova++ line number and exits with status 1. */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int64_t len, cap; uint64_t* data; } NovaArr;

static void* xmalloc(size_t n) {
    void* p = malloc(n ? n : 1);
    if (!p) { fputs("runtime error: out of memory\n", stderr); exit(1); }
    return p;
}

static void fail(int32_t line, const char* what) {
    fflush(stdout);
    fprintf(stderr, "runtime error (line %d): %s\n", (int)line, what);
    exit(1);
}

static char* dup_n(const char* s, size_t n) {
    char* r = (char*)xmalloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

/* ---------- strings ---------- */

char* nova_str_concat(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    char* r = (char*)xmalloc(la + lb + 1);
    memcpy(r, a, la);
    memcpy(r + la, b, lb + 1);
    return r;
}

char* nova_int_to_str(int64_t v) {
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%lld", (long long)v);
    return dup_n(buf, (size_t)n);
}

char* nova_float_to_str(double v) {
    char buf[64];
    int n = snprintf(buf, sizeof buf, "%g", v);
    return dup_n(buf, (size_t)n);
}

const char* nova_bool_to_str(int32_t v) { return v ? "true" : "false"; }

int32_t nova_str_cmp(const char* a, const char* b) {
    int c = strcmp(a, b);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

char* nova_str_upper(const char* s) {
    char* r = dup_n(s, strlen(s));
    for (char* p = r; *p; ++p) *p = (char)toupper((unsigned char)*p); /* ASCII only */
    return r;
}

char* nova_str_lower(const char* s) {
    char* r = dup_n(s, strlen(s));
    for (char* p = r; *p; ++p) *p = (char)tolower((unsigned char)*p);
    return r;
}

int32_t nova_str_contains(const char* s, const char* needle) { return strstr(s, needle) != NULL; }

int64_t nova_str_len(const char* s) { return (int64_t)strlen(s); } /* bytes, not characters */

/* ---------- arrays: 8-byte slots (int, float bits, bool, string pointer) ---------- */

NovaArr* nova_arr_new(void) {
    NovaArr* a = (NovaArr*)xmalloc(sizeof *a);
    a->len = a->cap = 0;
    a->data = NULL;
    return a;
}

void nova_arr_push(NovaArr* a, uint64_t v) {
    if (a->len == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 4;
        a->data = (uint64_t*)realloc(a->data, (size_t)a->cap * sizeof(uint64_t));
        if (!a->data) { fputs("runtime error: out of memory\n", stderr); exit(1); }
    }
    a->data[a->len++] = v;
}

static void check_index(const NovaArr* a, int64_t i, int32_t line) {
    if (i < 1 || i > a->len) {
        char msg[96];
        snprintf(msg, sizeof msg, "array index %lld is out of range (length %lld)", (long long)i, (long long)a->len);
        fail(line, msg);
    }
}

uint64_t nova_arr_get(const NovaArr* a, int64_t i, int32_t line) { /* i is 1-based */
    check_index(a, i, line);
    return a->data[i - 1];
}

void nova_arr_remove_at(NovaArr* a, int64_t i, int32_t line) {
    check_index(a, i, line);
    memmove(&a->data[i - 1], &a->data[i], (size_t)(a->len - i) * sizeof(uint64_t));
    a->len--;
}

/* kind: 0 = compare bits (int, bool), 1 = double, 2 = string. Removes the first match, if any. */
void nova_arr_remove_value(NovaArr* a, uint64_t bits, int32_t kind) {
    for (int64_t i = 0; i < a->len; ++i) {
        int same;
        if (kind == 1) {
            double x, y;
            memcpy(&x, &a->data[i], sizeof x);
            memcpy(&y, &bits, sizeof y);
            same = x == y;
        } else if (kind == 2) {
            same = strcmp((const char*)(uintptr_t)a->data[i], (const char*)(uintptr_t)bits) == 0;
        } else {
            same = a->data[i] == bits;
        }
        if (same) {
            memmove(&a->data[i], &a->data[i + 1], (size_t)(a->len - i - 1) * sizeof(uint64_t));
            a->len--;
            return;
        }
    }
}

int64_t nova_arr_len(const NovaArr* a) { return a->len; }

NovaArr* nova_str_split(const char* s, const char* delim) {
    NovaArr* arr = nova_arr_new();
    size_t dl = strlen(delim);
    if (dl == 0) {
        nova_arr_push(arr, (uint64_t)(uintptr_t)dup_n(s, strlen(s)));
        return arr;
    }
    const char* start = s;
    const char* hit;
    while ((hit = strstr(start, delim)) != NULL) {
        nova_arr_push(arr, (uint64_t)(uintptr_t)dup_n(start, (size_t)(hit - start)));
        start = hit + dl;
    }
    nova_arr_push(arr, (uint64_t)(uintptr_t)dup_n(start, strlen(start)));
    return arr;
}
