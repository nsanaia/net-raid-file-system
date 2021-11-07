#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/stat.h>
#include <sys/wait.h>

// for socket api
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <arpa/inet.h>

#include "parser.h"
#include "cache.h"
#include "../commons/procol.h"

#define SERVER_NOT_RESPONDING 500
#define SERVER_OK 200
#define SERVER_TRY_RECCONECT 300
#define SERVER_DEAD 600

struct server_basic {
    char name[256];
    int sfd;
    int status;
};

struct not_resolved_events {
    int log_len;
    int len;
    struct protocol_struct *content;
};

struct config_basic {
    char errorlog[256];
    struct cache_base base;
    struct server_basic servers[3];
    char diskname[256];
    pthread_mutex_t *client_lock;
    int timeout;
    struct not_resolved_events events;
};


int nrf_log(char *log_file_path, char *diskname, char *server, char *message) {
    FILE *logfile = fopen(log_file_path, "a");
    if (logfile == NULL) {
        logfile = fopen(log_file_path, "w");
    }
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    char res_time[256];
    strcpy(res_time, asctime(timeinfo));
    res_time[strlen(res_time) - 1] = '\0';
    fprintf(logfile, "%s%s%s", "[", res_time, "]");
    fprintf(logfile, "%s%s", " ", diskname);
    fprintf(logfile, "%s%s", " ", server);
    fprintf(logfile, "%s%s", " ", message);
    fprintf(logfile, "\n");
    fflush(logfile);
}

void not_resolved_events_init(struct not_resolved_events *events) {
    events->log_len = 0;
    events->len = 8;
    events->content = malloc(events->len * sizeof(struct protocol_struct));
}

void not_resolved_events_grow_if_needed(struct not_resolved_events *events) {
    if (events->log_len < events->len) {
        return;
    }
    events->len *= 2;
    events->content = realloc(events->content, events->len * sizeof(struct protocol_struct));
}

void not_resolved_events_add(struct not_resolved_events *events, struct protocol_struct *metadata) {
    not_resolved_events_grow_if_needed(events);
    void *dest = events->content + events->log_len;
    memcpy(dest, metadata, sizeof(struct protocol_struct));
    events->log_len++;
}

void not_resolved_events_close(struct not_resolved_events *events, int sfd) {
    int event_i = 0;
    for (; event_i < events->log_len; event_i++) {
        write(sfd, events->content + event_i, sizeof(struct protocol_struct));
        int res = 0;
        read(sfd, &res, sizeof(int));
    }
    free(events->content);
    not_resolved_events_init(events);
}

int connect_server(struct config_basic *cb, struct server_basic *sb) {
    char server_temp[256];
    printf("tryting connect : %s", sb->name);
    strcpy(server_temp, sb->name);
    printf("server1  :  %s\n", server_temp);
    char *ip_temp = strtok(server_temp, ":");
    char *port_temp = strtok(NULL, ":");
    printf("ip  :  %s\n", ip_temp);
    printf("port :  %s\n", port_temp);
    int port = atoi(port_temp);
    struct sockaddr_in addr;
    int ip;
    sb->sfd = socket(AF_INET, SOCK_STREAM, 0);
    inet_pton(AF_INET, ip_temp, &ip);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = ip;
    int ret = connect(sb->sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));
    nrf_log(cb->errorlog, cb->diskname, sb->name, "open connection");
    printf("Server file descriptor : %d", sb->sfd);
    return ret;
}

int client(config_struct *config, struct config_basic *cb, int disk_i) {
    int i = 0;
    for (; i < config->disks[disk_i].servers_num; i++) {
        strcpy(cb->servers[i].name, config->disks[disk_i].servers[i]);
        connect_server(cb, cb->servers + i);
        cb->servers[i].status = SERVER_OK;
    }
    strcpy(cb->servers[2].name, config->disks[disk_i].hotswap);
    connect_server(cb, cb->servers + 2);
    cb->servers[2].status = SERVER_OK;
}

