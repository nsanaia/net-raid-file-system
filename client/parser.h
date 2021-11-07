#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>


typedef struct {
    char *diskname;// = STORAGE1
    char *mountpoint; // = /path/to/mountpoint1
    int raid;//= 
    int servers_num;
    char **servers;// = 127.0.0.1:10001, 127.0.0.1:10002
    char *hotswap;////= 127.0.0.1:11111
} disk_struct;

typedef struct {
    char *errorlog;// = /path/to/error.log
    int cache_size;// = 1024M
    char *cache_replacment;// = rlu
    int timeout;// = 10
    int disks_num;
    disk_struct *disks;//disk array
} config_struct;


void parser_parse(char *config_file_name, config_struct *config);

void parser_destroy(config_struct *config);












