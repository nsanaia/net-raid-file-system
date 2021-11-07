#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct protocol_struct {
    char old_name[256];
    char current_name[256];
    size_t size;
    size_t newsize;
    mode_t mode;
    off_t offset;
    int flags;
    int fh;
    int function_id;
    int write_flag;
    DIR *dp;
};

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