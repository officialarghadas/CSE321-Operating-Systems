#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define FS_MAGIC 0x56534653U

#define BLOCK_SIZE        4096U
#define INODE_SIZE         128U
#define JOURNAL_BLOCK_IDX    1U
#define JOURNAL_BLOCKS      16U
#define INODE_BLOCKS         2U
#define DATA_BLOCKS         64U
#define INODE_BMAP_IDX     (JOURNAL_BLOCK_IDX + JOURNAL_BLOCKS)
#define DATA_BMAP_IDX      (INODE_BMAP_IDX + 1U)
#define INODE_START_IDX    (DATA_BMAP_IDX + 1U)
#define DATA_START_IDX     (INODE_START_IDX + INODE_BLOCKS)
#define TOTAL_BLOCKS       (DATA_START_IDX + DATA_BLOCKS)
#define DEFAULT_IMAGE "vsfs.img"

struct superblock {
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;

    uint32_t journal_block;
    uint32_t inode_bitmap;
    uint32_t data_bitmap;
    uint32_t inode_start;
    uint32_t data_start;

    uint8_t  _pad[128 - 9 * 4];
};

struct inode {
    uint16_t type;
    uint16_t links;
    uint32_t size;

    uint32_t direct[8];

    uint32_t ctime;
    uint32_t mtime;

    uint8_t _pad[128 - (2 + 2 + 4 + 8 * 4 + 4 + 4)];
};

struct dirent {
    uint32_t inode;
    char name[28];
};

_Static_assert(sizeof(struct superblock) == 128, "superblock must be 128 bytes");
_Static_assert(sizeof(struct inode) == 128, "inode must be 128 bytes");
_Static_assert(sizeof(struct dirent) == 32, "dirent must be 32 bytes");

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void write_block(int fd, const void *block) {
    ssize_t written = write(fd, block, BLOCK_SIZE);
    if (written != (ssize_t)BLOCK_SIZE) {
        die("write");
    }
}

static void set_bitmap(uint8_t *bitmap, uint32_t index) {
    bitmap[index / 8] |= (uint8_t)(1U << (index % 8));
}

int main(int argc, char *argv[]) {
    const char *image_path = (argc > 1) ? argv[1] : DEFAULT_IMAGE;

    int fd = open(image_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        die("open");
    }

    uint8_t block[BLOCK_SIZE];
    memset(block, 0, sizeof(block));

    struct superblock sb = {
        .magic = FS_MAGIC,
        .block_size = BLOCK_SIZE,
        .total_blocks = TOTAL_BLOCKS,
        .inode_count = INODE_BLOCKS * (BLOCK_SIZE / INODE_SIZE),
        .journal_block = JOURNAL_BLOCK_IDX,
        .inode_bitmap = INODE_BMAP_IDX,
        .data_bitmap = DATA_BMAP_IDX,
        .inode_start = INODE_START_IDX,
        .data_start = DATA_START_IDX,
    };

    memcpy(block, &sb, sizeof(sb));
    write_block(fd, block); // Superblock

    memset(block, 0, sizeof(block));
    for (uint32_t i = 0; i < JOURNAL_BLOCKS; ++i) {
        write_block(fd, block); // Journal blocks
    }

    memset(block, 0, sizeof(block));
    set_bitmap(block, 0); // Reserve inode 0 for root
    write_block(fd, block); // Inode bitmap

    memset(block, 0, sizeof(block));
    set_bitmap(block, 0); // Reserve first data block for root directory
    write_block(fd, block); // Data bitmap

    time_t now = time(NULL);

    struct inode root = {0};
    root.type = 2; // directory
    root.links = 2; // "." and ".."
    root.size = 2 * sizeof(struct dirent);
    memset(root.direct, 0, sizeof(root.direct));
    root.direct[0] = DATA_START_IDX;
    root.ctime = (uint32_t)now;
    root.mtime = (uint32_t)now;

    memset(block, 0, sizeof(block));
    memcpy(block, &root, sizeof(root));
    write_block(fd, block); // First inode block

    memset(block, 0, sizeof(block));
    write_block(fd, block); // Second inode block

    memset(block, 0, sizeof(block));
    struct dirent *root_dirents = (struct dirent *)block;
    root_dirents[0].inode = 0;
    strncpy(root_dirents[0].name, ".", sizeof(root_dirents[0].name) - 1);
    root_dirents[0].name[sizeof(root_dirents[0].name) - 1] = '\0';
    root_dirents[1].inode = 0;
    strncpy(root_dirents[1].name, "..", sizeof(root_dirents[1].name) - 1);
    root_dirents[1].name[sizeof(root_dirents[1].name) - 1] = '\0';
    write_block(fd, block); // First data block holds root directory entries

    memset(block, 0, sizeof(block));
    for (uint32_t i = 1; i < DATA_BLOCKS; ++i) {
        write_block(fd, block);
    }

    if (close(fd) < 0) {
        die("close");
    }

    printf("Created VSFS image '%s' (%u blocks).\n", image_path, TOTAL_BLOCKS);
    return 0;
}
