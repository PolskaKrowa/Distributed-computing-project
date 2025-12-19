#define _POSIX_C_SOURCE 200809L
#include "metadata.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Simple linked list implementation for metadata store.
 * This keeps the implementation small and easy to understand.
 */

struct metadata_entry {
    char *key;
    char *value;
    struct metadata_entry *next;
};

struct metadata_store {
    struct metadata_entry *head;
    size_t count;
};

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    char *r = strdup(s);
    return r;
}

metadata_store_t *metadata_create(void) {
    metadata_store_t *m = calloc(1, sizeof(*m));
    return m;
}

void metadata_destroy(metadata_store_t *store) {
    if (!store) return;
    struct metadata_entry *e = store->head;
    while (e) {
        struct metadata_entry *n = e->next;
        free(e->key);
        free(e->value);
        free(e);
        e = n;
    }
    free(store);
}

static struct metadata_entry *find_entry(const metadata_store_t *store, const char *key) {
    if (!store || !key) return NULL;
    for (struct metadata_entry *e = store->head; e; e = e->next) {
        if (strcmp(e->key, key) == 0) return e;
    }
    return NULL;
}

int metadata_set(metadata_store_t *store, const char *key, const char *value) {
    if (!store || !key) return -1;
    struct metadata_entry *e = find_entry(store, key);
    if (e) {
        char *nv = xstrdup(value ? value : "");
        if (!nv) return -1;
        free(e->value);
        e->value = nv;
        return 0;
    }
    e = calloc(1, sizeof(*e));
    if (!e) return -1;
    e->key = xstrdup(key);
    e->value = xstrdup(value ? value : "");
    if (!e->key || !e->value) {
        free(e->key);
        free(e->value);
        free(e);
        return -1;
    }
    e->next = store->head;
    store->head = e;
    store->count++;
    return 0;
}

const char *metadata_get(const metadata_store_t *store, const char *key) {
    struct metadata_entry *e = find_entry(store, key);
    return e ? e->value : NULL;
}

int metadata_remove(metadata_store_t *store, const char *key) {
    if (!store || !key) return -1;
    struct metadata_entry *prev = NULL;
    for (struct metadata_entry *e = store->head; e; prev = e, e = e->next) {
        if (strcmp(e->key, key) == 0) {
            if (prev) prev->next = e->next;
            else store->head = e->next;
            free(e->key);
            free(e->value);
            free(e);
            store->count--;
            return 0;
        }
    }
    return 1; /* not found */
}

size_t metadata_count(const metadata_store_t *store) {
    if (!store) return 0;
    return store->count;
}

/* Minimal JSON escaping for strings: escapes backslash and double-quote.
 * This is sufficient for machine produced content from this store.
 */
static char *json_escape(const char *s) {
    if (!s) return NULL;
    size_t len = 0;
    for (const char *p = s; *p; ++p) {
        if (*p == '\\' || *p == '"') len += 2;
        else len += 1;
    }
    char *out = malloc(len + 1);
    if (!out) return NULL;
    char *q = out;
    for (const char *p = s; *p; ++p) {
        if (*p == '\\') { *q++ = '\\'; *q++ = '\\'; }
        else if (*p == '"') { *q++ = '\\'; *q++ = '"'; }
        else *q++ = *p;
    }
    *q = '\0';
    return out;
}

char *metadata_serialize_json(const metadata_store_t *store) {
    if (!store) return NULL;
    /* Rough sizing: average small. We'll realloc as needed. */
    size_t cap = 256;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t used = 0;
    int first = 1;
    used += snprintf(buf + used, cap - used, "{");
    for (struct metadata_entry *e = store->head; e; e = e->next) {
        char *ke = json_escape(e->key);
        char *ve = json_escape(e->value ? e->value : "");
        if (!ke || !ve) {
            free(ke); free(ve); free(buf);
            return NULL;
        }
        size_t needed = strlen(ke) + strlen(ve) + 10;
        if (used + needed + 1 > cap) {
            cap = (used + needed + 1) * 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(ke); free(ve); free(buf); return NULL; }
            buf = nb;
        }
        if (!first) {
            used += snprintf(buf + used, cap - used, ",");
        }
        used += snprintf(buf + used, cap - used, "\"%s\":\"%s\"", ke, ve);
        first = 0;
        free(ke); free(ve);
    }
    if (used + 2 > cap) {
        char *nb = realloc(buf, used + 2);
        if (!nb) { free(buf); return NULL; }
        buf = nb;
    }
    used += snprintf(buf + used, cap - used, "}");
    return buf;
}

/* Very small JSON parser that accepts {"k":"v",...}
 * It does not accept nested structures or arrays.
 * It tolerates whitespace. Keys and values must be quoted.
 */
static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

static char *unescape_json_string(const char *start, const char *end) {
    size_t len = end - start;
    char *out = malloc(len + 1);
    if (!out) return NULL;
    char *q = out;
    const char *p = start;
    while (p < end) {
        if (*p == '\\') {
            p++;
            if (p >= end) break;
            if (*p == '\\' || *p == '"') *q++ = *p++;
            else {
                /* unsupported escape, copy literally */
                *q++ = *p++;
            }
        } else {
            *q++ = *p++;
        }
    }
    *q = '\0';
    return out;
}

int metadata_load_json(metadata_store_t *store, const char *json) {
    if (!store || !json) return -1;
    /* clear existing */
    struct metadata_entry *e = store->head;
    while (e) {
        struct metadata_entry *n = e->next;
        free(e->key);
        free(e->value);
        free(e);
        e = n;
    }
    store->head = NULL;
    store->count = 0;

    const char *p = skip_ws(json);
    if (*p != '{') return -2;
    p++;
    p = skip_ws(p);
    while (*p && *p != '}') {
        if (*p != '"') return -3;
        p++;
        const char *kstart = p;
        while (*p && *p != '"') {
            if (*p == '\\') p++; /* skip escaped char */
            p++;
        }
        if (*p != '"') return -4;
        char *key = unescape_json_string(kstart, p);
        p++; /* skip closing quote */
        p = skip_ws(p);
        if (*p != ':') { free(key); return -5; }
        p++;
        p = skip_ws(p);
        if (*p != '"') { free(key); return -6; }
        p++;
        const char *vstart = p;
        while (*p && *p != '"') {
            if (*p == '\\') p++;
            p++;
        }
        if (*p != '"') { free(key); return -7; }
        char *val = unescape_json_string(vstart, p);
        p++; /* skip closing quote */
        /* store key/value */
        if (metadata_set(store, key, val) != 0) {
            free(key); free(val);
            return -8;
        }
        free(key); free(val);
        p = skip_ws(p);
        if (*p == ',') { p++; p = skip_ws(p); continue; }
        else if (*p == '}') break;
        else return -9;
    }
    if (*p != '}') return -10;
    return 0;
}

int metadata_iterate(const metadata_store_t *store, metadata_iter_cb cb, void *userdata) {
    if (!store || !cb) return -1;
    for (struct metadata_entry *e = store->head; e; e = e->next) {
        int r = cb(e->key, e->value, userdata);
        if (r != 0) return r;
    }
    return 0;
}
