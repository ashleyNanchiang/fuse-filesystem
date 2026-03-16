#include "bfs.h"

/*
  Return file attributes. The "stat" structure is described in detail in the stat(2) manual page. 
  For the given pathname, this should fill in the elements of the "stat" structure. 
  If a field is meaningless or semi-meaningless (e.g., st_ino) then it should be set to 0 or given a "reasonable" value. 
  This call is pretty much required for a usable filesystem. 

  Fill the following fields of struct stat

    st_uid
    st_gid
    st_atime
    st_mtime
    st_mode
    st_size
*/
int bfs_getattr(const char *path, struct stat *stbuf) {

  off_t inode_off = find_inode(path);

  if(inode_off < 0)
    return -ENOENT;

  off_t *inode_loc = (off_t*)((char*)disks[0] + inode_off);

  stbuf->st_uid = *(uid_t*)((char*)inode_loc + 8); // sizeof(int) + sizeof(mode_t)
  stbuf->st_gid = *(gid_t*)((char*)inode_loc + 12); // sizeof(int) + sizeof(mode_t) + sizeof(uid_t)
  stbuf->st_atime = *(time_t*)((char*)inode_loc + 32);
  stbuf->st_mtime = *(time_t*)((char*)inode_loc + 40);
  stbuf->st_ctime = *(time_t*)((char*)inode_loc + 48);
  stbuf->st_mode = *(mode_t*)((char*)inode_loc + 4);
  stbuf->st_size = *(off_t*)((char*)inode_loc + 16);

  return 0; // Return 0 on success
}

/*
  Make a special (device) file, FIFO, or socket. See mknod(2) for details. 
  This function is rarely needed, since it's uncommon to make these objects inside special-purpose filesystems. 
*/
int bfs_mknod(const char* path, mode_t mode, dev_t rdev) {

  mode |= S_IFREG;

  // extract new directory name from path
  char new_dir[MAX_NAME];

  int length = strlen(path);
  int current_letter = length - 1;

  // find the new directory name from path
  while(current_letter >= 0) {
    if(path[current_letter] == '/' || current_letter == 0)
      break;
    else  
      current_letter--;
  }
  
  int index = 0;
  for(int i = current_letter; i < length; i++) {
    if(path[i] == '/')
      continue;
    new_dir[index] = path[i];
    index ++;
  }
  new_dir[index] = '\0';

  // construct a new string for the directry to put the new directory in
  off_t curr_inode_off;

  if(current_letter != 0) {
    char loc_for_dir[current_letter];
    for(int i = 0; i < current_letter; i++) {
      loc_for_dir[i] = path[i];
    }
    loc_for_dir[current_letter] = '\0';
    curr_inode_off = find_inode(loc_for_dir);
    if(curr_inode_off == -ENOENT)
      return -ENOENT;       
  }
  else {
    curr_inode_off = find_inode(NULL);
    if(curr_inode_off == -ENOENT)
      return -ENOENT;       
  }
  // printf("curr_inode_off: %ld\n", curr_inode_off);
  // allocate a new inode for the new directory
  off_t new_inode_loc_off = allocate_inode(mode);

  if(new_inode_loc_off < 0)
    return new_inode_loc_off;

  int inode_num = *(int*)((char*)disks[0] + new_inode_loc_off);

  // for raid 0
  int set = 0;

  for(int i = 0; i < disk_index; i++) {

    off_t *curr_inode_loc = (off_t*) ((char*)disks[i] + curr_inode_off);
    off_t *curr_inode_dblock = (off_t*) ((char*)curr_inode_loc + 56);

    // first scan through parent data blocks to find space, look for blocks that exists
    int found = 0;
    // for raid 0
    int check_raid = (raid == 0 && set == 0);
    for(int j = 0; j < N_BLOCKS; j++) {
      if( check_raid == 1 || raid == 1 || raid == 2 ) {

        if(found == 1)
          continue;
        off_t index = *(off_t*)((char*)curr_inode_dblock + 8 * j);

        if(index < 0)
          continue;
        else {
          off_t *block_ptr = malloc(sizeof(off_t));
          find_dblock(i, index, &block_ptr);

          for(int k = 0; k < 16; k++) {

            if(found == 1)
              continue;

            off_t *curr_block_ptr = (off_t*)((char*)block_ptr + 32 * k);

            if(*((char*)curr_block_ptr + MAX_NAME) == 0) {

              strcpy((char*)curr_block_ptr, new_dir);
              int *inode_num_ptr = (int*)((char*)curr_block_ptr + MAX_NAME);
              *inode_num_ptr = inode_num;
              found = 1;
              set = 1;
            }
          }
        }        
      }

    }
    // then if no space, allocate a new block
    if(found == 0) {
      int check_raid = (raid == 0 && set == 0);

      if( check_raid == 1 || raid == 1 || raid == 2) {
        off_t *new_block_ptr = malloc(sizeof(off_t));
        int result = allocate_dblock(i, curr_inode_loc, &new_block_ptr);
        if(result < 0)
          return result;

        strcpy((char*)new_block_ptr, new_dir);

        int *inode_num_ptr = (int*)((char*)new_block_ptr + MAX_NAME);
        *inode_num_ptr = inode_num;
        found = 1;
        set = 1;
      }

    }

    // update all fields: size, nlink, atim, mtim, ctim
    int links = (int)*((char*)curr_inode_loc + 24);
    links++;
    *(int*)((char*)curr_inode_loc + 24) = links;

    time_t current_time;
    time(&current_time);
    *(off_t*)((char*)curr_inode_loc + 32) = current_time;
    *(off_t*)((char*)curr_inode_loc + 40) = current_time;
    *(off_t*)((char*)curr_inode_loc + 48) = current_time;
  }
  return 0;
}

