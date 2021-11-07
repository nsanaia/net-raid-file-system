# Net Raid File System
Final project for Course - OS 2018 spring - at Free University of Tbilisi

A network filesystem that controls data storing and retrieving according standard RAID 1 scheme.
Major feature are, replicating servers, that gives are stable storage, atomicity of read/write and resilience to failures and loses.
Also, we use additional hotswap server, that replaces the lost server, which status is resolved after trials of reconnection. That gives use a system 
that is robust against hardware and other failures and minimum delay for requests. Also, we use client-side cache and epoll api for better performance and higher availability.



## Local usage
NRFS only works on unix based operating systems. 

Before starting install 'libpcap-dev libssl-dev' for hashing purposes.
```bash
sudo apt-get install libpcap-dev libssl-dev
```

After that we need valid configuration file. You can see here [config example](config.txt)

#### Configurations:

* errorlog - log file path
* cache_size - size of cache. format number followed by char B/K/M/G (e.g. 1024M)
* cache_replacement - algorithm type for caching (currently only LRU is supported)
* timeout - seconds, during we try to reconnect lost server and then declare server is declared lost

Each disk configuration
* diskname - used only in logging.
* mountpoint - main mount directory for this disk, where client will read and write data.
* raid - RAID algorithm level(currently only 1 is supported)
* servers - list of servers  list of ip:port (currently only two servers is supported)
* hotswap - hotswap server for replacing lost server ip:port

Firstly we must run servers, then client!

#### Build and run Server:
```bash
cd ./server
make
./server_o [ip] [port] [storageDir] # e.g ./server_o 127.0.0.1 10001 ./some/path/some/storage
```

#### Build and run client:
```bash
cd ./client
make
./client_o [config file]  # e.g ./net_raid_client ./some/path/some/config.txt
```
After launching the client/server,mount points will be mounted according configuration. After updating file system in mounted dir, data will be sent and stored to the server specified in the configuration.


--------------------------------------------


#Simple Doc

## Config

Parse of client side config file is achieved simply with fscanf. Parse is processed simultaneously as client starts. Main structures and their attributes that stores configs are very similar to config keys.

``` bash
typedef struct {
    char * diskname;// = STORAGE1
    char * mountpoint; // = /path/to/mountpoint1
    int raid;//=
    int servers_num;
    char ** servers;// = 127.0.0.1:10001, 127.0.0.1:10002
    char * hotswap;////= 127.0.0.1:11111
} disk_struct;
```
``` bash
typedef struct {
    char * errorlog;// = /path/to/error.log
    int cache_size;// = 1024M
    char * cache_replacment;// = rlu
    int timeout;// = 10
    int disks_num;
    disk_struct * disks;//disk array
} config_struct;
```


## Hierarchical Directory Structure

I used overriding Fuse functions, that is directly communicating server and exchange the relevant data, that gives us functionality of creating, updating, deleting, opening and other functionalities of files.
One of the main structures is protocol_struct, that gives us protocol to communicate client server. Any request uses it.
``` bash
struct protocol_struct {
    char current_name[256];
    char old_name[256];
    size_t newsize;
    size_t size;
    mode_t mode;
    off_t offset;
    int fh;
    int function_id;
    int flags;
    int write_flag;
    DIR *dp;
};
```
The main of param of this structure is function_id, a unique id, that indicates the server, which function is requested, that is supported by the servers.
For example client creates request, sets unique id of mkdir, so the server knows which functionality is requested. Server waits all such kind of requests using infinite while loop,
and once the request comes, checks its validity and processes the task according received unique function id and data.

This gives us functionality to diversify ech request and process relevant task. Additional params are used for each function. That is why some of them may be empty.
For example rename function need old name and also current name. So they are added in protocol struct.

Also there are added simple additional struct for exchanging data from server, that gives faster solution.
``` bash
struct getattr_response {
    struct stat statbuf;
    int res;
};

struct readdir_response {
    int res;
    int len;
};

struct opendir_response {
    int res;
    DIR *dp;
};

struct server_iterator_response {
    int function_id;
    char path[512];
};
```


## Mirroring

We use replication for improving the availability and persistence of data. There is two servers main and reserve servers. Each file/directory is duplicated on different server. Write is processed on each individual server
parallelly. Client does not receive approval, until both servers commits writing. 

We have 2 servers: main and reserve. One is considered main because it receives at first request and then second. Second is for stable storage. That is why opendir, readdir and such king of 
request are not made on reserve server.  Second server will get only request that changes the hierarchy, files and content. As a result we have two identical servers.

For achieving stable storage I use extend attribute. On server-side during mknod and write requests we use md5 hash of content and set
it in extend attribute. During open we generate new hash of content and send it with old hash in response. 
Client processes and compares hashes from two servers. 

