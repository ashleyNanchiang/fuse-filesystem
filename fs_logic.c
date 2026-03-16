#include "bfs.h"

// return offset from start
off_t allocate_inode(mode_t mode) {

  int empty_inode;

  off_t new_inode_off = 0;
  
  for(int disk_num = 0; disk_num < disk_index; disk_num++) {
    empty_inode = -1;
    
    off_t i_bitmap_ptr_off = *((off_t*)disks[disk_num] + 16 / sizeof(off_t)); // sizeof(size_t) * 2
    off_t *i_bitmap_ptr = (off_t*) ((char*)disks[disk_num] + i_bitmap_ptr_off);
    off_t i_blocks_ptr_off = *((off_t*)disks[disk_num] + 32 / sizeof(off_t)); // sizeof(size_t) * 2 + sizeof(off_t) * 2
    off_t *i_blocks_ptr = (off_t*) ((char*)disks[disk_num] + i_blocks_ptr_off);

    size_t num_inodes = *((size_t*)disks[disk_num]);

    int byte = num_inodes / 8;

    for(int i = 0; i < byte; i++) {

      for(int j = 0; j < 8; j++) {

        if(empty_inode != -1)
          continue;
        if((*((char*)i_bitmap_ptr + i) & (1 << j)) == 0) {

          *((char*)i_bitmap_ptr + i) |= (1 << j);
          empty_inode = (8 * i) + (j % 8);

        }
      }
    }

    if(empty_inode == -1)
      return -ENOSPC;

    struct bfs_inode new_inode;
    new_inode.num = empty_inode;
    new_inode.mode = mode;
    new_inode.uid = getuid();
    new_inode.gid = getgid();
    new_inode.size = 0;
    new_inode.nlinks = 1;
    time_t current_time;
    time(&current_time);
    new_inode.atim = current_time;
    new_inode.mtim = current_time;
    new_inode.ctim = current_time;
    
    i_blocks_ptr = i_blocks_ptr + BLOCK_SIZE * empty_inode / sizeof(off_t);

    new_inode_off = (off_t) ((char*)i_blocks_ptr - (char*)disks[disk_num]);

    *(int*)((char*)i_blocks_ptr) = new_inode.num;
    *(mode_t*)((char*)i_blocks_ptr + 4) = new_inode.mode;
    *(uid_t*)((char*)i_blocks_ptr + 8) = new_inode.uid;
    *(gid_t*)((char*)i_blocks_ptr + 12) = new_inode.gid;
    *(off_t*)((char*)i_blocks_ptr + 16) = new_inode.size;
    *(int*)((char*)i_blocks_ptr + 24) = new_inode.nlinks;
    *(time_t*)((char*)i_blocks_ptr + 32) = new_inode.atim;
    *(time_t*)((char*)i_blocks_ptr + 40) = new_inode.mtim;
    *(time_t*)((char*)i_blocks_ptr + 48) = new_inode.ctim;
    memset((char*)i_blocks_ptr + 56, -1, N_BLOCKS * sizeof(off_t));
  }

  if(new_inode_off == 0)
    return -ENOSPC;
  else
    return new_inode_off;
}

/*
 * parse path and return the offset from start of disk mmap to inode
 */
