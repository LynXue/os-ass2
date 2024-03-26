#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>

#include <kern/unistd.h>

/*
 * Add your file-related functions here ...
 */

/**
 * Initialize global open file table
 */

int
of_table_init(void) {

    /* Initialize the global open file table if it hasn't been initialized already */
    if (oft == NULL) {
        oft = kmalloc(sizeof(struct open_file_table));
        if (oft == NULL) {
            return ENOMEM; // Out of memory
        }

        for (int i = 0; i < OPEN_MAX; i++) {
            oft->files[i] = NULL; // Initialize all entries to NULL
        }
    }
    // kprintf("oepn file table init success.\n");
    return 0; // Indicate success
}

/**
 * Initialize file descriptor table for a process
 */
int
fd_table_init(struct fd_table **fdt) {
    *fdt = kmalloc(sizeof(struct fd_table));
    if (*fdt == NULL) {
        return ENOMEM;
    }

    /* Empty the new table */
    for (int i = 0; i < OPEN_MAX; i++) {
        (*fdt)->fd_array[i].in_use = 0;        // Set to unused
        (*fdt)->fd_array[i].global_index = -1; // Set to an invalid index
    }

    // kprintf("fd table init success\n");
    return 0;
}

/*
 * Destroy the process fd table
 */
void
fd_table_destory(struct fd_table *fdt) {
    /* Ensure all the files are closed before freeing the fd table */
    for (int i = 0; i < OPEN_MAX; i++) {
        if (fdt->fd_array[i].in_use != 0) // Check if the file descriptor is in use
        {
            // Close the file via its global index
            sys_close(fdt->fd_array[i].global_index);
        }
    }

    /* Free the memory allocated for the fd table */
    kfree(fdt);
}

int
open_console(char *path, int flags, struct open_file **of_ret) {
    int result;
    struct open_file *of;

    result = create_open_file(path, flags, 0, &of);
    if (result != 0) {
        return result;
    }

    *of_ret = of;

    return 0;
}

int
init_fd_for_console(struct fd_table *fdt, struct open_file *of, int fd_target) {
    int result, global_index;

    // Insert the opened file into the global open file table
    result = insert_open_file(of, &global_index);
    if (result) {
        // vfs_close(of->vnode); 在insert_open_file() 中关闭了文件，relsease memory，这边不需要
        // kfree(of);
        // kprintf("insert into open file table failed\n");
        return result;
    }
    // kprintf("std insert success\n");
    // Allocate a file descriptor for the process
    int fd = allocate_fd(fdt, global_index);
    // kprintf("fd = %d\n", fd);
    if (fd != fd_target) {
        vfs_close(of->vnode);
        kfree(of);
        // kprintf("failed for allocating fd\n");
        return EMFILE; // Failed to allocate file descriptor or does not match the target file descriptor
    }

    return 0;
}

int
setup_stdout_stderr(void) {
    struct open_file *of_stdout, *of_stderr;
    int result;
    char conname[5];

    // Open the console as stdout
    strcpy(conname, "con:");
    result = open_console(conname, O_WRONLY, &of_stdout);
    if (result != 0) {
        return result;
    }
    // Initialize stdout
    result = init_fd_for_console(curproc->fdtable, of_stdout, STDIN_FILENO);
    if (result != 0) {
        return result;
    }

    // Open the console as stdout
    strcpy(conname, "con:");
    result = open_console(conname, O_WRONLY, &of_stdout);
    if (result != 0) {
        return result;
    }
    // Initialize stdout
    result = init_fd_for_console(curproc->fdtable, of_stdout, STDOUT_FILENO);
    if (result != 0) {
        return result;
    }
    // Open the console as stderr
    strcpy(conname, "con:");
    result = open_console(conname, O_WRONLY, &of_stderr);
    if (result != 0)
        return result;

    // Initialize stderr
    result = init_fd_for_console(curproc->fdtable, of_stderr, STDERR_FILENO);
    if (result != 0)
        return result;

    return 0;
}

