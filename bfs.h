#include <time.h>
#include <sys/stat.h>

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
struct wfs_sb {
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
struct wfs_inode {
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
struct wfs_dentry {
    char name[MAX_NAME];
    int num;
};


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