int nrf_getattr(const char *path, struct stat *statbuf) {

    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    metadata->function_id = 10;
    strcpy(metadata->current_name, path);

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));

    struct getattr_response resp;
    int data_size = read(cb->servers[0].sfd, &resp, sizeof(struct getattr_response));

    memcpy(statbuf, &(resp.statbuf), sizeof(struct stat));

    printf("%s %d \n", "get_attr", resp.res);
    pthread_mutex_unlock(cb->client_lock);
    return resp.res;
}

int nrf_mknod(const char *path, mode_t mode, dev_t dev) {
    printf("* * * * *  client mknod : %s\n", path);
    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    metadata->function_id = 16;
    strcpy(metadata->current_name, path);
    metadata->mode = mode;

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));

    int res = 0;

    read(cb->servers[0].sfd, &res, sizeof(int));

    printf("%s %d", "ressulti of mknod ", res);
    if (cb->servers[1].status == SERVER_TRY_RECCONECT) {
        not_resolved_events_add(&(cb->events), metadata);
    } else if (cb->servers[1].status == SERVER_OK) {
        write(cb->servers[1].sfd, metadata, sizeof(struct protocol_struct));
        int res1;
        read(cb->servers[1].sfd, &res1, sizeof(int));
    }

    pthread_mutex_unlock(cb->client_lock);

    return res;
}

int nrf_mkdir(const char *path, mode_t mode) {
    printf("----  mkdir : %s\n", path);

    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    strcpy(metadata->current_name, path);
    metadata->function_id = 17;
    metadata->mode = mode;

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));

    int res = 0;

    read(cb->servers[0].sfd, &res, sizeof(int));

    if (res >= 0) {
        if (cb->servers[1].status == SERVER_TRY_RECCONECT) {
            not_resolved_events_add(&(cb->events), metadata);
        } else if (cb->servers[1].status == SERVER_OK) {
            write(cb->servers[1].sfd, metadata, sizeof(struct protocol_struct));
            int res1;
            read(cb->servers[1].sfd, &res1, sizeof(int));
        }
    }

    pthread_mutex_unlock(cb->client_lock);
    return res;
}

int nrf_unlink(const char *path) {
    printf("----  unlink : %s\n", path);
    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    cache_remove(&(cb->base), path);

    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    strcpy(metadata->current_name, path);
    metadata->function_id = 24;

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));

    int res = 0;

    read(cb->servers[0].sfd, &res, sizeof(int));

    if (res >= 0) {

        if (cb->servers[1].status == SERVER_TRY_RECCONECT) {
            not_resolved_events_add(&(cb->events), metadata);
        } else if (cb->servers[1].status == SERVER_OK) {
            write(cb->servers[1].sfd, metadata, sizeof(struct protocol_struct));
            int res1;
            read(cb->servers[1].sfd, &res1, sizeof(int));
        }
    }

    pthread_mutex_unlock(cb->client_lock);
    return res;
}

int nrf_rmdir(const char *path) {
    printf("----  rmdir : %s\n", path);
    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    strcpy(metadata->current_name, path);
    metadata->function_id = 21;

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));

    int res = 0;

    read(cb->servers[0].sfd, &res, sizeof(int));

    if (res >= 0) {

        if (cb->servers[1].status == SERVER_TRY_RECCONECT) {
            not_resolved_events_add(&(cb->events), metadata);
        } else if (cb->servers[1].status == SERVER_OK) {
            write(cb->servers[1].sfd, metadata, sizeof(struct protocol_struct));
            int res1;
            read(cb->servers[1].sfd, &res1, sizeof(int));
        }
    }

    pthread_mutex_unlock(cb->client_lock);
    return res;
}