int
sys_open(userptr_t pathname, int flags, mode_t mode, int *retval) {
    struct open_file *of = NULL;
    char *path;
    int result, fd_index, global_index;
    *retval = -1;

    // Check for any unknown flags */
    if ((flags & VALID_FLAGS) != flags) {
        return EINVAL; // unknown flags
    }

    path = kmalloc(PATH_MAX);
    if (path == NULL) {
        return ENOMEM; // out of memory error
    }

    // Copy file path from user space to kernel space
    result = copyinstr(pathname, path, PATH_MAX, NULL);
    if (result) {
        kfree(path);
        return result; // Return the error if path copying fails
    }

    // Create and open the file
    result = create_open_file(path, flags, mode, &of);
    kfree(path);
    if (result) {
        return result;
    }

    // Insert the file into the open file table
    result = insert_open_file(of, &global_index);
    if (result) {
        return result;
    }

    // Allocate a file descriptor for process
    fd_index = allocate_fd(curproc->fdtable, global_index);
    if (fd_index < 0) {       // allocate failed
        vfs_close(of->vnode); // Close the vnode
        kfree(of);
        return EMFILE; // Too many open files in process
    }

    *retval = fd_index; // Set the file descriptor as the return value
    return 0;
}

/**
 * create the open file
 */
int
create_open_file(char *path, int flags, mode_t mode, struct open_file **ret) {
    struct vnode *vn;
    int result = vfs_open(path, flags, mode, &vn);
    if (result) {
        return result;
    }

    struct open_file *of = kmalloc(sizeof(struct open_file));
    if (of == NULL) {
        vfs_close(vn);
        return ENOMEM; // out of memory error
    }

    of->vnode = vn;
    of->offset = 0;    // Initialize the file offset to 0
    of->ref_count = 1; // Initialize the reference count to 1
    of->flag = flags;  // Store the flags

    *ret = of; // Set the return value to the new open_file structure
    return 0;
}

/**
 * Insert the open file into the global open file table
 */
int
insert_open_file(struct open_file *of, int *global_index) {
    for (int i = 0; i < OPEN_MAX; i++) {
        if (oft->files[i] == NULL) {
            oft->files[i] = of; // Insert the open file into the first available slot
            *global_index = i;  // Set the global index for the caller
            // kprintf("stdout insert into open file success\n");
            return 0; // Insert success
        }
    }
    vfs_close(of->vnode); // Insert failed, close file, release memory
    kfree(of);
    // kprintf("insert into open file table failed\n");
    return ENFILE; // Too many open files in system
}

/**
 * Allocate file descripter for process;
 */
int
allocate_fd(struct fd_table *fdt, int global_index) {
    for (int i = 0; i < OPEN_MAX; i++) {
        if (fdt->fd_array[i].in_use == 0) {
            fdt->fd_array[i].in_use = 1;                  // Mark the file descriptor as in use
            fdt->fd_array[i].global_index = global_index; // Set the global index
            return i;                                     // Return the file descriptor index
        }
    }
    return -1; // allocate failed
}

int
sys_close(int fd) {
    // Check file descriptor
    int global_index = fd_check(fd);
    if (global_index < 0) {
        return EBADF; // Bad file number
    }

    struct open_file *of = oft->files[global_index]; // Use the correct reference to the global open file table

    vfs_close(of->vnode); // Close the vnode associated with the file

    // Update global open file table
    // Reduce ref_count
    of->ref_count--;
    // Clean up the global open file table entry if ref_count is 0
    if (of->ref_count == 0) {
        kfree(of);
        oft->files[global_index] = NULL;           // Clear the entry in the global open file table
        curproc->fdtable->fd_array[fd].in_use = 0; // mark the slot unused
        curproc->fdtable->fd_array[fd].global_index = -1;
    }

    return 0; // Indicate success
}

int
fd_check(int fd) {
    // Check the range of fd
    if (fd < 0 || fd >= OPEN_MAX) {
        return -1; // fd is out of valid range
    }

    if (curproc->fdtable->fd_array[fd].in_use == 0) {
        return -1; // fd is not in use, hence invalid
    }

    // Get global index from the process's file descriptor table
    int global_index = curproc->fdtable->fd_array[fd].global_index;
    // Check if the corresponding open file entry exists in the global open file table
    if (oft->files[global_index] == NULL) {
        return -1; // The open file entry does not exist, indicating an invalid fd
    }

    return global_index; // Return the global index for the open file
}

