#ifndef ELFUSE_FUSE_H
#define ELFUSE_FUSE_H

#include <pthread.h>

extern pthread_mutex_t elfuse_mutex;
extern pthread_cond_t elfuse_cond_var;

/* TODO: Prefix the values */
extern enum elfuse_function_waiting_enum {
    WAITING_NONE,
    WAITING_READDIR,
    WAITING_GETATTR,
    WAITING_OPEN,
    WAITING_READ,
} elfuse_function_waiting;

extern const char *args_path;

/* GETATTR results */
extern enum elfuse_getattr_result_enum {
    GETATTR_FILE,
    GETATTR_DIR,
    GETATTR_UNKNOWN,
} getattr_results;
extern size_t getattr_results_file_size;

/* READIR results */
extern char **readdir_results;
extern size_t readdir_results_size;

/* OPEN results */
extern enum elfuse_open_result_enum {
    OPEN_FOUND,
    OPEN_UNKNOWN,
} open_results;

/* READ args and results */
extern size_t args_read_offset;
extern size_t args_read_size;
extern int read_results;
extern char *read_results_data;

int
elfuse_fuse_loop(char* mountpath);

void
elfuse_stop_loop();

#endif //ELFUSE_FUSE_H