/*
  Create a directory with the given name. The directory permissions are encoded in mode. 
  See mkdir(2) for details. This function is needed for any reasonable read/write filesystem. 
*/
int bfs_mkdir(const char* path, mode_t mode) {

  mode |= S_IFDIR;

  // extract new directory name from path
  char new_dir[MAX_NAME];

  int length = strlen(path);
  int current_letter = length - 1;

  // find the new directory name from path
  while(current_letter >= 0) {
    if(path[current_letter] == '/' || current_letter == 0)
      break;
    else  
      current_letter--;
  }
  
  int index = 0;
  for(int i = current_letter; i < length; i++) {
    if(path[i] == '/')
      continue;
    new_dir[index] = path[i];
    index ++;
  }
  new_dir[index] = '\0';

  // construct a new string for the directry to put the new directory in
  off_t curr_inode_off;

  if(current_letter != 0) {
    char loc_for_dir[current_letter];
    for(int i = 0; i < current_letter; i++) {
      loc_for_dir[i] = path[i];
    }
    loc_for_dir[current_letter] = '\0';
    curr_inode_off = find_inode(loc_for_dir);

    if(curr_inode_off == -ENOENT)
      return -ENOENT;       
  }
  else {
    curr_inode_off = find_inode(NULL);
    if(curr_inode_off == -ENOENT)
      return -ENOENT;       
  }

  // allocate a new inode for the new directory
  off_t new_inode_loc_off = allocate_inode(mode);

  if(new_inode_loc_off < 0)
    return new_inode_loc_off;

  int inode_num = *(int*)((char*)disks[0] + new_inode_loc_off);

  // for raid 0
  int set = 0;

  for(int i = 0; i < disk_index; i++) {

    off_t *curr_inode_loc = (off_t*) ((char*)disks[i] + curr_inode_off);
    off_t *curr_inode_dblock = (off_t*) ((char*)curr_inode_loc + 56);

    // first scan through parent data blocks to find space, look for blocks that exists
    int found = 0;
    // for raid 0
    int check_raid = (raid == 0 && set == 0);
    for(int j = 0; j < N_BLOCKS; j++) {
      if( check_raid == 1 || raid == 1 || raid == 2 ) {

        if(found == 1)
          continue;
        off_t index = *(off_t*)((char*)curr_inode_dblock + 8 * j);

        if(index < 0)
          continue;
        else {
          off_t *block_ptr = malloc(sizeof(off_t));
          find_dblock(i, index, &block_ptr);

          for(int k = 0; k < 16; k++) {

            if(found == 1)
              continue;
            off_t *curr_block_ptr = (off_t*)((char*)block_ptr + 32 * k);

            if(*((char*)curr_block_ptr + MAX_NAME) == 0) {
              strcpy((char*)curr_block_ptr, new_dir);

              int *inode_num_ptr = (int*)((char*)curr_block_ptr + MAX_NAME);
              *inode_num_ptr = inode_num;
              found = 1;
              set = 1;
            }
          }
        }        
      }

    }
    // then if no space, allocate a new block
    if(found == 0) {
      int check_raid = (raid == 0 && set == 0);

      if( check_raid == 1 || raid == 1 || raid == 2) {
        off_t *new_block_ptr = malloc(sizeof(off_t));
        int result = allocate_dblock(i, curr_inode_loc, &new_block_ptr);
        if(result < 0)
          return result;

        strcpy((char*)new_block_ptr, new_dir);

        int *inode_num_ptr = (int*)((char*)new_block_ptr + MAX_NAME);

        *inode_num_ptr = inode_num;

        found = 1;
        set = 1;
      }

    }

    // update all fields: size, nlink, atim, mtim, ctim
    off_t size = *(off_t*)((char*)curr_inode_loc + 16);
    size += 32;
    *(off_t*)((char*)curr_inode_loc + 16) = size;

    int links = (int)*((char*)curr_inode_loc + 24);
    links++;
    *(int*)((char*)curr_inode_loc + 24) = links;

    time_t current_time;
    time(&current_time);
    *(off_t*)((char*)curr_inode_loc + 32) = current_time;
    *(off_t*)((char*)curr_inode_loc + 40) = current_time;
    *(off_t*)((char*)curr_inode_loc + 48) = current_time;
  }

  return 0;

}