int nrf_rename(const char *path, const char *newpath) {
    printf("----  rename : %s\n", path);

    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    strcpy(metadata->current_name, newpath);
    strcpy(metadata->old_name, path);
    metadata->function_id = 18;

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));

    int res = 0;
    read(cb->servers[0].sfd, &res, sizeof(int));

    if (res >= 0) {

        if (cb->servers[1].status == SERVER_TRY_RECCONECT) {
            not_resolved_events_add(&(cb->events), metadata);
        } else if (cb->servers[1].status == SERVER_OK) {
            write(cb->servers[1].sfd, metadata, sizeof(struct protocol_struct));
            int res1;
            read(cb->servers[1].sfd, &res1, sizeof(int));
        }
    }
    cache_rename(&(cb->base), path);

    pthread_mutex_unlock(cb->client_lock);
    return res;
}

int nrf_truncate(const char *path, off_t newsize) {
    printf("----  truncate : %s\n", path);

    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    strcpy(metadata->current_name, path);
    metadata->function_id = 19;
    metadata->newsize = newsize;

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));

    int res = 0;
    read(cb->servers[0].sfd, &res, sizeof(int));

    pthread_mutex_unlock(cb->client_lock);
    return res;
}

void reconstruct_file(int sfd_[2], int fd_[2], const char *path, size_t batch_size) {
    off_t off_i = 0;
    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    int res = 0;
    for (;; off_i += batch_size) {
        metadata->size = batch_size;
        metadata->function_id = 14;
        metadata->offset = off_i;
        metadata->write_flag = off_i == 0 ? 1 : 2;
        metadata->fh = fd_[0];

        strcpy(metadata->current_name, path);

        write(sfd_[0], metadata, sizeof(struct protocol_struct));

        int res;
        char buf[batch_size];
        read(sfd_[0], &res, sizeof(int));
        read(sfd_[0], buf, batch_size);

        if (res <= 0) {
            break;
        }

        metadata->function_id = 23;
        metadata->size = res;
        metadata->fh = fd_[1];

        write(sfd_[1], metadata, sizeof(struct protocol_struct));
        write(sfd_[1], buf, res);
        read(sfd_[1], &res, sizeof(int));
    }
}

void delete_file_on_both_server(int sfd_[2], const char *path) {
    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    strcpy(metadata->current_name, path);
    metadata->function_id = 24;

    write(sfd_[0], metadata, sizeof(struct protocol_struct));
    int res;
    read(sfd_[0], &res, sizeof(int));

    write(sfd_[1], metadata, sizeof(struct protocol_struct));
    read(sfd_[1], &res, sizeof(int));
}

int nrf_open(const char *path, struct fuse_file_info *fi) {
    printf("----  open : %s\n", path);

    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    metadata->function_id = 13;
    metadata->flags = fi->flags;
    strcpy(metadata->current_name, path);

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));

    int res = 0;
    int fd = 0;

    char file_hash[33];
    char sys_file_hash[33];

    char file_hash_1[33];
    char sys_file_hash_1[33];

    read(cb->servers[0].sfd, file_hash, 33);
    read(cb->servers[0].sfd, sys_file_hash, 33);

    read(cb->servers[0].sfd, &fd, sizeof(int));
    read(cb->servers[0].sfd, &res, sizeof(int));
    fi->fh = fd;

    if (fd >= 0) {
        if (cb->servers[1].status == SERVER_OK) {
            int fd1 = 0;
            int res1 = 0;
            metadata->write_flag = 5;
            write(cb->servers[1].sfd, metadata, sizeof(struct protocol_struct));

            read(cb->servers[1].sfd, file_hash_1, 33);
            read(cb->servers[1].sfd, sys_file_hash_1, 33);

            read(cb->servers[1].sfd, &fd1, sizeof(int));
            read(cb->servers[1].sfd, &res1, sizeof(int));

            printf("----  hash of  sys_file_hash1: %s %s\n", path, file_hash_1);
            printf("----  hash of  sys_file_hash1: %s %s\n", path, sys_file_hash_1);

            printf("----  hash of  file_hash: %s %s\n", path, file_hash);
            printf("----  hash of  sys_file_hash: %s %s\n", path, sys_file_hash);

            int sfd_[2];
            int fd_[2];

            if (fd1 >= 0) {
                if (strcmp(file_hash, sys_file_hash) != 0 && strcmp(file_hash_1, sys_file_hash_1) == 0) {
                    sfd_[0] = cb->servers[1].sfd;
                    sfd_[1] = cb->servers[0].sfd;
                    fd_[0] = fd1;
                    fd_[1] = fd;
                    reconstruct_file(sfd_, fd_, path, 4096);
                }
                if (strcmp(file_hash_1, sys_file_hash_1) != 0 || strcmp(sys_file_hash, sys_file_hash_1) != 0) {
                    sfd_[0] = cb->servers[0].sfd;
                    sfd_[1] = cb->servers[1].sfd;
                    fd_[0] = fd;
                    fd_[1] = fd1;
                    reconstruct_file(sfd_, fd_, path, 4096);
                }
                if (strcmp(file_hash, sys_file_hash) != 0 && strcmp(file_hash_1, sys_file_hash_1) != 0) {
                    sfd_[0] = cb->servers[0].sfd;
                    sfd_[1] = cb->servers[1].sfd;
                    delete_file_on_both_server(sfd_, path);
                }
                res = 0;
            }
        }
    }

    printf("----  openeded with status : %s %d\n", path, res);
    pthread_mutex_unlock(cb->client_lock);

    return res;
}

