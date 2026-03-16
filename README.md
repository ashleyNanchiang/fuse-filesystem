# Filesystem in FUSE

This is a simple filesystem developed in C using FUSE. This project is developed to understand how to implement filesystem operations.

## Features
 + **Support RAID1 and RAID0**
 + **Create Files and directories**
 + **Read directories**
 + **Read and write to files**
 + **Remove files and directories**
 + **Get attributes of a file or directory**

## Technical Details
 + **Disk Layout**: Contains a superblock, bitmaps, inode table, and data blocks.
 + **RAID Logic**: 
   - RAID 0 (Striping): Data blocks are interleaved across disks. In this project metadata uses RAID 1.
   - RAID 1 (Mirrioing): All data and metadata are mirroed across all disks.
   - RAID 1v (Verified Mirroring): Compares the copies across disks and return the majority one.

## Project Structure
 + `bfs.c`: sets up the disks and return to fuse_main
 + `fuse_op.c`: fuse operation implementations, includes getattr, mknod, mkdir, unlink, rmdir, read, write, readdir
 + `fs_logic.c`: helper functions that deal with inodes, data blocks, and raid
 + `mkfs.c`: initializes the filesystem with provided disk image files
 + `bfs.h`: contains constants, data structures, and function signatures

## Usage
### Prerequisites
You'll need to have the FUSE libraries and `pkg-config` installed on your system.
```
sudo apt-get install libfuse-dev pkg-config gcc make
```  

### Compilation
Use the provided Makefile to compile the source files into the `bfs` executable.
```
make clean    # delete previous builds
make          # compile
```  

### Creating a disk image
Before you start using this filesystem, you need to create disk images to act as the physical disks.
Run the provided `create_disk.sh` with 
```
./create_disk.sh
```
to create a disk image of size 1MB.
You can change the size by altering the script, or run it a couple of times for multiple disk images. (Be sure to change the name of the disk images though!)

### Run
1. Use `mkdir mnt` to create a folder you want the filesystem to be mounted.
2. Run `./mkfs -r <raid_mode> -d <disk> -i <inodes> -b <blocks>` to initialize a file to an empty filesystem
   <raid_mode>: `-r 0` for RAID 0 (striping), `-r 1` for RAID 1 (mirroring), `-r 1v` for RAID 1v (verified mirroring)
   <disk>: the name of the disk images file, can have multiple disk images
   <inodes>: the number of inodes
   <blocks>: the number of data blocks (will be rounded to the nearest multiple of 32 to prevent misalignment)
3. Then run `./bfs <disk_img_name> <FUSE_options> mnt`
   <disk>: the name of the disk images file, can have multiple disk images
   <FUSE_options>: recommends `-f` which lets FUSE run in the foreground, and `-s` so it is single-threaded  
4. Use `echo $?` to check return codes. Return code should be `0` if success.

### Unmount
Run with provided script `./umount.sh mnt` to unmount.

### Demo
```
demo:fs$ make clean
rm -rf bfs mkfs
demo:fs$ ./create_disk.sh  # can change script to make creating disk images faster
1+0 records in
1+0 records out
1048576 bytes (1.0 MB, 1.0 MiB) copied, 0.0019105 s, 549 MB/s
demo:fs$ mv disk.img disk1.img
demo:fs$ ./create_disk.sh
1+0 records in
1+0 records out
1048576 bytes (1.0 MB, 1.0 MiB) copied, 0.00208518 s, 503 MB/s
demo:fs$ mv disk.img disk2.img
demo:fs$ make all
gcc -Wall -Werror -pedantic -std=gnu18 -g bfs.c fs_logic.c fuse_op.c bfs.h `pkg-config fuse --cflags --libs` -o bfs
gcc -Wall -Werror -pedantic -std=gnu18 -g `pkg-config fuse --cflags --libs` -o mkfs mkfs.c
demo:fs$ mkdir mnt
demo:fs$ ls
Makefile  README.md  bfs  bfs.c  bfs.h  create_disk.sh  disk1.img  disk2.img  fs_logic.c  fuse_op.c  mkfs  mkfs.c  mnt  umount.sh
demo:fs$ ./mkfs -r 1 -d disk1.img -d disk2.img -i 32 -b 250
demo:fs$ ./bfs disk1.img disk2.img -f -s mnt
demo:fs$ stat mnt
  File: mnt
  Size: 0               Blocks: 0          IO Block: 4096   directory
Device: 0,97    Inode: 1           Links: 0
Access: (0700/drwx------)  Uid: ( 1000/ashley_yuan)   Gid: ( 1000/ashley_yuan)
Access: 2026-03-15 21:48:48.000000000 -0500
Modify: 2026-03-15 21:48:48.000000000 -0500
Change: 2026-03-15 21:48:48.000000000 -0500
 Birth: -
demo:fs$ mkdir mnt/dirA
demo:fs$ stat mnt/dirA
  File: mnt/dirA
  Size: 0               Blocks: 0          IO Block: 4096   directory
Device: 0,97    Inode: 2           Links: 0
Access: (0755/drwxr-xr-x)  Uid: ( 1000/ashley_yuan)   Gid: ( 1000/ashley_yuan)
Access: 2026-03-15 21:49:35.000000000 -0500
Modify: 2026-03-15 21:49:35.000000000 -0500
Change: 2026-03-15 21:49:35.000000000 -0500
 Birth: -
demo:fs$ mkdir mnt/dirB
demo:fs$ ls mnt
dirA  dirB
demo:fs$ echo hello > mnt/text
demo:fs$ cat mnt/text
hello
demo:fs$ ls mnt
dirA  dirB  text
demo:fs$ echo bye > mnt/dirB/text2
demo:fs$ ls mnt/dirB
text2
demo:fs$ stat mnt/dirB
  File: mnt/dirB
  Size: 0               Blocks: 0          IO Block: 4096   directory
Device: 0,97    Inode: 3           Links: 0
Access: (0755/drwxr-xr-x)  Uid: ( 1000/ashley_yuan)   Gid: ( 1000/ashley_yuan)
Access: 2026-03-15 21:51:14.000000000 -0500
Modify: 2026-03-15 21:51:14.000000000 -0500
Change: 2026-03-15 21:51:14.000000000 -0500
 Birth: -
demo:fs$ stat mnt/text
  File: mnt/text
  Size: 6               Blocks: 0          IO Block: 4096   regular file
Device: 0,97    Inode: 4           Links: 0
Access: (0644/-rw-r--r--)  Uid: ( 1000/ashley_yuan)   Gid: ( 1000/ashley_yuan)
Access: 2026-03-15 21:50:26.000000000 -0500
Modify: 2026-03-15 21:50:26.000000000 -0500
Change: 2026-03-15 21:50:26.000000000 -0500
 Birth: -
demo:fs$ rm -r mnt/dirB
demo:fs$ ls mnt
dirA  text
```