* First case is, when one server needs restore from second server. That means one server old and new hashes is not same, that means it has invalid content.
Second server has same old and new hashes, that means it has valid content. Therefore, we copy content from reserve to main server.
* Second case, when both servers have invalid content,both have different old and new hashes. So we need to delete it on both servers.
* Last case is when each server have valid content same old and new hashes, but problem is hashes of first server and hashes for second server is different, that means one of 
them has invalid content, so I replace one of the servers content by content of second server.



## Resilience

For resilience use two threads. Before explain each usage, remember difference between main and replica servers(without hotswap). We have 2 servers: main and
reserve. One is considered main because it receives at first request and then second. Second is for stable storage. That is why opendir, readdir and such king of request are not made on reserve server.
When main server is down, read/write is available. If main server is down, it is replaced with reserve and opendir, readdir and such king of request 
is redirected to the second server, and for all request data written on second server at first. In this way user can not see anu change not server error. This replacement 
is conducted by the first thread. Second thread tries to recover the lost previously main server. This includes the case when it replaced server and is second replica. So it also tries to recover reserve server.


We use timeout from config file, when we try to re-establish connection to the server. During this process we have 2 cases:
* when re-establish succeeded
* when re-establish failed


Before start this case, during timeout before resolving success or fail, it is impossible to work stable storage correctly, replicating data across two servers, because second server is down.
During this waiting time before resolving reconnection, we aggregate requests(update, read, write...) wrapping in event structure(not_resolved_events). We save these events in memory.

```bash
struct not_resolved_events  {
    int log_len;
    int len;
    struct protocol_struct *content;
};
```
If re-establishment happens, we send aggregated requests to the reconnected server. Therefore, we will still have almost identical servers. However, there will be difference.
We will save and send default requests, except big writes. In this scenario we will correct and maintain the file hierarchical directory structure/skeleton. Then Dynamically update missed file contents from replica server, that has all the data. Stable storage plays main role here.
Additionally, we do not have to copy whole server at one time, that decrease performance significantly.

In the second case, when we could not reconnect, we use hotswap server to replace lost server. Identically in the success case, we copy the  directory structure/skeleton to the hotswap server
unlike the success case, we do not use aggregated events during timeout, because hotswap server is empty and does not include data processed before losing server. Similarly, we dynamically update content of files
using stable storage.


## Caching
 
Client side cache is used for reducing requests number on server, that optimizes server loading and also improves response time for client.
For code modularity cache.h is interface for caching. Here is declared all the functionality that is used in client side caching.

``` bash
struct cache_item {
    char name[256];
    off_t offset;
    size_t size;
    char * content;
    struct cache_item * next;
};

struct cache_base {
    int log_size;
    int max_size;
    struct cache_item * next;
};

void cache_init(struct cache_base * base, int max_len_);
int cache_add(struct cache_base * base, const char * name_ , off_t offset_, size_t size_, char * content_ );  
int cache_find(struct cache_base * base, const char * name_  ,off_t offset_, size_t size_, char * content_ );
void cache_rename(struct cache_base * base, const char * name_ );
void cache_remove(struct cache_base * base, const char * name_ );
void cache_destroy(struct cache_base * base);
```

Cache is implemented with linked list, that gives us fast/simple solution for deletion and insertion.
Main structure(cache_base) consists attributes used size(log_size), max size (max_size), and pointer to the linked list/cache_item structure(next).
Linked list is created cache_item structure. Each individual item consists of:
* name - name of file
* offset - offset of the file, from which is cached in this item
* size - how much is cached
* content - cached content
* next - pointer to the next cache item

Cache is initialized within the initialization of client. Cache updates in these function read, write, rename, unlink.

Read -  At first we check if we have data in cache according to given parameters: name, offset, size. If we find items with relevant name and its offset and size is in range of given parameters,
we immediately return cached data. If we could not find, we traditionally ask server for the data, and after receiving, we will update cache using cache_add function.
We cache_add for updating cache. At first we check validity of used space. If we hit the maximum available space for caching, we release space by removing some element using LRU algorithm.
We remove first added items. Linked list structure is very comfortable for this, we just point next attribute of base from first element to new first element and free allocated space.

Write, Unlink - we update cache with just using removing items from linked list associated with given parameter name(cache_remove) and adding new data for write

Rename - We update cache, if we already have items associated with given parameter old name. We simpy update name attributes. (cache_rename)

Time complexity for each cache function is O(n). 

## Logging

Logging is for major events.
* Establishment of connection tot the server.
* Re-establishment of connection tot the server.
* Server considered to be lost.
* All servers are down, that means we cannot continue processing.

for logging we use function:
```bash
int _log(char *log_file_path, char *diskname, char *server, char *message);
```

## Epoll

We use Epoll api for using minimal resources and minimal delay. It gives us asynchronous  I/O syscall interface. 
Epoll is initialized with function epoll_create, that takes 1 input parameter - count of clients. Currently, we are not using multiple clients, so 
this functionality is not very usable, besides the cases, when client make request parallelly.
