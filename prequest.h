#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct ProxyRequest Prequest_t;

// Constructor.
// Parse the data from connection. Checks static correctness (i.e.,
// that each field fits within our required bounds), but does not
// check for semantic correctness (e.g., does not check that a URI is
// not a directory).
Prequest_t *prequest_new(int connfd);

// Destructor
void prequest_delete(Prequest_t **req);

//////////////////////////////////////////////////////////////////////
// Functions that get stuff we might need elsewhere from a connection

// Return URI from parsing.
char *prequest_get_uri(Prequest_t *req);

// Return host from parsing.
char *prequest_get_host(Prequest_t *req);

// Return port from parsing.
size_t prequest_get_port(Prequest_t *req);

// Return the value for the header field named header.
// Implemented for header named "Host".
char *prequest_get_header(Prequest_t *req, char *header);

//Functions for debugging:
char *prequest_str(Prequest_t *req);