int nrf_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("----  read : %s\n", path);

    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    char *content_ = malloc(size);

    if (cache_find(&cb->base, path, offset, size, content_) == 1) {
        memcpy(buf, content_, size);
        printf("shemo shechemaa");
        printf("----  buf : %s\n", content_);

        printf("----  buf : %s\n", buf);
        pthread_mutex_unlock(cb->client_lock);

        return size;
    }

    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    strcpy(metadata->current_name, path);
    metadata->function_id = 14;
    metadata->size = size;
    metadata->offset = offset;
    metadata->fh = fi->fh;

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));
    int res;
    read(cb->servers[0].sfd, &res, sizeof(int));
    read(cb->servers[0].sfd, buf, metadata->size);

    cache_add(&cb->base, path, offset, size, buf);
    printf("----  buf : %s\n", buf);
    pthread_mutex_unlock(cb->client_lock);

    return res;
}

/** Write data to an open file */
int nrf_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    cache_remove(&(cb->base), path);

    printf(" --   %s\n", "write");
    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    metadata->function_id = 23;
    metadata->size = size;
    metadata->offset = offset;
    metadata->fh = fi->fh;
    metadata->write_flag = 0;
    strcpy(metadata->current_name, path);

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));
    write(cb->servers[0].sfd, buf, size);

    int res = 0;
    read(cb->servers[0].sfd, &res, sizeof(int));

    if (res >= 0) {
        metadata->write_flag = 5;
        metadata->flags = 33793;
        write(cb->servers[1].sfd, metadata, sizeof(struct protocol_struct));
        write(cb->servers[1].sfd, buf, size);
        int res2;
        read(cb->servers[1].sfd, &res2, sizeof(int));
    }

    pthread_mutex_unlock(cb->client_lock);

    return res;
}

/** Release an open file */
int nrf_release(const char *path, struct fuse_file_info *fi) {
    printf("----  release : %s\n", path);

    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);
    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    metadata->function_id = 15;
    metadata->fh = fi->fh;

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));

    int res = 0;
    read(cb->servers[0].sfd, &res, sizeof(int));

    pthread_mutex_unlock(cb->client_lock);
    return res;
}

/** Open directory */
int nrf_opendir(const char *path, struct fuse_file_info *fi) {
    printf("----  opendir : %s\n", path);

    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    metadata->function_id = 11;
    strcpy(metadata->current_name, path);

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));

    struct opendir_response resp;

    read(cb->servers[0].sfd, &resp, sizeof(struct opendir_response));

    fi->fh = (intptr_t) resp.dp;

    pthread_mutex_unlock(cb->client_lock);
    return resp.res;
}