off_t find_inode(const char *path) {
  
  // offset from start to root inode
  off_t curr_inode_off = *(off_t*)((char*)disks[0] + 32); // sizeof(size_t) * 2 + sizeof(off_t) * 2, the first inode/root inode
  // pointer to root inode of disk[0]
  off_t *curr_inode = (off_t*)((char*)disks[0] + curr_inode_off);

  if(path == NULL)
    return curr_inode_off;

  char *path_cpy = malloc(strlen(path) * sizeof(char) + sizeof(char));
  strcpy(path_cpy, path);

  char *current = strtok(path_cpy, "/");

  off_t i_bitmap_ptr_off = *(off_t*)((char*)disks[0] + 16); // sizeof(size_t) * 2
  off_t *i_bitmap_ptr = (off_t*)((char*)disks[0] + i_bitmap_ptr_off);

  off_t i_blocks_ptr_off = *(off_t*)((char*)disks[0] + 32); // sizeof(size_t) * 2 + sizeof(off_t) * 3
  off_t *i_blocks_ptr = (off_t*)((char*)disks[0] + i_blocks_ptr_off);

  int inode_num = -1;
  
  // root
  if(current == NULL) {
    return curr_inode_off;
  }

  int loop = 0;
  while(current != NULL) {
    loop++;
    if(loop > 100)
      break;
    
    // loop through all blocks of the current inode
    int found = 0;

    // loop through direct blocks
    for(int i = 0; i <= D_BLOCK; i++) {
      if(found == 1)
        continue;
      // pointer to current data block
      off_t *curr_dblock_array;
      
      curr_dblock_array = (off_t*) ((char*)curr_inode + 56 + i * 8);
      // no data block
      if(*curr_dblock_array < 0)
        continue;
      
      // first check if the current block is valid
      int dindex = *curr_dblock_array;

      off_t *dblock_loc = malloc(sizeof(off_t));
      find_dblock(0, dindex, &dblock_loc);

      for(int j = 0; j < 16; j++) {

        char dirName[MAX_NAME];
        off_t *curr_dblock = (off_t*) ((char*)dblock_loc + j * 32);

        char *name_ptr = (char*)curr_dblock;

        if(*curr_dblock == 0 || found == 1)
          continue;

        strcpy(dirName, name_ptr);

        if(strcmp(dirName, current) == 0) {

          inode_num = *((char*)curr_dblock + MAX_NAME);
          size_t num_inodes = *((size_t*)disks[0]);
          int byte = num_inodes / 8;

          for(int x = 0; x < byte; x++) {
            for(int y = 0; y < 8; y++) {
              if((*((char*)i_bitmap_ptr + x) & (1 << y)) == (1 << y)) {
                int index = x * 8 + y;
                off_t *inode_check = (off_t*) ((char*)i_blocks_ptr + BLOCK_SIZE * index);
                if(*(int*)inode_check == inode_num) {
                  found = 1;
                  curr_inode_off = (off_t) ((char*)inode_check - (char*)disks[0]);
                  curr_inode = inode_check;
                }
              }
            }
          }
        }
      }
    }

    // loop through indirect blocks
    if(!found) {
      off_t *curr_indblock = (off_t*) ((char*)curr_inode + 56 + IND_BLOCK * sizeof(off_t));
      if(*curr_indblock != -1) {
        int dindex = *curr_indblock;
        int byteOff = dindex / 8;
        int bitOff = dindex % 8;
        int mask = 1 << bitOff;
        if((*((char*)i_bitmap_ptr + byteOff) | mask) == 1) {
          off_t *curr_dblock = malloc(sizeof(off_t));
          find_dblock(0, dindex, &curr_dblock);

          for(int j = 0; j < BLOCK_SIZE; j += sizeof(off_t)) {          
            off_t *curr_dblock_it = (off_t*) ((char*) *curr_dblock + j);

            if(*curr_dblock_it == -1)
              continue;

            int dindex = *curr_dblock_it;
            int byteOff = dindex / 8;
            int bitOff = dindex % 8;
            int mask = 1 << bitOff;
            if((*((char*)i_bitmap_ptr + byteOff) | mask) == 0) // if the data block bit map is 0
              continue;

            for(int k = 0; k < 16; k++) {
              char dirName[MAX_NAME];
              curr_dblock_it = (off_t*) ((char*)*curr_dblock_it + k * 32);
              strcpy(dirName, (char*)*curr_dblock_it);
              if(strcmp(dirName, current) == 0) {
                inode_num = *((char*)*curr_dblock_it + MAX_NAME);
                curr_inode_off = *((off_t*)disks[0] + 32 / sizeof(off_t) + BLOCK_SIZE * inode_num / sizeof(off_t));
                curr_inode = (off_t*)disks[0] + curr_inode_off / sizeof(off_t);
                found = 1;
              }
            }
          }
        } 
      }

    }

    if(!found)
      return -ENOENT;

    current = strtok(NULL, "/");
  }

  if(inode_num != -1) {
    return curr_inode_off;
  }
  else {
    return -ENOENT;
  }

}

