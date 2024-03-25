/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>
#include <uio.h>


/*
 * Put your function declarations and data types here ...
 */


/* File descriptor structure, each process has its own array of file descriptors */
struct file_descriptor {
    int in_use;            // Indicates if the file descriptor is in use (0 for unused, 1 for used)
    int global_index;      // Index in the global open file table
};

/* File descriptor table structure for each process */
struct fd_table {
    struct file_descriptor fd_array[OPEN_MAX]; // Index is the file descriptor
};

/* Structure for a single open file */
struct open_file {
    struct vnode *vnode;   // Pointer to the file's vnode
    off_t offset;          // Current read/write position
    int ref_count;         // Reference count
    int flag;              // File flags 
};

/* Global open file table structure */
struct open_file_table {
    struct open_file *files[OPEN_MAX]; // Array of pointers to open files
};


struct open_file_table *oft;


int of_table_init(void);
int fd_table_init(struct fd_table **fdt);
void fd_table_destory(struct fd_table *fdt);
int setup_stdout_stderr(void);

/* Syscalls*/
int sys_open(userptr_t pathname, int flags, mode_t mode, int *retval);
int sys_close(int fd);
int sys_read(int fd, void *buf, size_t buflen, size_t *retval);
int sys_write(int fd, void *buf, size_t buflen, size_t *retval);

/* helper function*/
int create_open_file(char *path, int flags, mode_t mode, struct open_file **ret);
int insert_open_file(struct open_file *of, int *global_index);
int allocate_fd(struct fd_table *fdt, int global_index);
int fd_check(int fd);
int open_console(char *path, int flags, struct open_file **of_ret);
int init_fd_for_console(struct fd_table *fdt, struct open_file *of, int fd_target);
int sys_rw(int fd, void *buf, size_t buflen, enum uio_rw flag, size_t *retval);





#endif /* _FILE_H_ */