/*
  Remove (delete) the given file, symbolic link, hard link, or special node. 
  Note that if you support hard links, unlink only deletes the data when the last hard link is removed. 
  See unlink(2) for details. 
*/
int bfs_unlink(const char* path) {

  // find the address to the inode of the thing to be deleted
  // if links == 1, can delete
  // update ibitmap

  //parse path for parent inode
  char *path_cpy = malloc(sizeof(char) * MAX_NAME);

  strcpy(path_cpy, path);
  
  int length = strlen(path);
  int current_letter = length - 1;

  // get to the first '/' from the end
  //       |
  // mnt/d1/d2
  while(path_cpy[current_letter] != '/') {
    if(path_cpy[current_letter] == '/') {
      path_cpy[current_letter] = '\0';
      break;
    }
    path_cpy[current_letter] = '\0';
    current_letter --;
  }

  off_t curr_inode_off = find_inode(path);
  off_t parent_inode_off = find_inode(path_cpy);

  // find the address to the inode of the thing to be deleted
  // if links == 1, can delete
  // update ibitmap
  // memset inode to 0

  // find the parent inode
  // delete entry
  // if already deleted (raid 0) then skip
  // decrease link and size

  // make sure parent's size don't decrement more times than need to in raid 0
  // off_t *parent_inode = (off_t*) ((char*) disks[0] + parent_inode_off);
  // off_t parent_size = *(off_t*)((char*)parent_inode + 16);
  // parent_size = (char) parent_size - 32;
  
  for(int i = 0; i < disk_index; i++) {

    // work on child node
    off_t *curr_inode = (off_t*) ((char*) disks[i] + curr_inode_off);

    // check links first
    int links = *(int*)((char*)curr_inode + 24);

    if(links > 1)
      return -1;

    // clear out bit on i bit map
    int inode_num = *(int*)curr_inode;
    int byte = inode_num / 8;
    int bit = inode_num % 8;

    off_t i_bitmap_ptr_off = *(off_t*)((char*)disks[i] + 16); // sizeof(size_t) * 2
    off_t *i_bitmap_ptr = (off_t*) ((char*)disks[i] + i_bitmap_ptr_off);

    *((char*)i_bitmap_ptr + byte) &= ~(1 << bit);

    off_t *block_array = (off_t*) ((char*)curr_inode + 56);
    for(int j = 0; j < N_BLOCKS; j++) {
      if(j <= D_BLOCK) {
        off_t index = *(off_t*)((char*)block_array + j * 8);
        if(index == -1)
          continue;
        off_t *block_ptr = malloc(sizeof(off_t));
        find_dblock(i, index, &block_ptr);
        memset(block_ptr, 0, BLOCK_SIZE);
        if(raid == 0)
          raid0_cleardbit(index);
        else {
          off_t d_bitmap_ptr_off = *(off_t*)((char*)disks[i] + 24);
          off_t *d_bitmap_ptr = (off_t*)((char*)disks[i] + d_bitmap_ptr_off);

          int byte = index / 8;
          int bit = index % 8;

          *((char*)d_bitmap_ptr + byte) &= ~(1 << bit);
        }
      }
      else {
        off_t ind_index = *(off_t*)((char*)block_array + j * 8);
        off_t d_bitmap_ptr_off = *(off_t*)((char*)disks[i] + 24);
        off_t *d_bitmap_ptr = (off_t*)((char*)disks[i] + d_bitmap_ptr_off);
        if(ind_index == -1)
          continue;
        off_t *ind_block_ptr = malloc(sizeof(off_t));
        find_dblock(i, ind_index, &ind_block_ptr);        
        for(int m = 0; m < 64; m++) {
          off_t blk_index = *(off_t*)((char*)ind_block_ptr + m * 8);
          if(blk_index == -1)
            continue;
          off_t *blk_ptr = malloc(sizeof(off_t));
          find_dblock(i, blk_index, &blk_ptr);
          memset(blk_ptr, 0, BLOCK_SIZE);
          if(raid == 0)
            raid0_cleardbit(blk_index);
          else {

            int byte = blk_index / 8;
            int bit = blk_index % 8;
            *((char*)d_bitmap_ptr + byte) &= ~(1 << bit);

          }          
        }
        int byte = ind_index / 8;
        int bit = ind_index % 8;
        *((char*)d_bitmap_ptr + byte) &= ~(1 << bit);

        memset(ind_block_ptr, 0, BLOCK_SIZE);
      }
    }

    // empty out the inode, set to 0
    memset(curr_inode, 0, 120);

    // work on parent node
    int found = 0;
    off_t *parent_blocks = (off_t*) ((char*) disks[0] + parent_inode_off + 56);
    for(int a = 0; a < N_BLOCKS; a++) { 
      off_t block_index = *(off_t*)((char*)parent_blocks + a * 8);
      if(block_index < 0)
        continue;
      if(found == 1)
        continue;
      if(a <= D_BLOCK) {
        off_t *block_ptr = malloc(sizeof(off_t*));

        find_dblock(i, block_index, &block_ptr);

        for(int b = 0; b < 16; b++) {

          if(found == 1)
            continue;
          off_t *ent_ptr = (off_t*)((char*)block_ptr + 32 * b);
          int ent_inode_num = *(int*)((char*)ent_ptr + MAX_NAME);
          if(inode_num == ent_inode_num) {
            memset(ent_ptr, 0, 32);
            found = 1;
          }
        }
      }
      else {
        // haven't implement indirect blocks
        if(block_index < 0)
          continue;
        off_t *ind_block = malloc(sizeof(off_t));
        find_dblock(i, block_index, &ind_block);
        for(int m = 0; m < 64; m++) {
          if(found == 1)
              continue;
          off_t *curr_block_ind = (off_t*)((char*)ind_block + 8 * m);
          if(*curr_block_ind == -1)
            continue;
          off_t *curr_block_loc = malloc(sizeof(off_t));
          find_dblock(i, *curr_block_ind, &curr_block_loc);
          for(int n = 0; n < 16; n++) {
            if(found == 1)
              continue;
            off_t *ent_ptr = (off_t*)((char*)curr_block_loc + 32 * n);
            int ent_inode_num = *(int*)((char*)ent_ptr + MAX_NAME);
            if(inode_num == ent_inode_num) {
              memset(ent_ptr, 0, 32);
              found = 1;
            }
          }
        }
        
      }
    }
    // update size
    off_t *parent_inode_disk = (off_t*) ((char*) disks[i] + parent_inode_off);
    // update nlinks
    *(off_t*)((char*)parent_inode_disk + 24) -= 1;
    // update times
    time_t current_time;
    time(&current_time);
    *(off_t*)((char*)parent_inode_disk + 32) = current_time;
    *(off_t*)((char*)parent_inode_disk + 40) = current_time;
    *(off_t*)((char*)parent_inode_disk + 48) = current_time;
  }

  return 0;
}

