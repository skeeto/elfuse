#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

#include "elfuse_fuse.h"

pthread_mutex_t elfuse_mutex = PTHREAD_MUTEX_INITIALIZER;

enum elfuse_function_waiting_enum elfuse_function_waiting = NONE;

static const char *hello_str = "Hello World!\n";
static const char *hello_path = "/hello";

const char *path_arg;

/* READIR args and results */
char **readdir_results;
size_t readdir_results_size;

/* GETATTR args and results */
enum elfuse_getattr_result_enum getattr_results;
size_t getattr_results_file_size;

/* OPEN args and results */
enum elfuse_open_result_enum open_results;


static int hello_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;

    pthread_mutex_lock(&elfuse_mutex);
    elfuse_function_waiting = GETATTR;
    path_arg = path;
    pthread_mutex_unlock(&elfuse_mutex);

    memset(stbuf, 0, sizeof(struct stat));

    while (true) {
        pthread_mutex_lock(&elfuse_mutex);

        if (elfuse_function_waiting == GETATTR) {
            pthread_mutex_unlock(&elfuse_mutex);
            fprintf(stderr, "GETATTR still waiting\n");
            sleep(1);
            continue;
        }

        if (elfuse_function_waiting == READY) {

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
            break;
        }

        assert(false);
    }

    return res;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    /* TODO: Wait if busy */
    pthread_mutex_lock(&elfuse_mutex);
    elfuse_function_waiting = READDIR;
    path_arg = path;
    pthread_mutex_unlock(&elfuse_mutex);

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    while (true) {
        pthread_mutex_lock(&elfuse_mutex);

        if (elfuse_function_waiting == READDIR){
            pthread_mutex_unlock(&elfuse_mutex);
            fprintf(stderr, "READDIR still waiting\n");
            sleep(1);
            continue;
        }

        if (elfuse_function_waiting == READY) {
            fprintf(stderr, "READDIR received results\n");
            for (size_t i = 0; i < readdir_results_size; i++) {
                filler(buf, readdir_results[i], NULL, 0);
            }
            elfuse_function_waiting = NONE;
            pthread_mutex_unlock(&elfuse_mutex);
            break;
        }

        assert(false);
    }

    return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi)
{
    if ((fi->flags & 3) != O_RDONLY)
        return -EACCES;

    pthread_mutex_lock(&elfuse_mutex);
    elfuse_function_waiting = OPEN;
    path_arg = path;
    pthread_mutex_unlock(&elfuse_mutex);

    int res = 0;
    while (true) {
        pthread_mutex_lock(&elfuse_mutex);

        if (elfuse_function_waiting == OPEN) {
            pthread_mutex_unlock(&elfuse_mutex);
            fprintf(stderr, "OPEN still waiting\n");
            sleep(1);
            continue;
        }

        if (elfuse_function_waiting == READY) {
            fprintf(stderr, "OPEN received results (%d)\n", open_results == OPEN_FOUND);

            if (open_results == OPEN_FOUND) {
                elfuse_function_waiting = NONE;
                pthread_mutex_unlock(&elfuse_mutex);
                res = 0;
            } else {
                elfuse_function_waiting = NONE;
                pthread_mutex_unlock(&elfuse_mutex);
                res = -ENOENT;
            }

            break;
        }

        assert(false);
    }

    return res;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	size_t len;
	(void) fi;
	if(strcmp(path, hello_path) != 0)
		return -ENOENT;

	len = strlen(hello_str);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, hello_str + offset, size);
	} else
		size = 0;

	return size;
}

static struct fuse_operations hello_oper = {
	.getattr	= hello_getattr,
	.readdir	= hello_readdir,
	.open		= hello_open,
	.read		= hello_read,
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

        f = fuse_new(ch, &args, &hello_oper, sizeof(hello_oper), NULL);
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
        sleep(0.1);
        fprintf(stderr, "not yet exited\n");
        pthread_mutex_lock(&elfuse_mutex);
        is_looping = f != NULL;
        pthread_mutex_unlock(&elfuse_mutex);
    }
    fprintf(stderr, "exited\n");
}