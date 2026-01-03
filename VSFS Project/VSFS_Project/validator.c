#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define DIRECT_POINTERS     8U
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

    uint32_t direct[DIRECT_POINTERS];

    uint32_t ctime;
    uint32_t mtime;

    uint8_t _pad[128 - (2 + 2 + 4 + DIRECT_POINTERS * 4 + 4 + 4)];
};

struct dirent {
    uint32_t inode;
    char name[28];
};

_Static_assert(sizeof(struct superblock) == 128, "superblock must be 128 bytes");
_Static_assert(sizeof(struct inode) == 128, "inode must be 128 bytes");
_Static_assert(sizeof(struct dirent) == 32, "dirent must be 32 bytes");

static int error_count = 0;

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void report_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("ERROR: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    error_count++;
}

static void pread_block(int fd, uint32_t block_index, void *buf) {
    off_t offset = (off_t)block_index * BLOCK_SIZE;
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        die("lseek");
    }
    ssize_t n = read(fd, buf, BLOCK_SIZE);
    if (n != (ssize_t)BLOCK_SIZE) {
        die("pread");
    }
}

static int bitmap_test(const uint8_t *bitmap, uint32_t index) {
    return (bitmap[index / 8] >> (index % 8)) & 0x1;
}

static void bitmap_check_zero_tail(const uint8_t *bitmap, uint32_t valid_bits, const char *name) {
    uint32_t total_bits = BLOCK_SIZE * 8;
    for (uint32_t bit = valid_bits; bit < total_bits; ++bit) {
        if (bitmap_test(bitmap, bit)) {
            report_error("%s bitmap has stray bit set at %u", name, bit);
            return;
        }
    }
}

static void validate_superblock(const struct superblock *sb) {
    if (sb->magic != FS_MAGIC) {
        report_error("invalid superblock magic 0x%08x", sb->magic);
    }
    if (sb->block_size != BLOCK_SIZE) {
        report_error("unexpected block size %u", sb->block_size);
    }
    if (sb->total_blocks != TOTAL_BLOCKS) {
        report_error("unexpected total blocks %u", sb->total_blocks);
    }
    uint32_t expected_inodes = INODE_BLOCKS * (BLOCK_SIZE / INODE_SIZE);
    if (sb->inode_count != expected_inodes) {
        report_error("unexpected inode count %u", sb->inode_count);
    }
    if (sb->journal_block != JOURNAL_BLOCK_IDX) {
        report_error("journal block index mismatch %u", sb->journal_block);
    }
    if (sb->inode_bitmap != INODE_BMAP_IDX) {
        report_error("inode bitmap index mismatch %u", sb->inode_bitmap);
    }
    if (sb->data_bitmap != DATA_BMAP_IDX) {
        report_error("data bitmap index mismatch %u", sb->data_bitmap);
    }
    if (sb->inode_start != INODE_START_IDX) {
        report_error("inode start index mismatch %u", sb->inode_start);
    }
    if (sb->data_start != DATA_START_IDX) {
        report_error("data start index mismatch %u", sb->data_start);
    }
}

static void check_directory(int fd,
                            const struct inode *inode,
                            uint32_t inode_index,
                            const uint8_t *inode_used,
                            uint32_t inode_count,
                            uint32_t *link_refs) {
    if (inode->size % sizeof(struct dirent) != 0) {
        report_error("inode %u directory size %u is not dirent-aligned", inode_index, inode->size);
        return;
    }

    uint32_t bytes_remaining = inode->size;
    uint8_t block[BLOCK_SIZE];
    int saw_dot = 0;
    int saw_dotdot = 0;

    for (uint32_t i = 0; i < DIRECT_POINTERS && bytes_remaining > 0; ++i) {
        uint32_t blk = inode->direct[i];
        if (blk == 0) {
            report_error("inode %u directory missing data block for bytes still remaining", inode_index);
            return;
        }
        pread_block(fd, blk, block);
        uint32_t chunk = bytes_remaining > BLOCK_SIZE ? BLOCK_SIZE : bytes_remaining;
        uint32_t entries = chunk / sizeof(struct dirent);
        const struct dirent *entries_ptr = (const struct dirent *)block;
        for (uint32_t e = 0; e < entries; ++e) {
            const struct dirent *de = &entries_ptr[e];
            if (de->inode == 0 && de->name[0] == '\0') {
                continue;
            }
            if (de->inode >= inode_count) {
                report_error("inode %u directory entry points to out-of-range inode %u", inode_index, de->inode);
                continue;
            }
            if (!inode_used[de->inode]) {
                report_error("inode %u directory entry references free inode %u", inode_index, de->inode);
            }
            if (memchr(de->name, '\0', sizeof(de->name)) == NULL) {
                report_error("inode %u directory entry has unterminated name", inode_index);
                continue;
            }
            if (de->name[0] == '\0') {
                report_error("inode %u directory entry has empty name", inode_index);
                continue;
            }
            link_refs[de->inode]++;
            if (strcmp(de->name, ".") == 0) {
                if (de->inode != inode_index) {
                    report_error("inode %u '.' entry points to %u", inode_index, de->inode);
                }
                saw_dot = 1;
            } else if (strcmp(de->name, "..") == 0) {
                saw_dotdot = 1;
            }
        }
        bytes_remaining -= chunk;
    }

    if (bytes_remaining != 0) {
        report_error("inode %u directory uses more data than direct pointers cover", inode_index);
    }
    if (inode->size > 0) {
        if (!saw_dot) {
            report_error("inode %u directory missing '.' entry", inode_index);
        }
        if (!saw_dotdot) {
            report_error("inode %u directory missing '..' entry", inode_index);
        }
    }
}

