#ifndef BFS_H
#define BFS_H

#define FUSE_USE_VERSION 30
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <fuse.h>

#define BLOCK_SIZE (512)
#define MAX_NAME   (28)

#define D_BLOCK    (6)
#define IND_BLOCK  (D_BLOCK+1)
#define N_BLOCKS   (IND_BLOCK+1)
#define MAX_DISKS   10

/*
  The fields in the superblock should reflect the structure of the filesystem.
  `mkfs` writes the superblock to offset 0 of the disk image. 
  The disk image will have this format:

          d_bitmap_ptr       d_blocks_ptr
               v                  v
+----+---------+---------+--------+--------------------------+
| SB | IBITMAP | DBITMAP | INODES |       DATA BLOCKS        |
+----+---------+---------+--------+--------------------------+
0    ^                   ^
i_bitmap_ptr        i_blocks_ptr

*/

// Superblock
struct bfs_sb {
    size_t num_inodes;
    size_t num_data_blocks;
    off_t i_bitmap_ptr;         
    off_t d_bitmap_ptr;
    off_t i_blocks_ptr;
    off_t d_blocks_ptr;
    int raid; // RAID 0: 0, RAID 1: 1, RAID 1v: 2
    char disk_names[MAX_DISKS][MAX_NAME];
};

// Inode
struct bfs_inode {
    int     num;      /* Inode number */
    mode_t  mode;     /* File type and mode */
    uid_t   uid;      /* User ID of owner */
    gid_t   gid;      /* Group ID of owner */
    off_t   size;     /* Total size, in bytes */
    int     nlinks;   /* Number of links */

    time_t atim;      /* Time of last access */
    time_t mtim;      /* Time of last modification */
    time_t ctim;      /* Time of last status change */

    off_t blocks[N_BLOCKS]; // me: stores the index of a data block
};

// Directory entry
// 16 per block
struct bfs_dentry {
    char name[MAX_NAME];
    int num;
};

extern int raid;

extern int disk_index; // total number of disks
extern void *disks[MAX_DISKS];

// 0 if available, 1 if allocated
extern int *raid0_dbit;
extern int raid0_bnum;

int bfs_getattr(const char *path, struct stat *stbuf);
int bfs_mknod(const char* path, mode_t mode, dev_t rdev);
int bfs_mkdir(const char* path, mode_t mode);
int bfs_unlink(const char* path);
int bfs_rmdir(const char* path);
int bfs_read(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi);
int bfs_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi);
int bfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi);

extern struct fuse_operations ops;

// helper functions
off_t find_inode(const char *path);
off_t allocate_inode(mode_t mode);
int find_dblock(int disk, off_t index, off_t **dblock_ptr);
int allocate_dblock(int disk, off_t *inode, off_t **dblock_ptr);
void indblock_init(off_t *block_loc);
void raid0_cleardbit(off_t index);

#endif

/*
    notes:

    ibitmap
    0
    +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
    |  8 |  7 |  6 |  5 |  4 |  3 |  2 |  1 | 16 | 15 | 14 | 13 | 12 | 11 | 10 |  9 |
    +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
    +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
    | 24 | 23 | 22 | 21 | 20 | 19 | 18 | 17 | 32 | 31 | 30 | 29 | 28 | 27 | 26 | 25 |
    +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+



    
size_t: 8
off_t: 8
int: 4
mode_t: 4
uid_t: 4
gid_t: 4
time_t: 8

*/

