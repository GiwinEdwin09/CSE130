#include "client_socket.h"
#include "iowrapper.h"
#include "listener_socket.h"
#include "prequest.h"
#include "a5protocol.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include <sys/file.h>
#include <sys/stat.h>
#include <regex.h>

#define BUFFER_SIZE 2048
#ifndef MAX_CACHE_ENTRY
#define MAX_CACHE_ENTRY 1048576  // 1 MB
#endif

// Global Variables
Listener_Socket_t *sock = NULL;
struct Cache *cache = NULL;

// Cache Data Structures
typedef struct CacheEntry {
    char *host;
    char *uri;
    int32_t port;
    char *response;
    size_t response_size;
    struct CacheEntry *next;
    struct CacheEntry *prev;
    struct CacheEntry *hash_next;
} CacheEntry;

typedef struct Cache {
    CacheEntry *head;
    CacheEntry *tail;
    size_t size;
    size_t capacity;
    char *mode;
    size_t table_size;
    CacheEntry **table;
} Cache;

// Hash Helper
static unsigned long hash_key(const char *host, const char *uri, int32_t port) {
    unsigned long hash = 5381;
    int c;
    while ((c = *host++))
        hash = ((hash << 5) + hash) + c;
    while ((c = *uri++))
        hash = ((hash << 5) + hash) + c;
    hash = ((hash << 5) + hash) + port;
    return hash;
}

// Cache Entry Creation
CacheEntry *cache_entry_new(char *host, char *uri, int32_t port,
                            char *response, size_t response_size) {
    CacheEntry *entry = malloc(sizeof(CacheEntry));
    if (!entry) return NULL;

    entry->host = strdup(host);
    entry->uri = strdup(uri);
    entry->port = port;

    entry->response = malloc(response_size);
    if (!entry->response) {
        free(entry->host);
        free(entry->uri);
        free(entry);
        return NULL;
    }
    memcpy(entry->response, response, response_size);
    entry->response_size = response_size;

    entry->next = entry->prev = entry->hash_next = NULL;
    return entry;
}

// Cache Creation & Deletion
Cache *cache_new(size_t capacity, char *mode) {
    Cache *c = malloc(sizeof(Cache));
    if (!c) return NULL;

    c->head = c->tail = NULL;
    c->size = 0;
    c->capacity = capacity;
    c->mode = strdup(mode);

    c->table_size = (capacity * 2 > 8) ? capacity * 2 : 8;
    c->table = calloc(c->table_size, sizeof(CacheEntry *));
    if (!c->table) {
        free(c->mode);
        free(c);
        return NULL;
    }
    return c;
}

void cache_delete(Cache **cache_ptr) {
    if (!cache_ptr || !*cache_ptr) return;
    Cache *c = *cache_ptr;

    CacheEntry *entry = c->head;
    while (entry) {
        CacheEntry *next = entry->next;
        free(entry->host);
        free(entry->uri);
        free(entry->response);
        free(entry);
        entry = next;
    }
    free(c->mode);
    free(c->table);
    free(c);
    *cache_ptr = NULL;
}

// Cache Lookup
CacheEntry *get_entry(Cache *cache, char *host, char *uri, int32_t port) {
    if (!cache) return NULL;
    unsigned long hv = hash_key(host, uri, port);
    size_t idx = hv % cache->table_size;

    CacheEntry *entry = cache->table[idx];
    while (entry) {
        if (strcmp(entry->host, host) == 0 &&
            strcmp(entry->uri, uri) == 0 &&
            entry->port == port) {
            return entry;
        }
        entry = entry->hash_next;
    }
    return NULL;
}

// Cache Insert
void evict_entry(Cache *cache);
void move_to_front(Cache *cache, CacheEntry *entry);

