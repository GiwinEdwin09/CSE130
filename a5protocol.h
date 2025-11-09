#pragma once

/**
 *  Regular expressions for the fileds of a request line.
 *
 *  The expressions *do not* include capture groups, which you might want to add
 *  by surrounding them with '(' and ')'. They also are independent, which you
 *  might want to do.  For example, to create a regex stirng that captures the
 *  TYPE and the URI, you could use the string literal::
 *
 *  "(" TYPE_REGEX ") (" URI_REGEX ")"
 */

#define GET_METHOD    "GET"
#define HOST_PREAMBLE "http://"
#define HOST_REGEX    "[a-zA-Z0-9.-]{1,128}"
#define PORT_REGEX    ":[0-9]{1,6}"
#define URI_REGEX     "/[a-zA-Z0-9.-/_]{0,256}"
#define HTTP_REGEX    "HTTP/[0-9].[0-9]"
#define HTTP_VERSION  "HTTP/1.1"

#define HEADER_FIELD_REGEX "[a-zA-Z0-9.-]{1,128}"
#define HEADER_VALUE_REGEX "[ -~]{1,128}"

#define CACHED_HEADER "Cached: True"

#define MAX_HEADER_LEN 2048

#define MAX_CACHE_ENTRY (1 << 20)

#define MAX_URI_SIZE  256
#define MAX_HOST_SIZE 128
#define MAX_PORT_SIZE 6
