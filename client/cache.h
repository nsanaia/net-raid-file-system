#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

struct cache_item{
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

/*
    return value 
    -1 for cant fin
    1 for find
*/
int cache_find(struct cache_base * base, const char * name_  ,off_t offset_, size_t size_, char * content_ );

void cache_rename(struct cache_base * base, const char * name_ );

void cache_remove(struct cache_base * base, const char * name_ );

void cache_destroy(struct cache_base * base);