int main(int argc, char *argv[]) {
    const char *image_path = (argc > 1) ? argv[1] : DEFAULT_IMAGE;

    int fd = open(image_path, O_RDONLY | O_BINARY);
    if (fd < 0) {
        die("open");
    }

    struct superblock sb;
    pread_block(fd, 0, &sb);
    validate_superblock(&sb);

    uint8_t inode_bitmap[BLOCK_SIZE];
    uint8_t data_bitmap[BLOCK_SIZE];
    pread_block(fd, INODE_BMAP_IDX, inode_bitmap);
    pread_block(fd, DATA_BMAP_IDX, data_bitmap);

    uint32_t inode_count = sb.inode_count;
    uint32_t total_inode_bytes = INODE_BLOCKS * BLOCK_SIZE;
    uint8_t *inode_area = malloc(total_inode_bytes);
    if (!inode_area) {
        die("malloc inode area");
    }
    for (uint32_t i = 0; i < INODE_BLOCKS; ++i) {
        pread_block(fd, INODE_START_IDX + i, inode_area + (i * BLOCK_SIZE));
    }
    struct inode *inodes = (struct inode *)inode_area;

    uint8_t inode_used[inode_count];
    for (uint32_t i = 0; i < inode_count; ++i) {
        inode_used[i] = (inodes[i].type != 0);
    }
    uint32_t *link_refs = calloc(inode_count, sizeof(uint32_t));
    if (!link_refs) {
        die("calloc link refs");
    }

    int data_owner[DATA_BLOCKS];
    memset(data_owner, -1, sizeof(data_owner));
    uint8_t data_blocks_referenced[DATA_BLOCKS];
    memset(data_blocks_referenced, 0, sizeof(data_blocks_referenced));

    for (uint32_t i = 0; i < inode_count; ++i) {
        struct inode *ino = &inodes[i];
        int allocated = ino->type != 0;
        int bitmap_bit = bitmap_test(inode_bitmap, i);
        if (allocated != bitmap_bit) {
            report_error("inode %u allocation mismatch (inode vs bitmap)", i);
        }
        inode_used[i] = allocated;
        if (!allocated) {
            continue;
        }

        if (ino->type > 2) {
            report_error("inode %u has invalid type %u", i, ino->type);
        }

        uint32_t required_blocks = (ino->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        if (required_blocks > DIRECT_POINTERS) {
            report_error("inode %u size %u exceeds direct pointers", i, ino->size);
        }

        uint32_t seen_blocks = 0;
        for (uint32_t d = 0; d < DIRECT_POINTERS; ++d) {
            uint32_t blk = ino->direct[d];
            if (blk == 0) {
                continue;
            }
            seen_blocks++;
            if (blk < DATA_START_IDX || blk >= DATA_START_IDX + DATA_BLOCKS) {
                report_error("inode %u points outside data region (block %u)", i, blk);
                continue;
            }
            uint32_t data_idx = blk - DATA_START_IDX;
            if (data_owner[data_idx] != -1 && data_owner[data_idx] != (int)i) {
                report_error("data block %u referenced by both inode %d and inode %u", blk, data_owner[data_idx], i);
            }
            data_owner[data_idx] = (int)i;
            data_blocks_referenced[data_idx] = 1;
        }

        if (seen_blocks < required_blocks) {
            report_error("inode %u lacks blocks for declared size (need %u have %u)", i, required_blocks, seen_blocks);
        }
        if (required_blocks == 0 && seen_blocks > 0) {
            report_error("inode %u has data blocks but zero size", i);
        }

        if (ino->type == 2) {
            check_directory(fd, ino, i, inode_used, inode_count, link_refs);
        }
    }

    for (uint32_t i = 0; i < inode_count; ++i) {
        if (!inode_used[i]) {
            continue;
        }
        if (inodes[i].links != link_refs[i]) {
            report_error("inode %u link count %u disagrees with directory refs %u", i, inodes[i].links, link_refs[i]);
        }
    }

    for (uint32_t bit = 0; bit < inode_count; ++bit) {
        int bit_val = bitmap_test(inode_bitmap, bit);
        if (bit_val && !inode_used[bit]) {
            report_error("inode bitmap marks %u used but inode is free", bit);
        }
        if (!bit_val && inode_used[bit]) {
            report_error("inode bitmap misses allocated inode %u", bit);
        }
    }
    bitmap_check_zero_tail(inode_bitmap, inode_count, "inode");

    for (uint32_t bit = 0; bit < DATA_BLOCKS; ++bit) {
        int bit_val = bitmap_test(data_bitmap, bit);
        if (bit_val && !data_blocks_referenced[bit]) {
            report_error("data bitmap marks block %u used but no inode references it", bit + DATA_START_IDX);
        }
        if (!bit_val && data_blocks_referenced[bit]) {
            report_error("data block %u referenced but bitmap is clear", bit + DATA_START_IDX);
        }
    }

    bitmap_check_zero_tail(data_bitmap, DATA_BLOCKS, "data");

    if (close(fd) < 0) {
        die("close");
    }

    if (error_count == 0) {
        printf("Filesystem '%s' is consistent.\n", image_path);
        return 0;
    }

    fprintf(stderr, "%d inconsistencies found.\n", error_count);
    return 1;
}