void add_entry(Cache *cache, CacheEntry *entry) {
    if (!cache || !entry) return;
    if (cache->size == cache->capacity) {
        evict_entry(cache);
    }
    entry->next = cache->head;
    if (cache->head)
        cache->head->prev = entry;
    cache->head = entry;
    if (!cache->tail)
        cache->tail = entry;
    cache->size++;

    unsigned long hv = hash_key(entry->host, entry->uri, entry->port);
    size_t idx = hv % cache->table_size;
    entry->hash_next = cache->table[idx];
    cache->table[idx] = entry;
}

// Cache Eviction
void evict_entry(Cache *cache) {
    if (!cache || cache->size == 0) return;

    CacheEntry *entry = cache->tail;
    if (cache->size == 1) {
        cache->head = cache->tail = NULL;
    } else {
        cache->tail = entry->prev;
        cache->tail->next = NULL;
    }

    unsigned long hv = hash_key(entry->host, entry->uri, entry->port);
    size_t idx = hv % cache->table_size;
    CacheEntry **pp = &cache->table[idx];
    while (*pp && *pp != entry) {
        pp = &(*pp)->hash_next;
    }
    if (*pp == entry) {
        *pp = entry->hash_next;
    }

    free(entry->host);
    free(entry->uri);
    free(entry->response);
    free(entry);
    cache->size--;
}

// LRU Movement
void move_to_front(Cache *cache, CacheEntry *entry) {
    if (!cache || !entry || entry == cache->head) return;

    if (entry == cache->tail) {
        cache->tail = entry->prev;
        if (cache->tail)
            cache->tail->next = NULL;
    } else {
        if (entry->prev)
            entry->prev->next = entry->next;
        if (entry->next)
            entry->next->prev = entry->prev;
    }
    entry->next = cache->head;
    if (cache->head)
        cache->head->prev = entry;
    entry->prev = NULL;
    cache->head = entry;
}

// Helper: Add "Cached: True" Header
char *add_cached_header(char *response, size_t response_size, size_t *new_size) {
    const char *header = "\r\nCached: True";
    const char *body_separator = "\r\n\r\n";

    char *headers_end = strstr(response, body_separator);
    if (!headers_end) {
        return NULL;
    }

    size_t header_size = strlen(header);
    *new_size = response_size + header_size;

    char *new_response = malloc(*new_size + 1);
    if (!new_response)
        return NULL;

    size_t headers_size = (size_t)(headers_end - response);
    memcpy(new_response, response, headers_size);
    memcpy(new_response + headers_size, header, header_size);
    memcpy(new_response + headers_size + header_size,
           headers_end,
           response_size - headers_size);

    new_response[*new_size] = '\0';
    return new_response;
}

// Cleanup handler for termination
void cleanup_handler(int signum) {
    (void) signum; // Mark signum as unused
    if (sock) {
        ls_delete(&sock);
        sock = NULL;
    }
    if (cache) {
        cache_delete(&cache);
    }
    fprintf(stderr, "Cleaned up and exiting.\n");
    exit(EXIT_SUCCESS);
}