/*
  Remove the given directory. This should succeed only if the directory is empty (except for "." and ".."). See rmdir(2) for details. 
*/
int bfs_rmdir(const char* path) {

  printf("///// rmdir /////\n");

  //parse path for parent inode
  char *path_cpy = malloc(sizeof(char) * MAX_NAME);

  strcpy(path_cpy, path);
  
  int length = strlen(path);
  int current_letter = length - 1;

  // get to the first '/' from the end
  //       |
  // mnt/d1/d2
  while(path_cpy[current_letter] != '/') {
    if(path_cpy[current_letter] == '/') {
      path_cpy[current_letter] = '\0';
      break;
    }
    path_cpy[current_letter] = '\0';
    current_letter --;
  }

  off_t curr_inode_off = find_inode(path);
  off_t parent_inode_off = find_inode(path_cpy);

  //  find the address to the inode of the thing to be deleted
  // if links == 1, can delete
  // update ibitmap
  // memset inode to 0

  //  find the parent inode
  // delete entry
  // if already deleted (raid 0) then skip
  // decrease link and size

  // make sure parent's size don't decrement more times than need to in raid 0
  // off_t *parent_inode = (off_t*) ((char*) disks[0] + parent_inode_off);
  // off_t parent_size = *(off_t*)((char*)parent_inode + 16);
  // parent_size = (char) parent_size - 32;
  
  for(int i = 0; i < disk_index; i++) {
    
    // work on child node
    off_t *curr_inode = (off_t*) ((char*) disks[i] + curr_inode_off); 

    // check links first
    int links = *(int*)((char*)curr_inode + 24);

    if(links > 1)
      return -1;

    // clear out bit on i bit map
    int inode_num = *(int*)curr_inode;
    int byte = inode_num / 8;
    int bit = inode_num % 8;

    off_t i_bitmap_ptr_off = *(off_t*)((char*)disks[i] + 16); // sizeof(size_t) * 2
    off_t *i_bitmap_ptr = (off_t*) ((char*)disks[i] + i_bitmap_ptr_off);

    *((char*)i_bitmap_ptr + byte) &= ~(1 << bit);

    off_t *block_array = (off_t*) ((char*)curr_inode + 56);
    for(int j = 0; j < N_BLOCKS; j++) {
      if(j <= D_BLOCK) {
        off_t index = *(off_t*)((char*)block_array + j * 8);
        if(index == -1)
          continue;
        off_t *block_ptr = malloc(sizeof(off_t));
        find_dblock(i, index, &block_ptr);
        memset(block_ptr, 0, BLOCK_SIZE);
        if(raid == 0)
          raid0_cleardbit(index);
        else {
          off_t d_bitmap_ptr_off = *(off_t*)((char*)disks[i] + 24);
          off_t *d_bitmap_ptr = (off_t*)((char*)disks[i] + d_bitmap_ptr_off);

          int byte = index / 8;
          int bit = index % 8;

          *((char*)d_bitmap_ptr + byte) &= ~(1 << bit);
        }
      }
      else {
        off_t ind_index = *(off_t*)((char*)block_array + j * 8);
        off_t d_bitmap_ptr_off = *(off_t*)((char*)disks[i] + 24);
        off_t *d_bitmap_ptr = (off_t*)((char*)disks[i] + d_bitmap_ptr_off);
        if(ind_index == -1)
          continue;
        off_t *ind_block_ptr = malloc(sizeof(off_t));
        find_dblock(i, ind_index, &ind_block_ptr);        
        for(int m = 0; m < 64; m++) {
          off_t blk_index = *(off_t*)((char*)ind_block_ptr + m * 8);
          if(blk_index == -1)
            continue;
          off_t *blk_ptr = malloc(sizeof(off_t));
          find_dblock(i, blk_index, &blk_ptr);
          memset(blk_ptr, 0, BLOCK_SIZE);
          if(raid == 0)
            raid0_cleardbit(blk_index);
          else {
            int byte = blk_index / 8;
            int bit = blk_index % 8;
            *((char*)d_bitmap_ptr + byte) &= ~(1 << bit);
          }          
        }
        int byte = ind_index / 8;
        int bit = ind_index % 8;
        *((char*)d_bitmap_ptr + byte) &= ~(1 << bit);

        memset(ind_block_ptr, 0, BLOCK_SIZE);
      }
    }

    // empty out the inode, set to 0
    memset(curr_inode, 0, 120);

    // work on parent node
    int found = 0;
    off_t *parent_blocks = (off_t*) ((char*) disks[0] + parent_inode_off + 56);
    for(int a = 0; a < N_BLOCKS; a++) { 
      off_t block_index = *(off_t*)((char*)parent_blocks + a * 8);
      if(block_index < 0)
        continue;
      if(found == 1)
        continue;
      if(a <= D_BLOCK) {
        off_t *block_ptr = malloc(sizeof(off_t*));
        
        find_dblock(i, block_index, &block_ptr);

        for(int b = 0; b < 16; b++) {
          if(found == 1)
            continue;

          off_t *ent_ptr = (off_t*)((char*)block_ptr + 32 * b);
          int ent_inode_num = *(int*)((char*)ent_ptr + MAX_NAME);
          if(inode_num == ent_inode_num) {
            memset(ent_ptr, 0, 32);
            found = 1;
          }
        }
      }
      else {
        if(block_index < 0)
          continue;
        off_t *ind_block = malloc(sizeof(off_t));
        find_dblock(i, block_index, &ind_block);
        for(int m = 0; m < 64; m++) {
          if(found == 1)
              continue;
          off_t *curr_block_ind = (off_t*)((char*)ind_block + 8 * m);
          if(*curr_block_ind == -1)
            continue;
          off_t *curr_block_loc = malloc(sizeof(off_t));
          find_dblock(i, *curr_block_ind, &curr_block_loc);
          for(int n = 0; n < 16; n++) {
            if(found == 1)
              continue;
            off_t *ent_ptr = (off_t*)((char*)curr_block_loc + 32 * n);
            int ent_inode_num = *(int*)((char*)ent_ptr + MAX_NAME);
            if(inode_num == ent_inode_num) {
              memset(ent_ptr, 0, 32);
              found = 1;
            }
          }
        }
        
      }
    }
    // update size
    off_t *parent_inode_disk = (off_t*) ((char*) disks[i] + parent_inode_off);

    // update nlinks
    *(off_t*)((char*)parent_inode_disk + 24) -= 1;
    // update times
    time_t current_time;
    time(&current_time);
    *(off_t*)((char*)parent_inode_disk + 32) = current_time;
    *(off_t*)((char*)parent_inode_disk + 40) = current_time;
    *(off_t*)((char*)parent_inode_disk + 48) = current_time;
  }

  return 0;
}

