#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>


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

//xv6 fs img
//similar to vsfs very simple file system. There's no inode bitmap though. The inodes themselves
// contain info about whether or not they are in use.
// approx layout: somewhere on the image there's ...
// superblock | inode table | bitmap (data) | data blocks
// there are also some gaps in there

int main(int argc, char *argv[]) {

  if (argc != 2) {
    printf("usage: %s fs.img\n", argv[0]);
    return -1;
  }

  int fd = open("fs.img", O_RDONLY);
  assert(fd > -1);

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

  // we need to find a way to tell the superblock pointer where the eff the superblock actually is.
  struct superblock *sb;
  sb = (struct superblock*) (imgptr + BSIZE); // set sb equal to the start of this buffer
  printf("Image size (blocks): %d Data blocks: %d inodes: %d\n", sb->size, sb->nblocks, sb->ninodes); // will print first three fields of the sb
  //mmap is great because now we have this pointer that means we don't have to read and seek.
  // we can just treat the file as if it is in memory at this point and mmap handles everything else.

  //next thing to do is cast s struct dinode to point to the first inode!
  int i;
  struct dinode* dip = (struct dinode *) (imgptr + (2*BSIZE)); //(since we skip first two blocks)
  for (i = 0; i < sb->ninodes; ++i) {
    printf("%d type: %d\n", i, dip->type);
    dip++;
  }

  // next thing is to figure out where the bitmap is

  // then do other stuff (rest of p5)

  return 0;
}