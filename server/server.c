#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fts.h>

#include <openssl/md5.h>
#include <sys/xattr.h>
#include <sys/epoll.h>

#include "../commons/procol.h"

#define BACKLOG 10
#define SERVER_OK 200

char *storage_dir;
int sfd, cfd;
int epol_cfd;

//stackoverflow code
int hash(char *filename, char *hash) {
    int MD_DIGEST_LENGTH = 16;
    unsigned char c[MD_DIGEST_LENGTH];

    int i;
    FILE *inFile = fopen(filename, "rb");
    MD5_CTX mdContext;
    int bytes;
    unsigned char data[1024];

    if (inFile == NULL) {
        printf("%s can't be opened.\n", filename);
        return 0;
    }

    MD5_Init(&mdContext);
    while ((bytes = fread(data, 1, 1024, inFile)) != 0)
        MD5_Update(&mdContext, data, bytes);

    MD5_Final(c, &mdContext);

    char md5string[33];
    i = 0;
    for (; i < 16; ++i)
        sprintf(&md5string[i * 2], "%02x", (unsigned int) c[i]);

    strcpy(hash, md5string);
    fclose(inFile);

    return 0;
}

int _mknod(struct protocol_struct *metadata) {

    char fpath[256];
    strcpy(fpath, storage_dir);
    strcat(fpath, metadata->current_name);

    int retstat;
    retstat = open(fpath, O_CREAT | O_EXCL | O_WRONLY, metadata->mode);
    if (retstat >= 0)
        retstat = close(retstat);
    if (retstat < 0)
        retstat = -errno;

    char hash_temp[33];
    hash(fpath, hash_temp);
    setxattr(fpath, "user.hash", hash_temp, 33, 0);
    hash_temp[32] = '\0';

    write(epol_cfd, &retstat, sizeof(int));
    return 1;
}

int _getattr(struct protocol_struct *metadata) {
    char fpath[256];
    strcpy(fpath, storage_dir);
    strcat(fpath, metadata->current_name);

    struct getattr_response resp;

    int retstat = lstat(fpath, &resp.statbuf);
    if (retstat < 0)
        retstat = -errno;

    resp.res = retstat;
    write(epol_cfd, &resp, sizeof(struct getattr_response));

    return retstat;
}

int _readdir(struct protocol_struct *metadata) {

    struct readdir_response resp;
    char temp[10000];
    temp[0] = '\0';

    int retstat = 0;
    DIR *dp;
    struct dirent *de;
    dp = metadata->dp;


    de = readdir(dp);
    if (de == 0) {
        retstat = -errno;
    }

    do {

        strcat(temp, de->d_name);
        strcat(temp, "/");
    } while ((de = readdir(dp)) != NULL);
    resp.res = retstat;

    resp.len = strlen(temp) + 1;
    write(epol_cfd, &resp, sizeof(struct readdir_response));

    write(epol_cfd, temp, resp.len);

    return 0;
}

int _mkdir(struct protocol_struct *metadata) {

    char fpath[256];
    fpath[0] = '\0';
    strcpy(fpath, storage_dir);
    strcat(fpath, metadata->current_name);

    int retstat = mkdir(fpath, metadata->mode);
    if (retstat < 0)
        retstat = -errno;

    write(epol_cfd, &retstat, sizeof(int));

    return retstat;
}

int _rename(struct protocol_struct *metadata) {

    char fpath[256];
    strcpy(fpath, storage_dir);
    strcat(fpath, metadata->current_name);

    char old_path[256];
    strcpy(old_path, storage_dir);
    strcat(old_path, metadata->old_name);

    int retstat = rename(old_path, fpath);
    if (retstat < 0)
        retstat = -errno;

    write(epol_cfd, &retstat, sizeof(int));

    return retstat;
}

int _opendir(struct protocol_struct *metadata) {


    char fpath[256];
    strcpy(fpath, storage_dir);
    strcat(fpath, metadata->current_name);


    int retstat = 0;
    printf("%s  \n", fpath);

    // since opendir returns a pointer, takes some custom handling of
    // return status.
    DIR *dp = opendir(fpath);


    if (dp == NULL) {
        retstat = -errno;
    }

    struct opendir_response resp;
    resp.res = retstat;
    resp.dp = dp;

    write(epol_cfd, &resp, sizeof(struct opendir_response));

    return retstat;
}