// given the index of a block, return a pointer to the data block
// return value in dblock_ptr
int find_dblock(int disk, off_t index, off_t **dblock_ptr) {
  // if striping, returns the location of block on a certain disk
  if(raid == 0) {
    disk = index % disk_index;
    off_t offset = index / disk_index;
    off_t d_blocks_ptr_off = *(off_t*)((char*)disks[disk] + 40); // sizeof(size_t) * 2 + sizeof(off_t) * 3
    *dblock_ptr = (off_t*)((char*) disks[disk] + d_blocks_ptr_off + BLOCK_SIZE * offset);
  }
  // if mirroring, return the location of block on a giver disk
  else {
    off_t d_blocks_ptr_off = *(off_t*)((char*)disks[disk] + 40); // sizeof(size_t) * 2 + sizeof(off_t) * 3
    *dblock_ptr = (off_t*)((char*) disks[disk] + d_blocks_ptr_off + BLOCK_SIZE * index);;
  }
  return 0;
}

// find a spot to allocate a data block, then set d bit map
// only updating d bit map and inode block array
// return pointer to the new block
// first find an empty spot in dbitmap
// if raid 1 / 1v, stay the way the method is, the calling function should put this in a loop
// if raid 0, the calling function should only call this once for a block across all disks
int allocate_dblock(int disk, off_t *inode, off_t **dblock_ptr) {

  // if raid 0, first find available block in raid0_dbit
  // then update the correct d bit in the specific disk
  // then after, update all inodes block array in all disks

  // if raid 1 or 1v, do what this method is already doing

  // first find available slot in d bit map
  off_t d_bitmap_ptr_off;
  off_t *d_bitmap_ptr;
  size_t dblock_num;
  int byte;
  off_t index;

  if(raid == 0) {
    index = -1;

    for(int i = 0; i < raid0_bnum; i++) {
      if (index != -1)
        break;
      if(raid0_dbit[i] == 0) {
        
        int disk_no = i % disk_index;
        int disk_off = i / disk_index;

        d_bitmap_ptr_off = *(off_t*)((char*)disks[disk_no] + 24);
        d_bitmap_ptr = (off_t*)((char*)disks[disk_no] + d_bitmap_ptr_off);
        dblock_num = *(size_t*)((char*)disks[disk_no] + 8);
        
        index = i;
        byte = disk_off / 8;
        int bit = disk_off % 8;

        raid0_dbit[i] = 1;

        char *byte_ptr = (char*)d_bitmap_ptr + byte;
        *byte_ptr |= 1 << bit;
      }
    }

  } 
  else {
    d_bitmap_ptr_off = *(off_t*)((char*)disks[disk] + 24);
    d_bitmap_ptr = (off_t*)((char*)disks[disk] + d_bitmap_ptr_off);
    dblock_num = *(size_t*)((char*)disks[disk] + 8);

    byte = dblock_num / 8;
    index = -1;
    for(int i = 0; i < byte; i++) {
      
      for(int j = 0; j < 8; j++) {
        char *byte_ptr = (char*)d_bitmap_ptr + i;
        if((*byte_ptr & (1 << j)) == 0) {
          index = i * 8 + j;
          // set bit map
          *byte_ptr |= 1 << j;
          break;
        }
      }
      if(index != -1)
        break;
    }    
  }

  // didn't find an available block
  if(index == -1)
    return -ENOSPC;
  
  // put block index into inode array
  int found = 0;
  off_t* d_block_base = (off_t*)((char*)inode + 56);
  off_t* d_block_ptr;

  for(int i = 0; i < N_BLOCKS; i++) {
    d_block_ptr = (off_t*)((char*) d_block_base + 8 * i);

    if(i <= D_BLOCK) {
      if(*d_block_ptr == -1) {
        if(raid == 0) {
          // update all inodes on all disks
          int inode_num = *(int*)inode;
          off_t i_blocks_off = *(off_t*)((char*)disks[disk] + 32);
          off_t inode_off = i_blocks_off + inode_num * BLOCK_SIZE;

          for(int d = 0; d < disk_index; d++) {
            off_t* block_array_ptr = (off_t*)((char*)disks[d] + inode_off + 56);
            *(off_t*)((char*) block_array_ptr + 8 * i) = index;
          }
        }
        else {
          *d_block_ptr = index;
        }

        found = 1;
        break;
      }
    }
    // indirect blocks
    else {
      if(raid == 0) {
        int inode_num = *(int*)inode;
        off_t i_blocks_off = *(off_t*)((char*)disks[disk] + 32);
        off_t inode_off = i_blocks_off + inode_num * BLOCK_SIZE;

        if(*d_block_ptr != -1) {
          off_t *ind_block_ptr = malloc(sizeof(off_t));
          // find ptr to block of ptrs
          find_dblock(disk, *d_block_ptr, &ind_block_ptr);
          //int done = 0;
          for(int m = 0; m < 64; m++) {
            off_t *curr_block = (off_t*)((char*)ind_block_ptr + m * 8);
            if(*curr_block == -1) {
              *curr_block = index;
              found = 1;
              break;
            }
          }
        }
        else {
          // first give found index to indirect block
          for(int d = 0; d < disk_index; d++) {
            off_t* block_array_ptr = (off_t*)((char*)disks[d] + inode_off + 56 + 8 * i);
            *(off_t*)((char*) block_array_ptr) = index;
          }
          off_t *new_block_loc = malloc(sizeof(off_t));
          find_dblock(0, index, &new_block_loc);
          indblock_init(new_block_loc);

          // then find another one for first indirect block
          index = -1;
          for(int i = 0; i < raid0_bnum; i++) {
            if (index != -1)
              break;
            if(raid0_dbit[i] == 0) {
              
              int disk_no = i % disk_index;
              int disk_off = i / disk_index;

              d_bitmap_ptr_off = *(off_t*)((char*)disks[disk_no] + 24);
              d_bitmap_ptr = (off_t*)((char*)disks[disk_no] + d_bitmap_ptr_off);
              dblock_num = *(size_t*)((char*)disks[disk_no] + 8);
              
              index = i;
              byte = disk_off / 8;
              int bit = disk_off % 8;

              raid0_dbit[i] = 1;

              char *byte_ptr = (char*)d_bitmap_ptr + byte;
              *byte_ptr |= 1 << bit;
              *new_block_loc = index;
              found = 1;
              break;              

            }
          }

        }
      }
      // raid 1
      else {
        if(*d_block_ptr != -1) {
          off_t *ind_block_ptr = malloc(sizeof(off_t));
          // find ptr to block of ptrs
          find_dblock(disk, *d_block_ptr, &ind_block_ptr);
          for(int j = 0; j < 64; j ++) {
            off_t *ind_block_loc = (off_t*)((char*)ind_block_ptr + 8 * j);
            if(*ind_block_loc == -1) {
              *ind_block_loc = index;
              found = 1;
              break;
            }
          }
        }
        else {
          // first allocate a block for indirect block
          int ind_index = -1;
          for(int i = 0; i < byte; i++) {
            for(int j = 0; j < 8; j++) {
              char *byte_ptr = (char*)d_bitmap_ptr + i;
              if((*byte_ptr & (1 << j)) == 0) {
                ind_index = i * 8 + j;
                // set bit map
                *byte_ptr |= 1 << j;
                break;
              }
            }
            if(ind_index != -1)
              break;
          }
          if(ind_index == -1)
            return -ENOSPC;
          *d_block_ptr = ind_index;
          // ptr to the block of pointers
          off_t *ind_block_ptr = malloc(sizeof(off_t));
          find_dblock(disk, ind_index, &ind_block_ptr);
          indblock_init(ind_block_ptr);

          *ind_block_ptr = index;
          found = 1;
          break;
        }
      }
    }
  }

  if(found == 1) {
    find_dblock(disk, index, dblock_ptr);
    return 0;
  }
  else
    return -ENOSPC;

}

void indblock_init(off_t *block_loc) {

  for(int i = 0; i < 64; i++) {
    off_t* index_loc = (off_t*)((char*)block_loc + i * sizeof(off_t));
    *index_loc = -1;
  }

} 

void raid0_cleardbit(off_t index) {
  int disk = index % disk_index;
  off_t offset = index / disk_index;
  off_t d_bitmap_ptr_off = *(off_t*)((char*)disks[disk] + 24);
  off_t *d_bitmap_ptr = (off_t*)((char*)disks[disk] + d_bitmap_ptr_off);

  int byte = offset / 8;
  int bit = offset % 8;

  *((char*)d_bitmap_ptr + byte) &= ~(1 << bit);

  raid0_dbit[index] = 0;
}