/*
  Read sizebytes from the given file into the buffer buf, beginning offset bytes into the file. 
  See read(2) for full details. Returns the number of bytes transferred, or 0 if offset was at or beyond the end of the file. 
  Required for any sensible filesystem. 
*/
int bfs_read(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi) {

  // raid 0
  // just look at disk 0 inode and go through its data block array
  // if the block at offset is -1  then return an error
  // and read the contents into the buffer

  // raid 1v
  // look at disk 0 inode, make an compare count array with the number of disks
  // compare each block and then write the best one to buffer
  // repeat until size is met

  off_t curr_inode_off = find_inode(path);
  off_t *curr_inode_ptr = (off_t*)((char*)disks[0] + curr_inode_off);
  off_t *inode_blocks_arr = (off_t*)((char*)curr_inode_ptr + 56);

  size_t file_size = *(size_t*)((char*)curr_inode_ptr + 16);

  if (offset >= file_size)
    return 0;

  off_t b_index = offset / BLOCK_SIZE;
  off_t b_off = offset % BLOCK_SIZE;

  if(raid == 0) {

    size_t size_check = size;
    if(size > file_size)
      size_check = file_size;

    if(b_index <= D_BLOCK) {

      int loop = 0;
      for(int i = b_index; i < N_BLOCKS; i++) {
        // printf("r: i: %d\n", i);
        // printf("size_check: %ld\n", size_check);
        off_t block_index = *(off_t*)((char*)inode_blocks_arr + i * 8);
        if(i <= D_BLOCK) {

          if(size_check == 0)
            continue;

          if(block_index == -1)
            return -ENOENT;

          off_t *start_ptr = malloc(sizeof(off_t));
          find_dblock(0, block_index, &start_ptr);

          size_t size_read;

          if(loop == 0) {
            start_ptr = (off_t*)((char*)start_ptr + b_off);
            size_read = BLOCK_SIZE - b_off;
            loop++;
          }
          else {
            size_read = BLOCK_SIZE;
          }

          if(size_read > size_check)
            size_read = size_check;

          memcpy(buf, start_ptr, size_read);

          printf("buf before: %p\n", (void*)buf);
          buf += size_read;
          size_check -= size_read;
        
        }
        else {
          if(size_check == 0)
            continue;
          off_t *ind_block_ptr_to_blocks = malloc(sizeof(off_t));
          find_dblock(0, block_index, &ind_block_ptr_to_blocks);
          for(int k = 0; k < 64; k ++) {
            if(size_check == 0)
              continue;
            off_t *curr_block_index_ptr = (off_t*)((char*)ind_block_ptr_to_blocks + k * 8);
            off_t *curr_block_loc = malloc(sizeof(off_t));
            find_dblock(0, *curr_block_index_ptr, &curr_block_loc);

            size_t size_read = BLOCK_SIZE;
            if(size_read > size_check)
              size_read = size_check;
            memcpy(buf, curr_block_loc, size_read);
            buf += size_read;
            size_check -= size_read;           
          }
        }
      }
    }
    else {
      // indirect block as offset
    }

  }
  // raid 1
  else {
    int compare[disk_index];

    curr_inode_ptr = (off_t*)((char*)disks[0] + curr_inode_off);
    inode_blocks_arr = (off_t*)((char*)curr_inode_ptr + 56);

    size_t size_check = size;
    if(size > file_size)
      size_check = file_size; 

    if(b_index <= D_BLOCK) {
      int loop = 0;
      for(int i = 0; i < N_BLOCKS; i++) {

        if(size_check == 0)
          continue;
        off_t block_index = *(off_t*)((char*)inode_blocks_arr + i * 8);

        if(i <= D_BLOCK) {

          if(size_check == 0)
            continue;

          if(block_index == -1)
            return -ENOENT;

          for(int j = 0; j < disk_index; j++) {
            compare[j] = 0;
          }

          for(int d = 0; d < disk_index; d++) {
            off_t* curr_block_ptr = malloc(sizeof(off_t));
            find_dblock(d, block_index, &curr_block_ptr);
            for(int e = d; e < disk_index; e++) {
              off_t* comp_block_ptr = malloc(sizeof(off_t));
              find_dblock(e, block_index, &comp_block_ptr);
              if(memcmp(curr_block_ptr, comp_block_ptr, BLOCK_SIZE) == 0) {
                compare[d] ++;
              }
            }
          }
          int most = compare[0];
		      int most_index = 0;
		      for(int k = 0; k < disk_index; k++) {
			      if(compare[k] > most) {
				      most = compare[k];
				      most_index = k;
			      }
		      }

          off_t *start_ptr = malloc(sizeof(off_t));
          find_dblock(most_index, block_index, &start_ptr);
          
          size_t size_read = BLOCK_SIZE;

          if(loop == 0) {
            start_ptr = (off_t*)((char*)start_ptr + b_off);
            size_read = BLOCK_SIZE - b_off;
            loop++;
          }
          else {
            size_read = BLOCK_SIZE;
          }

          if(size_read > size_check)
            size_read = size_check;
          
          memcpy(buf, start_ptr, size_read);

          buf += size_read;
          size_check -= size_read;

        }
        else {
          // indirect blocks
          off_t *ptr_to_blks = malloc(sizeof(off_t));
          find_dblock(0, block_index, &ptr_to_blks);

          for(int h = 0; h < 64; h++) {

            if(size_check == 0)
              continue;
            
            off_t curr_block_index = *(off_t*)((char*)ptr_to_blks + h * 8);

            for(int j = 0; j < disk_index; j++) {
              compare[j] = 0;
            }

            for(int d = 0; d < disk_index; d++) {
              off_t* curr_block_ptr = malloc(sizeof(off_t));
              find_dblock(d, curr_block_index, &curr_block_ptr);
              for(int e = d; e < disk_index; e++) {
                off_t* comp_block_ptr = malloc(sizeof(off_t));
                find_dblock(e, curr_block_index, &comp_block_ptr);
                if(memcmp(curr_block_ptr, comp_block_ptr, BLOCK_SIZE) == 0) {
                  compare[d] ++;
                }
              }
            }

            int most = compare[0];
            int most_index = 0;
            for(int k = 0; k < disk_index; k++) {
              if(compare[k] > most) {
                most = compare[k];
                most_index = k;
              }
            }

            size_t size_read = BLOCK_SIZE;
            if(size_read > size_check)
              size_read = size_check;

            off_t *start_ptr = malloc(sizeof(off_t));
            find_dblock(most_index, curr_block_index, &start_ptr);

            if(size_read > size_check)
              size_read = size_check;

            memcpy(buf, start_ptr, size_read);

            buf += size_read;
            size_check -= size_read;
          }  
        }
      }
    }
    else {
      // indirect block as offset
    }    

  }

  if(size > file_size)
    return file_size;
  else
    return size;
}