int _unlink(struct protocol_struct *metadata) {

    char fpath[256];
    fpath[0] = '\0';
    strcpy(fpath, storage_dir);
    strcat(fpath, metadata->current_name);

    int retstat = unlink(fpath);
    if (retstat < 0)
        retstat = -errno;

    write(epol_cfd, &retstat, sizeof(retstat));

    return retstat;
}

int _releasedir(struct protocol_struct *metadata) {

    closedir(metadata->dp);

    //  write(epol_cfd, &retstat, sizeof(retstat));
    return 0;
}

int _write(struct protocol_struct *metadata) {

    char fpath[256];
    fpath[0] = '\0';
    strcpy(fpath, storage_dir);
    strcat(fpath, metadata->current_name);

    char buf[metadata->size];
    read(epol_cfd, buf, metadata->size);

    int retstat;
    FILE *f;
    if (metadata->write_flag == 0 || metadata->write_flag == 5) {

        if (metadata->write_flag == 5) {
            metadata->fh = open(fpath, metadata->flags);
        }

        retstat = pwrite(metadata->fh, buf, metadata->size, metadata->offset);

        if (metadata->write_flag == 5) {
            close((uint64_t) metadata->fh);
        }
    } else {
        f = fopen(fpath, metadata->write_flag == 1 ? "w" : "a");
        retstat = pwrite(fileno(f), buf, metadata->size, metadata->offset);
        fclose(f);
    }

    if (retstat < 0)
        retstat = -errno;

    printf(" writiing server  resultiiiiiiiii; %d \n", retstat);

    char hash_temp[33];
    hash_temp[0] = '\0';
    hash(fpath, hash_temp);
    setxattr(fpath, "user.hash", hash_temp, 33, 0);
    hash_temp[32] = '\0';

    write(epol_cfd, &retstat, sizeof(int));
    return retstat;
}

int _rmdir(struct protocol_struct *metadata) {

    char fpath[256];
    fpath[0] = '\0';
    strcpy(fpath, storage_dir);
    strcat(fpath, metadata->current_name);
    int retstat = rmdir(fpath);
    if (retstat < 0)
        retstat = -errno;

    write(epol_cfd, &retstat, sizeof(retstat));

    return retstat;
}

int _open(struct protocol_struct *metadata) {

    char fpath[256];
    fpath[0] = '\0';
    strcpy(fpath, storage_dir);
    strcat(fpath, metadata->current_name);

    int fd = 0;
    int retstat = 0;

    fd = metadata->write_flag != 5 ? open(fpath, metadata->flags) : fd;


    if (fd < 0) {
        fd = -errno;
        retstat = -errno;
    }

    char file_hash[33];
    file_hash[0] = '\0';
    hash(fpath, file_hash);
    file_hash[32] = '\0';


    char sys_file_hash[33];
    sys_file_hash[0] = '\0';
    getxattr(fpath, "user.hash", sys_file_hash, 33);
    sys_file_hash[32] = '\0';


    write(epol_cfd, file_hash, 33);
    write(epol_cfd, sys_file_hash, 33);

    write(epol_cfd, &fd, sizeof(int));
    write(epol_cfd, &retstat, sizeof(int));
    return retstat;
}

int _release(struct protocol_struct *metadata) {

    int retstat = close((uint64_t) metadata->fh);
    if (retstat < 0)
        retstat = -errno;

    write(epol_cfd, &retstat, sizeof(int));

    return retstat;
}

int _read(struct protocol_struct *metadata) {

    char fpath[256];
    fpath[0] = '\0';
    strcpy(fpath, storage_dir);
    strcat(fpath, metadata->current_name);

    char buf[metadata->size];


    int retstat;
    FILE *f;
    if (metadata->write_flag == 1) {
        f = fopen(fpath, "r");
        retstat = pread(fileno(f), buf, metadata->size, metadata->offset);
        fclose(f);
    } else {
        retstat = pread(metadata->fh, buf, metadata->size, metadata->offset);
    }

    if (retstat < 0)
        retstat = -errno;

    write(epol_cfd, &retstat, sizeof(int));
    write(epol_cfd, buf, metadata->size);
    return retstat;
}