int
sys_rw(int fd, void *buf, size_t buflen, enum uio_rw flag, size_t *retval) {
    *retval = -1;
    // check file descriptor number
    int global_index = fd_check(fd);
    if (global_index < 0) {
        return EBADF;
    }

    struct open_file *of = oft->files[global_index];

    /* check if the file support read or write */
    if ((flag == UIO_WRITE && (of->flag & O_ACCMODE) != O_WRONLY && (of->flag & O_ACCMODE) != O_RDWR) ||
        (flag == UIO_READ && (of->flag & O_ACCMODE) != O_RDONLY && (of->flag & O_ACCMODE) != O_RDWR)) {

        return EINVAL;
    }

    struct vnode *vn = of->vnode;
    struct iovec iov;
    struct uio myuio;
    off_t offset = of->offset;

    // check whether read or write
    int result;
    char kernel_buf[buflen];
    if (flag == UIO_WRITE) {
        result = copyin((userptr_t)buf, kernel_buf, buflen);
        if (result) {
            return result;
        }
        /**
         * The user-provided write_buf and the requested number of bytes to write, nbytes,
         * are used to initialize the iovec and uio structures.
         */
        uio_kinit(&iov, &myuio, kernel_buf, buflen, offset, flag);
        result = VOP_WRITE(vn, &myuio);
    } else {
        uio_kinit(&iov, &myuio, buf, buflen, offset, flag);
        result = VOP_READ(vn, &myuio);
    }

    if (result) {
        return result;
    }
    // update offset of the open file
    of->offset = myuio.uio_offset;
    /**
     * After the read or write is completed, the actual number of bytes read is
     * determined by checking the reduction in uio_resid
     */
    *retval = buflen - myuio.uio_resid;

    return 0;
}

int
sys_read(int fd, void *buf, size_t buflen, size_t *retval) {
    return sys_rw(fd, buf, buflen, UIO_READ, retval);
}

int
sys_write(int fd, void *buf, size_t buflen, size_t *retval) {
    return sys_rw(fd, buf, buflen, UIO_WRITE, retval);
}

int
sys_lseek(int fd, off_t offset, int whence, off_t *retval) {
    struct stat file_info;
    off_t file_size; /* file size in bytes */
    off_t position;
    int result;
    *retval = -1;

    // check file descriptor
    int global_index = fd_check(fd);
    if (global_index == -1) {
        return EBADF;
    }

    struct vnode *vnode = oft->files[global_index]->vnode;

    // Check if this file is seekable
    if (!VOP_ISSEEKABLE(vnode)) {
        return ESPIPE; // illegal seek
    }

    // Get file size in struct stat
    result = VOP_STAT(vnode, &file_info);
    if (result) {
        return result;
    }
    file_size = file_info.st_size;

    int cur_offset = oft->files[global_index]->offset;

    // Check whence arguemnt
    if (whence == SEEK_SET) {
        if (offset < 0) {
            return EINVAL; // Invalid argument
        }
        position = offset;
    } else if (whence == SEEK_CUR) {
        if (offset < 0 && KERNEL_ABS(offset) > cur_offset) {
            return EINVAL; // Invalid argument
        }
        position = offset + cur_offset;
    } else if (whence == SEEK_END) {
        if (offset < 0 && KERNEL_ABS(offset) > file_size) {
            return EINVAL; // Invalid argument
        }
        position = offset + file_size;
    } else {
        return EINVAL; // Invalid argument
    }

    // update read/write position in the file
    oft->files[global_index]->offset = position;
    *retval = position;

    return 0;
}

int
sys_dup2(int old_fd, int new_fd, int *retval) {
    *retval = -1;

    // Check file descriptor
    if (old_fd < 0 || old_fd >= OPEN_MAX || new_fd < 0 || new_fd >= OPEN_MAX) {
        return EBADF;
    }

    // Check if old_fd is opened
    int global_index = fd_check(old_fd);
    if (global_index == -1) {
        return EBADF;
    }

    if (old_fd == new_fd) {
        *retval = new_fd;
    } else {
        if (curproc->fdtable->fd_array[new_fd].global_index != -1) { // new_fd opened, need to close it first
            int result = sys_close(new_fd);
            if (result) {
                return EBADF;
            }
        }
        curproc->fdtable->fd_array[new_fd] = curproc->fdtable->fd_array[old_fd];
        oft->files[global_index]->ref_count++;
        *retval = new_fd;
    }

    return 0;
}
