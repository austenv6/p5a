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
  //test print of inodes
  // for (int i = 0; i < sb->ninodes; ++i) {
  //   printf("%d type: %d size %d addrs: ", i, dip[i].type, dip[i].size);  
  //   for (int j = 0; j < NDIRECT+1; ++j) {
  //     printf("%u ", dip[i].addrs[j]);
  //   } 
  //   printf ("\n");
  // }

  // next thing is to figure out where the bitmap is
  //Unused | Superblock | Inodes ... | Unused | Bitmap | Data ...
  // 3*BSIZE to skip unused, sb, unused; sb->ninodes/IPB to get number of blocks;
  // then multiply that by BSIZE to advance pointer n blocks.
  
  unsigned char* bitmap = (unsigned char *) (imgptr + (3*BSIZE) + ((sb->ninodes/IPB) * BSIZE));

  //to make things easier, going to store the bitmap into an array
  int bitmap_arr[sb->size];
  int charCt = 0;
  int count = 0;
  for (int i = 0; i < sb->size; ++i) {
    char x = bitmap[charCt];
    int mask = 1 << count;
    bitmap_arr[i] = (mask & x) >> (count);
    ++count;
    if (count == 8) {
      ++charCt;
      count = 0;
    }
  }
  
  //printf("bitmap = %p\n", bitmap);
  //int num_bits_bitmap = sb->nblocks + sb->ninodes/IPB + 3;
  //int bitmap_size = (sb->nblocks + sb->ninodes/IPB + sb->nblocks/BPB + 4) / 8;
  //int num_blocks_bitmap = (sb->size / (BPB)) ;//+ 1;

  // printf("num bits in bitmap = %d\n", num_bits_bitmap);
  // printf("bitmap size = %d\n", bitmap_size);
  // printf("num blocks bitmap = %d\n", num_blocks_bitmap);

  // finally get pointer to where the data lives. Assuming here that the bitmap
  // is 1 block based on sb->size / (512 * 8) + 1.
  void* dataptr = (void *) (imgptr + (4*BSIZE) + ((sb->ninodes/IPB) * BSIZE));
  //printf("dataptr = %p\n", dataptr);

  // check that all inodes = 0 1 2 or 3
  for (int i = 0; i < sb->ninodes; ++i) {
    if (dip[i].type < 0 || dip[i].type > 3) {
      // error time
      fprintf(stderr, "ERROR: bad inode.\n");
      exit(1);
    }
  }

  int first_db = (dataptr - imgptr) / BSIZE;
  int last_db = sb->nblocks + first_db; // should this be -1? Not sure if count starts at 0.
  //printf("first %d last %d\n", first_db, last_db);
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
    }
  }

  // 3. Root directory exists, its inode number is 1, and the parent of the root directory is itself. 
  // ERROR: root directory does not exist.
  // I think root is always at 29...
  struct dirent *de = (struct dirent *) (imgptr + (29*BSIZE));
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
  //set the first 28 to 1 since that should always be true more or less.
  for (int i = 0; i < 29; ++i) {
    used_addr[i] = 1;
  }

  for (int i = 0; i < sb->ninodes; ++i) {
    if (dip[i].type > 0) { // means the inode is in use
      for (int j = 0; j < NDIRECT; ++j) {
        if (dip[i].addrs[j] != 0) {
          used_addr[dip[i].addrs[j]] = 1; // set this address to used.
          if (bitmap_arr[dip[i].addrs[j]] == 0) {
            fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
            exit(1);
          }
        }

      }
      // now check indirect
      if (dip[i].addrs[NDIRECT] != 0) {
        used_addr[dip[i].addrs[NDIRECT]] = 1;
        // per bitmap help slide I think indirect address points to an entire block of addresses
        uint* indir_addrs = (uint *) (imgptr + (dip[i].addrs[NDIRECT] * BSIZE));
        // number uints in a block = 512 / 8 or BSIZE/8
        for (int i = 0; i < BSIZE/sizeof(uint); ++i) {
          if (indir_addrs[i] != 0) {
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
    //printf("bitmap_arr[%d] = %d\n", i, bitmap_arr[i]);
    if (bitmap_arr[i] == 1 && used_addr[i] != 1) {
      fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
      exit(1);
    }
  }
  


  return 0;
}