/** Read directory */
int nrf_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                struct fuse_file_info *fi) {
    printf("----  readdir : %s\n", path);
    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    metadata->function_id = 12;
    metadata->dp = (DIR * )(uintptr_t)
    fi->fh;

    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));

    struct readdir_response resp;
    read(cb->servers[0].sfd, &resp, sizeof(struct readdir_response));

    char temp[resp.len];
    read(cb->servers[0].sfd, temp, resp.len);

    char *token = strtok(temp, "/");
    do {
        if (filler(buf, token, NULL, 0) != 0) {
            pthread_mutex_unlock(cb->client_lock);
            return -ENOMEM;
        }

    } while ((token = strtok(NULL, "/")) != NULL);

    pthread_mutex_unlock(cb->client_lock);
    return resp.res;
}

int nrf_releasedir(const char *path, struct fuse_file_info *fi) {
    printf("----  releasedir : %s\n", path);
    struct config_basic *cb = (struct config_basic *) fuse_get_context()->private_data;
    pthread_mutex_lock(cb->client_lock);

    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    metadata->function_id = 22;
    metadata->dp = (DIR * )(uintptr_t)
    fi->fh;
    write(cb->servers[0].sfd, metadata, sizeof(struct protocol_struct));

    pthread_mutex_unlock(cb->client_lock);
    return 0;
}

struct fuse_operations nrf_oper = {
        .getattr = nrf_getattr,
        .mknod = nrf_mknod,
        .mkdir = nrf_mkdir,
        .unlink = nrf_unlink,
        .rmdir = nrf_rmdir,
        .rename = nrf_rename,
        .truncate = nrf_truncate,
        .open = nrf_open,
        .read = nrf_read,
        .write = nrf_write,
        .opendir = nrf_opendir,
        .readdir = nrf_readdir,
        .releasedir = nrf_releasedir,
        .release = nrf_release,

};

int look_server(int sfd) {
    int ret = SERVER_NOT_RESPONDING;
    struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
    metadata->function_id = 20;
    write(sfd, metadata, sizeof(struct protocol_struct));
    read(sfd, &ret, sizeof(int));
    return ret;
}

void copy_server_content(int sfd1, int sfd2) {

    struct protocol_struct metadata;
    metadata.function_id = 25;
    write(sfd1, &metadata, sizeof(struct protocol_struct));
    struct not_resolved_events events;
    not_resolved_events_init(&events);

    struct server_iterator_response it;
    for (;;) {
        read(sfd1, &it, sizeof(struct server_iterator_response));
        if (it.function_id != -1) {
            metadata.function_id = it.function_id;
            metadata.mode = 0000777;
            strcpy(metadata.current_name, it.path);
            printf("%s\n", metadata.current_name);
            not_resolved_events_add(&events, &metadata);
        } else {
            not_resolved_events_close(&events, sfd2);
            return;
        }
    }
}

void *server_looker_first(void *data) {
    struct config_basic *cb = (struct config_basic *) data;
    int stage = SERVER_OK;
    time_t reconnect_intial_time;
    sleep(2);
    for (;;) {
        sleep(1);

        printf("looking %d\n", stage);

        pthread_mutex_lock(cb->client_lock);
        if (stage == SERVER_OK) {

            stage = look_server(cb->servers[0].sfd);
            if (stage == SERVER_NOT_RESPONDING) {
                time(&reconnect_intial_time);
                stage = SERVER_TRY_RECCONECT;
                cb->servers[0].status = SERVER_TRY_RECCONECT;
            }

            printf("statusioto %d\n", stage);
        } else if (stage == SERVER_TRY_RECCONECT) {
            if (cb->servers[1].status == SERVER_OK) {
                struct server_basic tmp_server;
                int temp_sfd = cb->servers[1].sfd;
                char temp_name[256];
                strcpy(temp_name, cb->servers[1].name);
                int temp_status = cb->servers[1].status;

                cb->servers[1].sfd = cb->servers[0].sfd;
                cb->servers[1].status = cb->servers[0].status;
                strcpy(cb->servers[1].name, cb->servers[0].name);

                cb->servers[0].sfd = temp_sfd;
                cb->servers[0].status = temp_status;
                strcpy(cb->servers[0].name, temp_name);

                stage = SERVER_OK;
            } else {
                nrf_log(cb->errorlog, cb->diskname, cb->servers[1].name, "all servers corupted");
                pthread_mutex_unlock(cb->client_lock);
                pthread_exit(0);
            }
        }
        pthread_mutex_unlock(cb->client_lock);
    }
}

