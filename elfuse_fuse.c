#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

#include "elfuse_fuse.h"

pthread_mutex_t elfuse_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t elfuse_cond_var = PTHREAD_COND_INITIALIZER;

enum elfuse_function_waiting_enum elfuse_function_waiting = NONE;

const char *path_arg;

/* READIR args and results */
char **readdir_results;
size_t readdir_results_size;

/* GETATTR args and results */
enum elfuse_getattr_result_enum getattr_results;
size_t getattr_results_file_size;

/* OPEN args and results */
enum elfuse_open_result_enum open_results;

/* READ args and results */
size_t read_args_size;
size_t read_args_offset;
int read_results;
char *read_results_data;


static int elfuse_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;

    pthread_mutex_lock(&elfuse_mutex);

    /* Function to call */
    elfuse_function_waiting = GETATTR;

    /* Set function args */
    path_arg = path;

    /* Wait for the funcall results */
    pthread_cond_wait(&elfuse_cond_var, &elfuse_mutex);

    memset(stbuf, 0, sizeof(struct stat));
    if (getattr_results == GETATTR_FILE) {
        fprintf(stderr, "GETATTR received results (file %s)\n", path);
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = getattr_results_file_size;
    } else if (getattr_results == GETATTR_DIR) {
        fprintf(stderr, "GETATTR received results (dir %s)\n", path);
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else {
        fprintf(stderr, "GETATTR received results (unknown %s)\n", path);
        res = -ENOENT;
    }

    elfuse_function_waiting = NONE;
    pthread_mutex_unlock(&elfuse_mutex);

    return res;
}

static int elfuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    /* TODO: remove? */
    if (strcmp(path, "/") != 0) {
        return -ENOENT;
    }

    pthread_mutex_lock(&elfuse_mutex);

    /* Function to call */
    elfuse_function_waiting = READDIR;

    /* Set function args */
    path_arg = path;

    /* Wait for results */
    pthread_cond_wait(&elfuse_cond_var, &elfuse_mutex);

    fprintf(stderr, "READDIR received results\n");
    for (size_t i = 0; i < readdir_results_size; i++) {
        filler(buf, readdir_results[i], NULL, 0);
    }

    elfuse_function_waiting = NONE;
    pthread_mutex_unlock(&elfuse_mutex);

    return 0;
}

static int elfuse_open(const char *path, struct fuse_file_info *fi)
{
    if ((fi->flags & 3) != O_RDONLY)
        return -EACCES;

    pthread_mutex_lock(&elfuse_mutex);

    /* Function to call */
    elfuse_function_waiting = OPEN;

    /* Set callback args */
    path_arg = path;
    int res = 0;

    /* Wait for results */
    pthread_cond_wait(&elfuse_cond_var, &elfuse_mutex);

    fprintf(stderr, "OPEN received results (%d)\n", open_results == OPEN_FOUND);

    if (open_results == OPEN_FOUND) {
        res = 0;
    } else {
        res = -ENOENT;
    }

    elfuse_function_waiting = NONE;
    pthread_mutex_unlock(&elfuse_mutex);

    return res;
}

static int elfuse_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
    size_t res;
    (void) fi;

    pthread_mutex_lock(&elfuse_mutex);

    /* Function to call */
    elfuse_function_waiting = READ;

    /* Set function args */
    path_arg = path;
    read_args_offset = offset;
    read_args_size = size;

    /* Wait for the funcall results */
    pthread_cond_wait(&elfuse_cond_var, &elfuse_mutex);

    if (read_results >= 0) {
        fprintf(stderr, "READ received results %s(%d)\n", read_results_data, read_results);
        memcpy(buf, read_results_data, read_results);
        free(read_results_data);
        res = read_results;
    } else {
        fprintf(stderr, "READ did not receive results (%d)\n", read_results);
        res = -ENOENT;
    }

    elfuse_function_waiting = NONE;
    pthread_mutex_unlock(&elfuse_mutex);

    return res;
}

static struct fuse_operations elfuse_oper = {
    .getattr	= elfuse_getattr,
    .readdir	= elfuse_readdir,
    .open	= elfuse_open,
    .read	= elfuse_read,
};

static struct fuse *f;

int
elfuse_fuse_loop(char* mountpath)
{
    int argc = 2;
    char* argv[] = {
        "",
        mountpath
    };
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_chan *ch;
    char *mountpoint;
    int err = -1;

    if (fuse_parse_cmdline(&args, &mountpoint, NULL, NULL) != -1 &&
        (ch = fuse_mount(mountpoint, &args)) != NULL) {

        f = fuse_new(ch, &args, &elfuse_oper, sizeof(elfuse_oper), NULL);
        if (f != NULL) {
            fprintf(stderr, "start loop\n");
            err = fuse_loop(f);
            fprintf(stderr, "stop loop\n");
            fuse_destroy(f);
        }
        fuse_unmount(mountpoint, ch);
    }
    fuse_opt_free_args(&args);

    /* TODO: use an atomic boolean flag here? */
    pthread_mutex_lock(&elfuse_mutex);
    f = NULL;
    pthread_mutex_unlock(&elfuse_mutex);

    fprintf(stderr, "done with the loop\n");

    return err ? 1 : 0;
}

void
elfuse_stop_loop()
{
    pthread_mutex_lock(&elfuse_mutex);
    bool is_looping = f != NULL;
    if (!is_looping) {
        pthread_mutex_unlock(&elfuse_mutex);
        return;
    }
    fuse_exit(f);
    pthread_mutex_unlock(&elfuse_mutex);

    while (is_looping) {
        sleep(1);
        fprintf(stderr, "not yet exited\n");
        pthread_mutex_lock(&elfuse_mutex);
        is_looping = f != NULL;
        pthread_mutex_unlock(&elfuse_mutex);
    }
    fprintf(stderr, "exited\n");
}
