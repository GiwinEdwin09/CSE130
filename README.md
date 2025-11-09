## CSE 130 Final Assignment

**Skills:**  
- C  
- HTTP Protocol Handling  
- Data Structures  

Implemented a multi-threaded HTTP proxy server in C with an **LRU/FIFO caching system** to improve web request performance by reusing previously fetched responses.  

- **TCP Connection Management:** Created and managed TCP connections between clients and remote servers, demonstrating low-level network programming.  
- **HTTP Request Parsing:** Parsed and reconstructed HTTP GET requests using custom request-handling structs.  
- **Custom Cache Design:** Built a cache using a hash table and doubly-linked list for O(1) lookup and eviction.  
- **Memory Management:** Allocated and freed dynamic memory safely for large HTTP responses and cache entries, and implemented safe termination for resource cleanup.  
- **Multithreading:** Structured the system for concurrent handling of multiple client requests.  

_Worked on during UCSC 2025 Winter Quarter (Feb 20 – Mar 14)._