int _truncate(struct protocol_struct *metadata) {

    char fpath[256];
    fpath[0] = '\0';
    strcpy(fpath, storage_dir);
    strcat(fpath, metadata->current_name);

    int retstat = truncate(fpath, metadata->newsize);
    if (retstat < 0)
        retstat = -errno;

    write(epol_cfd, &retstat, sizeof(int));

    return retstat;
}

int _server_iterator(struct protocol_struct *metadata) {

    char fpath[256];
    fpath[0] = '\0';
    strcpy(fpath, storage_dir);
    int fpath_len = strlen(storage_dir);

    char *path[] = {fpath, NULL};
    FTS *ftsp;
    FTSENT *p, *chp;
    int fts_options = FTS_COMFOLLOW | FTS_LOGICAL | FTS_NOCHDIR;
    int rval = 0;

    if ((ftsp = fts_open(path, fts_options, NULL)) == NULL) {
        return -1;
    }

    /* Initialize ftsp with as many argv[] parts as possible. */
    chp = fts_children(ftsp, 0);
    if (chp == NULL) {
        return 0; /* no files to traverse */
    }

    struct server_iterator_response resp_i;

    while ((p = fts_read(ftsp)) != NULL) {
        if (p->fts_info == FTS_D) {
            resp_i.function_id = 17;
        } else if (p->fts_info == FTS_F) {
            resp_i.function_id = 16;
        } else {
            continue;
        }
        char *temp = p->fts_path + fpath_len;
        strcpy(resp_i.path, temp);
        write(epol_cfd, &resp_i, sizeof(struct server_iterator_response));
    }
    resp_i.function_id = -1;
    write(epol_cfd, &resp_i, sizeof(struct server_iterator_response));

    fts_close(ftsp);
    return 0;
}

int main(int argc, char *argv[]) {

    int server_ok = SERVER_OK;
    struct sockaddr_in addr;
    struct sockaddr_in peer_addr;
    int port = atoi(argv[2]);

    storage_dir = argv[3];

    sfd = socket(AF_INET, SOCK_STREAM, 0);
    int optval = 1;
    setsockopt(sfd, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR), (char *) &optval, sizeof(optval));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));
    listen(sfd, BACKLOG);
    int peer_addr_size = sizeof(struct sockaddr_in);

    cfd = accept(sfd, (struct sockaddr *) &peer_addr, &peer_addr_size);
    printf("%s", "Connected\n");

    int epoll_fd = epoll_create(1);
    struct epoll_event e;
    struct epoll_event es[1];
    e.events = EPOLLIN;
    e.data.fd = cfd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cfd, &e);

    while (1) {

        int ready = epoll_wait(epoll_fd, es, 1, -1);
        int i = 0;
        for (; i < ready; i++) {
            epol_cfd = es[i].data.fd;
            struct protocol_struct *metadata = malloc(sizeof(struct protocol_struct));
            int data_size = recv(epol_cfd, metadata, sizeof(struct protocol_struct), 0);

            if (metadata->function_id == 25) {
                _server_iterator(metadata);
            } else if (metadata->function_id == 16) {
                _mknod(metadata);
            } else if (metadata->function_id == 10) {
                _getattr(metadata);
            } else if (metadata->function_id == 11) {
                _opendir(metadata);
            } else if (metadata->function_id == 12) {
                _readdir(metadata);
            } else if (metadata->function_id == 17) {
                _mkdir(metadata);
            } else if (metadata->function_id == 18) {
                _rename(metadata);
            } else if (metadata->function_id == 21) {
                _rmdir(metadata);
            } else if (metadata->function_id == 24) {
                _unlink(metadata);
            } else if (metadata->function_id == 22) {
                _releasedir(metadata);
            } else if (metadata->function_id == 13) {
                _open(metadata);
            } else if (metadata->function_id == 14) {
                _read(metadata);
            } else if (metadata->function_id == 15) {
                _release(metadata);
            } else if (metadata->function_id == 19) {
                _truncate(metadata);
            } else if (metadata->function_id == 23) {
                _write(metadata);
            } else if (metadata->function_id == 20) {
                write(cfd, &server_ok, sizeof(int));
            }
        }
    }

    close(cfd);
    close(sfd);
}