/*
  As for read above, except that it can't return 0. 
*/
int bfs_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi) {

  // write size of buf to offset of path
  // first check if size is big enough for the path starting at offset
  // if not just return an error
  // get path inode
  // get dblocks
  // get the exact ptr to offset
  // write one block at a time
  // go back to find the next block
  // if -1, allocate

  // raid 1: for loop for all disks
  // raid 0: only allocate once

  off_t curr_inode_off = find_inode(path);

  size_t total_size = 7 * 512 + 64 * 512;

  if(size > (total_size - offset))
    return -ENOSPC;

  off_t b_index = offset / BLOCK_SIZE;
  off_t b_off = offset % BLOCK_SIZE;

  const char *buf_begin = buf;

  if(raid == 0) {
    // update inode for all disks
    // inodes should be the same on all disks
    // d bit map won't be the same because dblocks aren't
    // ===> allocate_inode fixed to accomdate all of the above

    // loop through block array of inode, if found, write
    // if not, allocate_inode, no matter the disk, just use disk 0

    // char *buf_cpy = malloc(sizeof(char) * strlen(buf));
    // strcpy(buf_cpy, buf);

    size_t size_check = size;
    // printf("initial size: %ld\n", size);
    off_t *curr_inode_ptr = (off_t*)((char*)disks[0] + curr_inode_off);

    if(b_index <= D_BLOCK) {
      // loop is so the first iteration will always execute a certain block of code
      int loop = 0;
      for(int i = b_index; i < N_BLOCKS; i++) {

        if(size_check == 0)
            continue;

        off_t *start_block_ptr = malloc(sizeof(off_t));
        off_t *block_loc_index = (off_t*)((char*)curr_inode_ptr + 56 + i * sizeof(off_t));

        if(i <= D_BLOCK) {

          if(*block_loc_index == -1) {

            int result = allocate_dblock(0, curr_inode_ptr, &start_block_ptr);

            if(result != 0)
              return result;
          }
          else {
            find_dblock(0, *block_loc_index, &start_block_ptr);
          }
            
          size_t size_write;

          if(loop == 0) {
            start_block_ptr = (off_t*)((char*)start_block_ptr + b_off);
            size_write = BLOCK_SIZE - b_off;
            loop++;
          }
          else {
            size_write = BLOCK_SIZE;
          }

          if(size_write > size_check)
            size_write = size_check;

          memcpy(start_block_ptr, buf, size_write);

          buf = buf + size_write;
          size_check -= size_write;

        }
        else {
          // indirect blocks
          if(*block_loc_index == -1) {
            off_t *block_ptr = malloc(sizeof(off_t));
            int result = allocate_dblock(0, curr_inode_ptr, &block_ptr);

            if(result != 0)
              return result;
          }
          find_dblock(0, *block_loc_index, &start_block_ptr);

          for(int k = 0; k < 64; k ++) {

            if(size_check == 0)
              continue;
            off_t *curr_block_ptr = (off_t*)((char*)start_block_ptr + k * 8);
            off_t *inblock_loc = malloc(sizeof(off_t));

            if(*curr_block_ptr == -1) {
              int result = allocate_dblock(0, curr_inode_ptr, &inblock_loc);
              if(result != 0)
                return result;
            }
            else {
              find_dblock(0, *curr_block_ptr, &inblock_loc);
            }

            size_t size_write = BLOCK_SIZE;

            if(size_write > size_check)
              size_write = size_check;

            memcpy(inblock_loc, buf, size_write);
            buf = buf + size_write;
            size_check -= size_write;

          }

        }
      }

    }
    // offset in indirect block
    else {
        
      off_t *ind_block_ptr = (off_t*)((char*)curr_inode_ptr + 56 + 56);
      off_t *inblock_loc = malloc(sizeof(off_t));
      if(*ind_block_ptr == -1) {
        off_t *first_blk = malloc(sizeof(off_t));
        int result = allocate_dblock(0, curr_inode_ptr, &first_blk);
        if(result != 0)
          return result;
      }
      find_dblock(0, *ind_block_ptr, &inblock_loc);

      int ind_index = b_index - IND_BLOCK;

      int loop = 0;
      for(int k = ind_index; k < 64; k++) {
          
        if(size_check == 0)
          continue;
        off_t* inblock_loc_ptr = (off_t*)((char*)inblock_loc + 8 * k);

        off_t *inblock_ptr = malloc(sizeof(off_t));
        if(*inblock_loc_ptr == -1) {
          int result = allocate_dblock(0, curr_inode_ptr, &inblock_ptr);
          if(result != 0)
            return result;
        }
        else {
          find_dblock(0, *inblock_loc_ptr, &inblock_ptr);
        }

        size_t size_write = BLOCK_SIZE;

        if(loop == 0) {
          inblock_ptr = (off_t*)((char*)inblock_ptr + b_off);
          size_write = BLOCK_SIZE - b_off;
          loop ++;
        }
          
        if(size_write > size_check)
          size_write = size_check;

        memcpy(inblock_ptr, buf, size_write);
        buf = buf + size_write;
        size_check -= size_write;
      }
    }

  }
  // raid 1
  else {
    for(int disk_num = 0; disk_num < disk_index; disk_num++) {

      buf = buf_begin;

      size_t size_check = size;

      off_t *curr_inode_ptr = (off_t*)((char*)disks[disk_num] + curr_inode_off);

      if(b_index <= D_BLOCK) {
        
        int loop = 0;
        for(int i = b_index; i < N_BLOCKS; i++) {

          if(size_check == 0)
            continue;

          off_t *start_block_ptr = malloc(sizeof(off_t));
          off_t *block_loc_index = (off_t*)((char*)curr_inode_ptr + 56 + i * sizeof(off_t));

          if(i <= D_BLOCK) {

            if(*block_loc_index == -1) {             
              int result = allocate_dblock(disk_num, curr_inode_ptr, &start_block_ptr);
              if(result != 0)
                return result;
            }
            else {
              find_dblock(disk_num, *block_loc_index, &start_block_ptr);
            }
            
            size_t size_write;

            if(loop == 0) {
              start_block_ptr = (off_t*)((char*)start_block_ptr + b_off);
              size_write = BLOCK_SIZE - b_off;
              loop++;
            }
            else {
              size_write = BLOCK_SIZE;
            }
            
            if(size_write > size_check)
              size_write = size_check;

            memcpy(start_block_ptr, buf, size_write);

            buf = buf + size_write;
            size_check -= size_write;
          }
          // indirect block
          else {
            // start_block_ptr points to block of ptrs
            if(*block_loc_index == -1) {
              off_t *first_blk = malloc(sizeof(off_t));
              int result = allocate_dblock(disk_num, curr_inode_ptr, &first_blk);
              if(result != 0)
                return result;
              
            }
            find_dblock(disk_num, *block_loc_index, &start_block_ptr);

            for(int k = 0; k < 64; k ++) {
              if(size_check == 0)
                continue;
              
              off_t *curr_block_ptr = (off_t*)((char*)start_block_ptr + k * 8);
              off_t *inblock_loc = malloc(sizeof(off_t));
              if(*curr_block_ptr == -1) {
                int result = allocate_dblock(disk_num, curr_inode_ptr, &inblock_loc);
                if(result != 0)
                  return result;
              }
              else {
                find_dblock(disk_num, *curr_block_ptr, &inblock_loc);
              }

              size_t size_write = BLOCK_SIZE;

              if(size_write > size_check)
                size_write = size_check;
              memcpy(inblock_loc, buf, size_write);
              buf = buf + size_write;
              size_check -= size_write;
            }
          }

        }
      }
      // if offset is in an indirect block
      else {        
        off_t *ind_block_ptr = (off_t*)((char*)curr_inode_ptr + 56 + 56);
        off_t *inblock_loc = malloc(sizeof(off_t));

        if(*ind_block_ptr == -1) {
          off_t *first_blk = malloc(sizeof(off_t));
          int result = allocate_dblock(disk_num, curr_inode_ptr, &first_blk);
          if(result != 0)
            return result;
        }
        // inclock_loc is the address to the block of ptrs
        find_dblock(disk_num, *ind_block_ptr, &inblock_loc);

        int ind_index = b_index - IND_BLOCK;
        int loop = 0;
        for(int k = ind_index; k < 64; k++) {
          
          if(size_check == 0)
            continue;
          off_t* inblock_loc_ptr = (off_t*)((char*)inblock_loc + 8 * k);

          off_t *inblock_ptr = malloc(sizeof(off_t));
          if(*inblock_loc_ptr == -1) {
            int result = allocate_dblock(disk_num, curr_inode_ptr, &inblock_ptr);
            if(result != 0)
              return result;
          }
          else {
            find_dblock(disk_num, *inblock_loc_ptr, &inblock_ptr);
          }

          size_t size_write = BLOCK_SIZE;

          if(loop == 0) {
            inblock_ptr = (off_t*)((char*)inblock_ptr + b_off);
            size_write = BLOCK_SIZE - b_off;
            loop ++;
          }
          
          if(size_write > size_check)
            size_write = size_check;

          memcpy(inblock_ptr, buf, size_write);
          buf = buf + size_write;
          size_check -= size_write;
        }
      }
    }
  }
  
  // update size of this file 
  for(int disk_num = 0; disk_num < disk_index; disk_num++) {
    off_t *child_inode = (off_t*)((char*)disks[disk_num] + curr_inode_off);
    *(off_t*)((char*)child_inode + 16) += size;
  }

  return size;
}

