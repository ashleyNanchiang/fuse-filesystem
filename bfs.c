#include "bfs.h"

#define MAX_FUSE_ARG 10

int raid;

int disk_index; // total number of disks
void *disks[MAX_DISKS];

// 0 if available, 1 if allocated
int *raid0_dbit;
int raid0_bnum;

struct fuse_operations ops = {
  .getattr = bfs_getattr,
  .mknod   = bfs_mknod,
  .mkdir   = bfs_mkdir,
  .unlink  = bfs_unlink,
  .rmdir   = bfs_rmdir,
  .read    = bfs_read,
  .write   = bfs_write,
  .readdir = bfs_readdir
};

int main(int argc, char *argv[]) {

  char *fuse_argv[MAX_FUSE_ARG];
  for(int i = 0; i < MAX_FUSE_ARG; i++)
    fuse_argv[i] = malloc(sizeof(char) * MAX_NAME);

  char root_name[MAX_NAME];

  char disk_names[MAX_DISKS][MAX_NAME];

  int index = 1;
  int index_fuse_arg = 0;
  disk_index = 0;
  int fd[MAX_DISKS];

  int loop = 0;
  while(index < argc) {  

    loop++;
    if(argv[index][0] == '-') {
      strcpy(fuse_argv[index_fuse_arg], argv[index]);
      index_fuse_arg ++;
    }
    else if(index != argc - 1) {

      strcpy(disk_names[disk_index], argv[index]);

      fd[disk_index] = open(argv[index], O_RDWR);

      if(fd[disk_index] < 0)
        return -1;

      struct stat curr_disk;
      open(argv[index], O_RDWR);
      fstat(fd[disk_index], &curr_disk);
      disks[disk_index]= mmap(NULL, curr_disk.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd[disk_index], 0);

      disk_index ++;
    }
    else {
      strcpy(root_name, argv[index]);
      strcpy(fuse_argv[index_fuse_arg], realpath(argv[index], NULL));
      index_fuse_arg ++;
    }
    index++;
  }

  // first pull all the disk names from the mmap regions
  // and put into a temp array
  // then sort it and update pointers in disks[]
  char disks_name_temp[disk_index][MAX_NAME];
  for(int d = 0; d < disk_index; d++) {
    char *name_ptr = (char*)((char*)disks[0] + 52 + MAX_NAME * d);
    memcpy(disks_name_temp[d], name_ptr, MAX_NAME);
  }

  // temp ptr array to mmap regions
  void *disks_temp[MAX_DISKS];
  for(int d = 0; d < disk_index; d++) {
    disks_temp[d] = disks[d];
  }

  //sorting
  for(int i = 0; i < disk_index; i++) {
    for(int j = 0; j < disk_index; j++) {
      if(strcmp(disks_name_temp[i], disk_names[j]) == 0) {
        disks[i] = disks_temp[j];
      }
    }
  }

  if(disks[0] == 0)
    return -1;
  raid = *((char*)disks[0] + 48);
  
  size_t dblock_num = *(off_t*)((char*)disks[0] + 8);

  raid0_bnum = disk_index * dblock_num;

  raid0_dbit = calloc(raid0_bnum, sizeof(int));

  return fuse_main(index_fuse_arg, fuse_argv, &ops, NULL);
}



