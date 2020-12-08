#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

#define stat xv6_stat  // avoid clash with host struct stat
#define dirent xv6_dirent  // avoid clash with host struct stat
#undef stat
#undef dirent

// -----copied in from fs.h-----------
//Should verify the below comments are actually TRUE
// Block 0 is unused.
// Block 1 is super block.
// Inodes start at block 2.

#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size

// File system super block
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
};

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses. lots of direct pointers and then
  // one additional indirect
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i)     ((i) / IPB + 2)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block containing bit for block b
#define BBLOCK(b, ninodes) (b/BPB + (ninodes)/IPB + 3)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14
#define DIRPB (BSIZE / sizeof(struct dirent)) // added this for directories per block

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};
// -------end of stuff copied from fs.h--------

int main(int argc, char *argv[]) {

  if (argc != 2) {
    printf("Usage: xv6_fsck <file_system_image>.\n");
    return -1;
  }

  int fd = open(argv[1], O_RDONLY);
  if (fd == -1) {
    fprintf(stderr, "image not found.\n");
    exit(1);
  }

  // how shoudl we read the sector?
  int rc;

  struct stat sbuf; // need to determine the file size. Use stat to do this.
  rc = fstat(fd, &sbuf);
  assert(rc == 0);

  // Use mmap() instead. memory mapping a file. put the file  in my address space starting at some pointer and then
  // make suer any time I refer to bytes within that addr space, you bring the contents fo the file into my memory
  // and map it into my address space at taht location. allows us to use pointers and pointer arithmetic to 
  // access different parts rather than using read(), lseek() and gross stuff like taht.
  void *imgptr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert(imgptr != MAP_FAILED);

  struct superblock *sb;
  sb = (struct superblock*) (imgptr + BSIZE); // set sb equal to the start of this buffer
  //printf("Image size (blocks): %d Data blocks: %d inodes: %d\n", sb->size, sb->nblocks, sb->ninodes); // will print first three fields of the sb
  
  //mmap is great because now we have this pointer that means we don't have to read and seek.
  // we can just treat the file as if it is in memory at this point and mmap handles everything else.

  //next thing to do is cast as struct dinode to point to the first inode
  struct dinode* dip = (struct dinode *) (imgptr + (2*BSIZE)); //(since we skip first two blocks)


  // next thing is to figure out where the bitmap is
  //Unused | Superblock | Inodes ... | Unused | Bitmap | Data ...
  // 3*BSIZE to skip unused, sb, unused; sb->ninodes/IPB to get number of blocks;
  // then multiply that by BSIZE to advance pointer n blocks.
  
  char* bitmap = (char *) (imgptr + (3*BSIZE) + ((sb->ninodes/IPB) * BSIZE));

  int bitmap_arr[sb->size];
  int charCt = 0;
  int count = 0;
  for (int i = 0; i < sb->size; ++i) {
    char x = bitmap[charCt];
    int shift = (x >> count) & 1; // shift bit I care about to right and mask everything else with a & 1.
    bitmap_arr[i] = shift;
    ++count;
    if (count == 8) {
      ++charCt;
      count = 0;
    }
  }
  // finally get pointer to where the data lives. Assuming here that the bitmap
  // is 1 block based on sb->size / (512 * 8) + 1.
  void* dataptr = (void *) (imgptr + (4*BSIZE) + ((sb->ninodes/IPB) * BSIZE));
  int start_of_data = ((dataptr - imgptr) / BSIZE); // created this becasue of probs with goodlarge

  // check that all inodes = 0 1 2 or 3
  for (int i = 0; i < sb->ninodes; ++i) {
    if (dip[i].type < 0 || dip[i].type > 3) {
      fprintf(stderr, "ERROR: bad inode.\n");
      exit(1);
    }
  }

  int first_db = (dataptr - imgptr) / BSIZE;
  int last_db = sb->nblocks + first_db; // should this be -1? Not sure if count starts at 0.
  // looks like the addresses stored in dip are actually just offsets
  // find the in use inodes. check address is valid - points to valid datablock address within the image

  for (int i = 0; i < sb->ninodes; ++i) {
    if (dip[i].type > 0) {
      for (int j = 0; j < NDIRECT; ++j) {// might need to iterate through the indirect guy too
        // if the block is used and invalid, print an error
        if (dip[i].addrs[j] != 0 && (dip[i].addrs[j] < first_db || 
          dip[i].addrs[j] > last_db)) {
          fprintf(stderr, "ERROR: bad direct address in inode.\n");
          exit(1);
        }
      }
      // now do the indirect check
      if (dip[i].addrs[NDIRECT] != 0 && (dip[i].addrs[NDIRECT] < first_db || dip[i].addrs[NDIRECT] > last_db)) {
        fprintf(stderr, "ERROR: bad indirect address in inode.\n");
        exit(1);
      }
      // examine the actual block of indirect addresses
      uint* nindir = (uint *) (imgptr + (dip[i].addrs[NDIRECT]) * BSIZE);
      for (int j = 0; j < NINDIRECT; ++j) {
        if (nindir[j] != 0 && (nindir[j] < first_db || nindir[j] > last_db)) {
          fprintf(stderr, "ERROR: bad indirect address in inode.\n");
          exit(1);
        }
      }
    }
  }

  // 3. Root directory exists, its inode number is 1, and the parent of the root directory is itself. 
  // ERROR: root directory does not exist.
  // I think root is always at 29...
  struct dirent *de = (struct dirent *) (imgptr + (start_of_data * BSIZE));
  // check the dot
  if (dip[1].type != 1 || strcmp(de[0].name, ".") != 0 || de[0].inum != 1) {
    fprintf(stderr, "ERROR: root directory does not exist.\n");
    exit(1);
  }
  // check the dot dot
  if (dip[1].type != 1 || strcmp(de[1].name, "..") != 0 || de[1].inum != 1) {
    fprintf(stderr, "ERROR: root directory does not exist.\n");
    exit(1);
  }

  // 4. Each directory contains . and .. entries, and the . entry points to the directory itself. 
  // ERROR: directory not properly formatted.\n
  for (int i = 0; i < sb->ninodes; ++i) {
    if (dip[i].type == 1) { // means it is a directory
      struct dirent *deCheck = (struct dirent *) (imgptr + (dip[i].addrs[0] * BSIZE));
        if (strcmp(deCheck[0].name, ".") != 0 || deCheck[0].inum != i || strcmp(deCheck[1].name, "..") != 0) {
          fprintf(stderr, "ERROR: directory not properly formatted.\n");
          exit(1);
        }
    }
  }

  // 5. For in-use inodes, each address in use is also marked in use in the bitmap. 
  // ERROR: address used by inode but marked free in bitmap.\n
  // also going to catch all these in use addresses in an array
  int used_addr[sb->size]; // hope this sets them all to zero initially
  // initialize evryting to zero to be safe
  for (int i = 0; i < sb->size; ++i) {
    used_addr[i] = 0;
  }
  //set all blocks before first data block to in use since that should always be true more or less.
  for (int i = 0; i < start_of_data; ++i) {
    used_addr[i] = 1;
  }

  for (int i = 0; i < sb->ninodes; ++i) {
    if (dip[i].type > 0) { // means the inode is in use
      for (int j = 0; j < NDIRECT; ++j) {
        if (dip[i].addrs[j] != 0) {
          if (used_addr[dip[i].addrs[j]] != 0) {// would mean address has already been used. #7.
            fprintf(stderr, "ERROR: direct address used more than once.\n");
            exit(1);
          }
          used_addr[dip[i].addrs[j]] = 1; // set this address to used.
          if (bitmap_arr[dip[i].addrs[j]] == 0) {
            fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
            exit(1);
          }
        }

      }
      // now check indirect
      if (dip[i].addrs[NDIRECT] != 0) {
          if (used_addr[dip[i].addrs[NDIRECT]] != 0) {// would mean address has already been used. #8.
            fprintf(stderr, "ERROR: indirect address used more than once.\n");
            exit(1);
          }
        used_addr[dip[i].addrs[NDIRECT]] = 1;
        // per bitmap help slide I think indirect address points to an entire block of addresses
        uint* indir_addrs = (uint *) (imgptr + (dip[i].addrs[NDIRECT] * BSIZE));
        // number uints in a block = 512 / 8 or BSIZE/8
        for (int i = 0; i < NINDIRECT; ++i) {
          if (indir_addrs[i] != 0) {
            if (used_addr[indir_addrs[i]] != 0) {// would mean address has already been used. #8.
              fprintf(stderr, "ERROR: indirect address used more than once.\n");
              exit(1);
            }
            used_addr[indir_addrs[i]] = 1;
            if (bitmap_arr[indir_addrs[i]] == 0) {
              fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
              exit(1);
            }
          }
        }
      }
    }
  }

  // 6. For blocks marked in-use in bitmap, actually is in-use in an inode or indirect block somewhere. 
  // ERROR: bitmap marks block in use but it is not in use.\n
  for (int i = 0; i < sb->nblocks; ++i) {
    if (bitmap_arr[i] == 1 && used_addr[i] != 1) {
      fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
      exit(1);
    }
  }

  // 9. For all inodes marked in use, must be referred to in at least one directory. 
  // ERROR: inode marked use but not found in a directory.\n  
  // maybe store all the inums of dirents in an array?
  int inode_in_dir[sb->ninodes];
  int inode_refs[sb->ninodes];
  int dir_refs[sb->ninodes];
  //set all to zero just in case.
  for (int i = 0; i < sb->ninodes; ++i) {
    inode_in_dir[i] = 0;
    inode_refs[i] = 0;
    dir_refs[i] = 0;
  }
  // now loop through directories and set to used where appropriate
  for (int i = 0; i < sb->ninodes; ++i) {
    if (dip[i].type == 1) { // means it is a directory
      for (int j = 0; j < NDIRECT; ++j) { // iterate through the normal addresses
        // pointer to the first directory struct in your block of directories
        struct dirent *dirCheck = (struct dirent *) (imgptr + (dip[i].addrs[j] * BSIZE));
        // now iterate through all 32 possible addrs.
        for (int k = 0; k < DIRPB; ++k) {
          inode_in_dir[dirCheck[k].inum] = 1;
          inode_refs[dirCheck[k].inum]++;
          //bug here wher I keep counting the dot and dot dot
          if (   (strcmp(dirCheck[k].name, ".") != 0) && (strcmp(dirCheck[k].name, "..") != 0)   ) {
            dir_refs[dirCheck[k].inum]++;
          }
          
        }
      }
      // pointer to first of 128 addrs in indir block
      uint* ind_addrs = (uint *) (imgptr + (dip[i].addrs[NDIRECT]) * BSIZE);
      // loop through the addresses
      for (int j = 0; j < NINDIRECT; ++j) {
        // grab pointers to their directory blocks
        struct dirent *dirCheck = (struct dirent *)(imgptr + (ind_addrs[j] * BSIZE));
        // loop through the directory blocks
        for (int k = 0; k < DIRPB; ++k) {
          inode_in_dir[dirCheck[k].inum] = 1;
          inode_refs[dirCheck[k].inum]++;
          dir_refs[dirCheck[k].inum]++;
        }
      }
    }
  }

  //checking 9 and 10 inside this loop.
  // now loop through inodes and check - if in use but inode_in_dir = 0, error out
  for (int i = 0; i < sb->ninodes; ++i) {
    if (dip[i].type > 0) { // means it is a directory
      if (inode_in_dir[i] == 0) {
        fprintf(stderr, "ERROR: inode marked use but not found in a directory.\n");
        exit(1);
      }
    }
    // 10. For each inode number that is referred to in a valid directory, it is actually marked in use. 
    // ERROR: inode referred to in directory but marked free.\n
    for (int i = 1; i < sb->ninodes; ++i) { // was failing on zero inode so skipping since it doesn't actually matter.
      if ((inode_in_dir[i] == 1) && (dip[i].type < 1)) {
        fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
        exit(1);
      }
    }
  }

  // 11. Reference counts (number of links) for regular files match the number of times file is referred to in 
  // directories (i.e., hard links work correctly). 
  // 
  // trick here is to count how many times a file is referenced in all the directories. Make another array of
  // times an inum is referenced.
  for (int i = 1; i < sb->ninodes; ++i) {
    if (dip[i].type == 2) {
      if (inode_refs[i] != dip[i].nlink) {
        fprintf(stderr, "ERROR: bad reference count for file.\n");
        exit(1);
      }
    }
    // 12. No extra links allowed for directories (each directory only appears in one other directory). 
    // ERROR: directory appears more than once in file system.\n
    if (dip[i].type == 1) {
      if (dir_refs[i] > 1) {
        fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
        exit(1);
      }
    }
    
  }

  // EC1. Each .. entry in the directory refers to the proper parent inode, and parent inode points back to it. 
  // ERROR: parent directory mismatch.\n

  // for (int i = 0; i < sb->ninodes; ++i) {
  //   if (dip[i].type == 1) { // means it is a directory
  //     for (int j = 0; j < NDIRECT; ++j) { // iterate through the normal addresses
  //       // pointer to the first directory struct in your block of directories
  //       struct dirent *dirCheck = (struct dirent *) (imgptr + (dip[i].addrs[j] * BSIZE));
  //       // now iterate through all 32 possible addrs.
  //       for (int k = 0; k < DIRPB; ++k) {
  //         //bug here wher I keep counting the dot and dot dot
  //         if ((strcmp(dirCheck[k].name, "..") == 0)) {
  //           printf("dirCheck[%d].inum = %d\n", k, dirCheck[k].inum);

  //         }
          
  //       }
  //     }
  //     // pointer to first of 128 addrs in indir block
  //     uint* ind_addrs = (uint *) (imgptr + (dip[i].addrs[NDIRECT]) * BSIZE);
  //     // loop through the addresses
  //     for (int j = 0; j < NINDIRECT; ++j) {
  //       // grab pointers to their directory blocks
  //       struct dirent *dirCheck = (struct dirent *)(imgptr + (ind_addrs[j] * BSIZE));
  //       // loop through the directory blocks
  //       for (int k = 0; k < DIRPB; ++k) {
  //         if ((strcmp(dirCheck[k].name, "..") == 0)) {
  //           printf("dirCheckLower[%d].inum = %d\n", k, dirCheck[k].inum);
  //         }
  //       }
  //     }
  //   }
  // }

  return 0;
}