/*
  int fuse_fill_dir_t(void *buf, const char *name,
				const struct stat *stbuf, off_t off);
*/
int bfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {

  off_t curr_inode_off = find_inode(path);

  for(int i = 0; i < N_BLOCKS; i++) {
    off_t* curr_inode_dblock_ptr = (off_t*)((char*) disks[0] + curr_inode_off + 56 + i * sizeof(off_t));
    if(i <= D_BLOCK) {
      off_t *block_loc = malloc(sizeof(off_t));
      if(*curr_inode_dblock_ptr != -1) {
        find_dblock(0, *curr_inode_dblock_ptr, &block_loc);
        for(int j = 0; j < 16; j++) {
          off_t *curr_block = (off_t*) ((char*)block_loc + j * 32);
          if(*curr_block != 0) {
            filler(buf, (char*)curr_block, NULL, 0);
          }
        }
      }
    }
    else {
      if(*curr_inode_dblock_ptr != -1) {
        off_t *ind_block_ptr = malloc(sizeof(off_t));
        find_dblock(0, *curr_inode_dblock_ptr, &ind_block_ptr);
        for(int j = 0; j < 64; j++) {
          off_t *curr_ind_block_index = (off_t*)((char*)ind_block_ptr + j * sizeof(off_t));
          off_t *curr_ind_block_ptr= malloc(sizeof(off_t));
          find_dblock(0, *curr_ind_block_index, &curr_ind_block_ptr);
          for(int k = 0; k < 16; k++) {
            off_t *curr_ind_block = (off_t*) ((char*)curr_ind_block + k * 32);
            if(*curr_ind_block != 0) {
              filler(buf, (char*)curr_ind_block, NULL, 0);
            }
          }
        }
      }
    }
  }
  return 0;
}
