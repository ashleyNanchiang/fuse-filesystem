#include "bfs.h"

int main (int argc, char *argv[]) {

  struct bfs_sb *superblock = malloc(sizeof(struct bfs_sb));
  if(superblock == 0)
    return -1;

  char diskNames[MAX_DISKS][MAX_NAME];
  int fd[MAX_DISKS];

  char hasR = 0;
  char hasD = 0;
  char hasI = 0;
  char hasB = 0;

  int index = 1;
  int diskIndex = 0;

  while(index < argc) {

    if(argv[index][0] == '-') {
      switch(argv[index][1]) {

        // get raid
        case 'r':
          if(argv[index+1] != 0 && !hasR) {
            if(strcmp(argv[index+1], "0") == 0) {
              superblock->raid = 0;
              hasR++;
            }
            else if(strcmp(argv[index+1], "1") == 0) {
              superblock->raid = 1;
              hasR++;
            }
            else if(strcmp(argv[index+1], "1v") == 0) {
              superblock->raid = 2;
              hasR++;
            }
            else
              return 1;
          }
          else
            return 1;
          break;

        // get disk names
        case 'd':
          if(argv[index+1] != 0) {
            strcpy(diskNames[diskIndex], argv[index+1]);
            fd[diskIndex] = open(argv[index+1], O_RDWR);
            if(fd[diskIndex] == -1)
              return -1;
            diskIndex++;
            hasD++;
          }
          else
            return 1;
          break;

        // get number of inodes
        case 'i':
          if(argv[index+1] != 0 && !hasI) {
            int inputI = (size_t) atoi(argv[index+1]);
            // make sure the number of inodes are a multiple of 32 (round up)
            if(inputI % 32 != 0) 
              superblock->num_inodes = (size_t) (32 * (inputI / 32 + 1));
            else 
              superblock->num_inodes = (size_t) inputI;
            hasI++;
          }
          else
            return 1;
          break;

        // get number of blocks
        case 'b':
          if(argv[index+1] != 0 && !hasB) {
            int inputB = atoi(argv[index+1]);
            // make sure the number of blocks are a multiple of 32 (round up)
            if(inputB % 32 != 0) 
              superblock->num_data_blocks = (size_t) (32 * (inputB / 32 + 1));
            else 
              superblock->num_data_blocks = (size_t) inputB;
            hasB++;
          }
          else
            return 1;
          break;
        // should not be here, error
        default:
          return 1;
      }
      index+=2;
    }
    // should not be here, error
    else {
      return 1;
    }
  }

  // check if every argument is there
  if(!hasD || !hasR || !hasB || !hasI)
    return 1;
  // check if there is over 2 disks
  if(hasD < 2)
    return 1; 
  

  struct bfs_inode *root_inode = malloc(sizeof(struct bfs_inode));
  if(root_inode == 0)
    return -1;
  root_inode->num = 0;
  root_inode->mode = S_IFDIR | S_IRWXU;
  root_inode->uid = getuid();
  root_inode->gid = getgid();
  root_inode->size = 0;
  root_inode->nlinks = 1;
  time_t current_time;
  time(&current_time);
  root_inode->atim = current_time;
  root_inode->mtim = current_time;
  root_inode->ctim = current_time;

  for(int i = 0; i < N_BLOCKS; i++) {
    root_inode->blocks[i] = -1;
  }

  for(int d = 0; d < diskIndex; d++) {
    strcpy(superblock->disk_names[d], diskNames[d]);
  }
  
  for(int i = 0; i < diskIndex; i++) {

    // check disk size
    struct stat curr_disk;
    if(fstat(fd[i], &curr_disk) == -1)
      return -1;
    size_t size_limit = superblock->num_data_blocks * BLOCK_SIZE + superblock->num_inodes * BLOCK_SIZE;

    int ibitmap_size = superblock->num_inodes / 8;
    int dbitmap_size = superblock->num_data_blocks / 8;

    size_limit += sizeof(struct bfs_sb) + ibitmap_size + dbitmap_size;

    if(curr_disk.st_size < size_limit)
      return -1;

    lseek(fd[i], sizeof(struct bfs_sb), SEEK_SET);
    
    lseek(fd[i], 0, SEEK_SET);
    superblock->i_bitmap_ptr = lseek(fd[i], sizeof(struct bfs_sb), SEEK_CUR);

    char num = 0;
    for(int j = 0; j < ibitmap_size; j++) {
      if(write(fd[i], &num, sizeof(char)) == -1)
        return -1;
    }

    superblock->d_bitmap_ptr = lseek(fd[i], 0, SEEK_CUR);
    
    for(int k = 0; k < dbitmap_size; k++) {
      if(write(fd[i], &num, sizeof(char)) == -1)
        return -1;
    }

    off_t currentPos = lseek(fd[i], 0, SEEK_CUR);
    if (currentPos % BLOCK_SIZE != 0)
      currentPos = (currentPos / BLOCK_SIZE + 1) * BLOCK_SIZE;
    superblock->i_blocks_ptr = lseek(fd[i], currentPos, SEEK_SET);

    int total_size_of_inodes = superblock->num_inodes * BLOCK_SIZE;

    superblock->d_blocks_ptr = lseek(fd[i], total_size_of_inodes, SEEK_CUR);

    lseek(fd[i], superblock->i_blocks_ptr, SEEK_SET);

    if(write(fd[i], &root_inode->num, sizeof(int)) == -1)
      return -1;
    if(write(fd[i], &root_inode->mode, sizeof(mode_t)) == -1)
      return -1;
    if(write(fd[i], &root_inode->uid, sizeof(uid_t)) == -1)
      return -1;
    if(write(fd[i], &root_inode->gid, sizeof(gid_t)) == -1)
      return -1;
    if(write(fd[i], &root_inode->size, sizeof(off_t)) == -1)
      return -1;
    if(write(fd[i], &root_inode->nlinks, sizeof(int)) == -1)
      return -1;
    lseek(fd[i], 4, SEEK_CUR);
    if(write(fd[i], &root_inode->atim, sizeof(time_t)) == -1)
      return -1;
    if(write(fd[i], &root_inode->mtim, sizeof(time_t)) == -1)
      return -1;
    if(write(fd[i], &root_inode->ctim, sizeof(time_t)) == -1)
      return -1; 
    for(int m = 0; m < N_BLOCKS; m++) {
      if(write(fd[i], &root_inode->blocks[m], sizeof(off_t)) == -1)
        return -1;      
    }

    // set root inode bit to 1
    lseek(fd[i], superblock->i_bitmap_ptr, SEEK_SET);

    num = 1;
    write(fd[i], &num, 1);
    
    lseek(fd[i], 0, SEEK_SET);
    write(fd[i], superblock, sizeof(struct bfs_sb));
    lseek(fd[i], 0, SEEK_SET);

  }
  

  free(root_inode);
  free(superblock);
  //printf("mkfs success\n");
  return 0;
}