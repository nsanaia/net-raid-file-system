#include "parser.h"

void copy_char_star(char *src, char **target) {
    int src_len = strlen(src);
    *target = malloc(src_len + 1);
    assert(target != NULL);
    int i = 0;
    for (; i < src_len; i++) {
        (*target)[i] = src[i];
    }
    (*target)[src_len] = '\0';
}

void parse_main(FILE *file, config_struct *config) {

    char *errorlog_temp = malloc(200);
    int cache_size_temp;
    char *cache_replacment_temp = malloc(200);
    int timeout_temp;

    int res = fscanf(file,
                     " errorlog = %s cache_size = %dM cache_replacment = %s timeout = %d ",
                     errorlog_temp, &cache_size_temp, cache_replacment_temp, &timeout_temp);

    assert(res != 0);

    copy_char_star(errorlog_temp, &config->errorlog);
    copy_char_star(cache_replacment_temp, &config->cache_replacment);
    config->cache_size = cache_size_temp;
    config->timeout = timeout_temp;

    free(errorlog_temp);
    free(cache_replacment_temp);
}

void parse_disks(FILE *file, config_struct *config) {

    char *diskname_temp = malloc(200);
    char *mountpoint_temp = malloc(200);
    int raid_temp;
    char *servers_temp = malloc(200);
    char *hotswap_temp = malloc(200);
    int disk_num_temp = 0;
    config->disks = malloc(0);

    while (fscanf(file,
                  " diskname = %s mountpoint = %s raid = %d servers = %[^\n] hotswap = %s",
                  diskname_temp,
                  mountpoint_temp,
                  &raid_temp,
                  servers_temp,
                  hotswap_temp) != EOF) {

        config->disks = realloc(config->disks, sizeof(disk_struct) * (disk_num_temp + 1));

        char *server_each_temp = strtok(servers_temp, ", ");
        int servers_num_temp = 0;
        char **servers_temp = malloc(0);

        while (server_each_temp != NULL) {

            servers_temp = realloc(servers_temp, (servers_num_temp + 1) * sizeof(char *));
            copy_char_star(server_each_temp, servers_temp + servers_num_temp);
            server_each_temp = strtok(NULL, ", ");
            servers_num_temp++;
        }

        copy_char_star(diskname_temp, &config->disks[disk_num_temp].diskname);
        copy_char_star(mountpoint_temp, &config->disks[disk_num_temp].mountpoint);
        config->disks[disk_num_temp].servers_num = servers_num_temp;
        config->disks[disk_num_temp].servers = servers_temp;
        copy_char_star(hotswap_temp, &config->disks[disk_num_temp].hotswap);
        config->disks[disk_num_temp].raid = raid_temp;
        disk_num_temp++;
    }

    config->disks_num = disk_num_temp;

    free(diskname_temp);
    free(mountpoint_temp);
    free(servers_temp);
    free(hotswap_temp);
}

void parser_parse(char *config_file_name, config_struct *config) {
    printf("Logger: Parse started (file name - %s )...\n", config_file_name);
    assert(config_file_name != NULL);
    assert(config != NULL);

    FILE *file;
    file = fopen(config_file_name, "r");
    assert(file != NULL);

    parse_main(file, config);

    printf("\n");

    printf("Config - errorlog = %s \n", config->errorlog);
    printf("Config - cache_size = %d \n", config->cache_size);
    printf("Config - cache_replacment = %s \n", config->cache_replacment);
    printf("Config - cache_size := %d \n", config->timeout);

    parse_disks(file, config);

    printf("Config - numbers of disks = %d \n", config->disks_num);
    printf("\n");

    int i = 0;
    for (; i < config->disks_num; i++) {
        disk_struct disk = config->disks[i];
        printf("Config/Disk[%d] - diskname = %s \n", i, disk.diskname);
        printf("Config/Disk[%d] - mountpoint = %s \n", i, disk.mountpoint);
        printf("Config/Disk[%d] - raid = %d \n", i, disk.raid);
        printf("Config/Disk[%d] - hotswap = %s \n", i, disk.hotswap);
        printf("Config/Disk[%d] - number of servers = %d \n", i, disk.servers_num);

        int j = 0;
        for (; j < disk.servers_num; j++) {
            printf("Config/Disk[%d]/Server[%d] - server = %s \n", i, j, disk.servers[j]);
        }

        printf("\n");
    }

    fclose(file);
    printf("Logger: Parse ended successfully.\n");
}

void parser_destroy(config_struct *config) {
    int i = 0;
    for (; i < config->disks_num; i++) {
        disk_struct disk = config->disks[i];
        free(disk.diskname);
        free(disk.mountpoint);
        free(disk.hotswap);
        int j = 0;
        for (; j < disk.servers_num; j++) {
            free(disk.servers[j]);
        }
        free(disk.servers);
    }
    free(config->cache_replacment);
    free(config->errorlog);
    free(config->disks);
}