// Connection Handler
void handle_connection(uintptr_t connfd) {
    Prequest_t *preq = NULL;
    int32_t cs = -1;
    char *response = NULL;
    size_t total_read = 0;
    size_t bytes_read = 0;
    size_t capacity = BUFFER_SIZE;

    preq = prequest_new(connfd);
    if (!preq) {
        fprintf(stderr, "Bad request from %lu\n", connfd);
        goto cleanup;
    }

    char *host = prequest_get_host(preq);
    char *uri = prequest_get_uri(preq);
    int32_t port = prequest_get_port(preq);

    if (cache) {
        CacheEntry *entry = get_entry(cache, host, uri, port);
        if (entry) {
            fprintf(stderr, "cache hit\n");
            size_t new_size = 0;
            char *cached_response = add_cached_header(entry->response, entry->response_size, &new_size);
            if (!cached_response) {
                fprintf(stderr, "Failed to add cached header\n");
                goto cleanup;
            }
            if (write_n_bytes(connfd, cached_response, new_size) < 0) {
                fprintf(stderr, "Failed to write to client\n");
                free(cached_response);
                goto cleanup;
            }
            if (strcmp(cache->mode, "LRU") == 0) {
                move_to_front(cache, entry);
            }
            free(cached_response);
            goto cleanup;
        }
    }

    cs = cs_new(host, port);
    if (cs < 0) {
        fprintf(stderr, "Failed to connect to %s:%d\n", host, port);
        goto cleanup;
    }

    char request[BUFFER_SIZE];
    int req_len = snprintf(request, BUFFER_SIZE,
                           "GET /%s HTTP/1.1\r\nConnection: close\r\n\r\n", uri);
    if (req_len < 0 || req_len >= BUFFER_SIZE) {
        fprintf(stderr, "Failed to build request\n");
        goto cleanup;
    }
    if (write_n_bytes(cs, request, req_len) < 0) {
        fprintf(stderr, "Failed to write to server\n");
        goto cleanup;
    }

    response = malloc(capacity);
    if (!response) {
        fprintf(stderr, "Memory allocation error\n");
        goto cleanup;
    }
    // Read loop with exponential (doubling) growth.
    while ((bytes_read = read(cs, response + total_read, capacity - total_read)) > 0) {
        total_read += bytes_read;
        if (total_read == capacity) {
            size_t new_capacity = capacity * 2;
            char *temp = realloc(response, new_capacity);
            if (!temp) {
                fprintf(stderr, "Memory allocation error\n");
                goto cleanup;
            }
            response = temp;
            capacity = new_capacity;
        }
    }
    if (bytes_read < 0) {
        fprintf(stderr, "Error reading from server\n");
        goto cleanup;
    }
    if (total_read == 0) {
        fprintf(stderr, "No data received from server\n");
        goto cleanup;
    }
    response[total_read] = '\0';

    if (write_n_bytes(connfd, response, total_read) < 0) {
        fprintf(stderr, "Failed to write to client\n");
        goto cleanup;
    }

    if (cache && total_read < MAX_CACHE_ENTRY) {
        CacheEntry *entry = cache_entry_new(host, uri, port, response, total_read);
        if (entry) {
            add_entry(cache, entry);
        } else {
            fprintf(stderr, "Failed to create cache entry\n");
        }
    }

cleanup:
    if (cs >= 0)
        close(cs);
    if (preq)
        prequest_delete(&preq);
    free(response);
    close(connfd);
}

// Usage
void usage(FILE *stream, char *exec) {
    fprintf(stream, "usage: %s <port> <mode> <n>\n", exec);
}

// Main
int main(int argc, char **argv) {
    if (argc < 4) {
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    // Install signal handlers for cleanup on termination.
    signal(SIGINT, cleanup_handler);
    signal(SIGTERM, cleanup_handler);

    char *endptr = NULL;
    int port = (int) strtoull(argv[1], &endptr, 10);
    if (endptr && *endptr != '\0') {
        warnx("invalid port number: %s", argv[1]);
        return EXIT_FAILURE;
    }

    char *mode = argv[2];
    if (strcmp(mode, "FIFO") != 0 && strcmp(mode, "LRU") != 0) {
        warnx("invalid cache mode: %s", mode);
        return EXIT_FAILURE;
    }

    size_t cap = (size_t) strtoull(argv[3], &endptr, 10);
    if (endptr && *endptr != '\0') {
        warnx("invalid cache size: %s", argv[3]);
        return EXIT_FAILURE;
    }

    if (cap > 0) {
        cache = cache_new(cap, mode);
        if (!cache) {
            warnx("failed to create cache");
            return EXIT_FAILURE;
        }
    }

    sock = ls_new(port);
    if (!sock) {
        warnx("failed to create listener socket");
        if (cache)
            cache_delete(&cache);
        return EXIT_FAILURE;
    }

    while (1) {
        uintptr_t connfd = ls_accept(sock);
        assert(connfd > 0);
        handle_connection(connfd);
    }

    // Unreachable cleanup, but provided for completeness.
    if (cache)
        cache_delete(&cache);
    if (sock)
        ls_delete(&sock);

    return EXIT_SUCCESS;
}