void *server_looker_second(void *data) {

    struct config_basic *cb = (struct config_basic *) data;
    int stage = SERVER_OK;
    time_t reconnect_intial_time;

    sleep(2);

    for (;;) {
        sleep(1);

        printf("looking %d\n", stage);

        pthread_mutex_lock(cb->client_lock);
        if (stage == SERVER_OK) {

            stage = look_server(cb->servers[1].sfd);
            if (stage == SERVER_NOT_RESPONDING) {
                time(&reconnect_intial_time);
                stage = SERVER_TRY_RECCONECT;
                cb->servers[1].status = SERVER_TRY_RECCONECT;
            }

            printf("statusioto %d\n", stage);
        } else if (stage == SERVER_TRY_RECCONECT) {
            time_t current_time;
            time(&current_time);
            double reconnect_period;
            reconnect_period = difftime(current_time, reconnect_intial_time);
            if (cb->timeout <= reconnect_period) {
                stage = SERVER_DEAD;
                printf("needs hotswap\n");
                nrf_log(cb->errorlog, cb->diskname, cb->servers[1].name, "server declared as lost");
                copy_server_content(cb->servers[0].sfd, cb->servers[2].sfd);
                memcpy(cb->servers + 1, cb->servers + 2, sizeof(struct server_basic));
                cb->servers[1].status = SERVER_OK;
                cb->servers[2].status = SERVER_DEAD;

                pthread_mutex_unlock(cb->client_lock);
                pthread_exit(0);
            } else {
                int reconnect_ret = connect_server(cb, cb->servers + 1);
                if (reconnect_ret != -1) {
                    nrf_log(cb->errorlog, cb->diskname, cb->servers[1].name, "server recconected");
                    stage = SERVER_OK;
                    cb->servers[1].status = SERVER_OK;
                    not_resolved_events_close(&(cb->events), cb->servers[1].sfd);
                }
            }
        }
        pthread_mutex_unlock(cb->client_lock);
    }
}

int main(int argc, char *argv[]) {

    char *config_file_name = argv[3];
    printf("Config File - %s\n", config_file_name);
    config_struct *config = malloc(sizeof(config_struct));
    parser_parse(config_file_name, config);

    pid_t pid;
    int disk_i;

    struct cache_base *base_ = malloc(sizeof(struct cache_base));

    cache_init(base_, config->cache_size);

    for (; disk_i < config->disks_num; disk_i++) {
        pid = fork();
        if (pid != 0) {
            continue;
        }
        pthread_mutex_t client_lock;
        pthread_mutex_init(&client_lock, NULL);

        strcpy(argv[3], config->disks[disk_i].mountpoint);

        struct config_basic *cb = malloc(sizeof(struct config_basic));
        strcpy(cb->diskname, config->disks[disk_i].diskname);
        memcpy(&(cb->base), base_, sizeof(struct cache_base));
        strcpy(cb->errorlog, config->errorlog);
        cb->client_lock = &client_lock;
        cb->timeout = config->timeout;
        not_resolved_events_init(&(cb->events));

        pthread_t t1 = 0;
        pthread_t t2 = 1;
        pthread_create(&t1, NULL, &server_looker_first, cb);
        pthread_create(&t2, NULL, &server_looker_second, cb);

        client(config, cb, disk_i);
        fuse_main(argc, argv, &nrf_oper, cb);

        break;
    }

    pid_t p;
    int temp = 0;
    while ((p = wait(&temp)) > 0) {}
    return 0;
}