#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* File system constants */
#define FS_MAGIC 0x56534653U
#define JOURNAL_MAGIC 0x4A524E4CU

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
#define DEFAULT_IMAGE      "vsfs.img"
#define NAME_LEN            28

/* Record types */
#define REC_DATA            1U
#define REC_COMMIT          2U

/* On-disk data structures */
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
    uint16_t type;        /* 0=free, 1=file, 2=dir */
    uint16_t links;
    uint32_t size;
    uint32_t direct[8];
    uint32_t ctime;
    uint32_t mtime;
    uint8_t _pad[128 - (2 + 2 + 4 + 8 * 4 + 4 + 4)];
};

struct dirent {
    uint32_t inode;
    char name[NAME_LEN];
};

struct journal_header {
    uint32_t magic;       /* JOURNAL_MAGIC */
    uint32_t nbytes_used; /* Total bytes currently used */
};

struct rec_header {
    uint16_t type;  /* REC_DATA or REC_COMMIT */
    uint16_t size;  /* Total size of this record */
};

struct data_record {
    struct rec_header hdr;
    uint32_t block_no;
    uint8_t data[BLOCK_SIZE];
};

struct commit_record {
    struct rec_header hdr;
};

_Static_assert(sizeof(struct superblock) == 128, "superblock must be 128 bytes");
_Static_assert(sizeof(struct inode) == 128, "inode must be 128 bytes");
_Static_assert(sizeof(struct dirent) == 32, "dirent must be 32 bytes");
_Static_assert(sizeof(struct journal_header) == 8, "journal_header must be 8 bytes");
_Static_assert(sizeof(struct rec_header) == 4, "rec_header must be 4 bytes");
_Static_assert(sizeof(struct data_record) == 4 + 4 + BLOCK_SIZE, "data_record size mismatch");
_Static_assert(sizeof(struct commit_record) == 4, "commit_record must be 4 bytes");

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void pread_block(int fd, uint32_t block_index, void *buf) {
    off_t offset = (off_t)block_index * BLOCK_SIZE;
    if (lseek(fd, offset, SEEK_SET) < 0) {
        fprintf(stderr, "lseek failed for block %u offset %lld\n", block_index, (long long)offset);
        die("lseek");
    }
    ssize_t n = read(fd, buf, BLOCK_SIZE);
    if (n != (ssize_t)BLOCK_SIZE) {
        fprintf(stderr, "read failed: expected %d bytes, got %ld\n", BLOCK_SIZE, (long)n);
        die("pread_block");
    }
}

static void pwrite_block(int fd, uint32_t block_index, const void *buf) {
    off_t offset = (off_t)block_index * BLOCK_SIZE;
    if (lseek(fd, offset, SEEK_SET) < 0) {
        fprintf(stderr, "lseek failed for block %u offset %lld\n", block_index, (long long)offset);
        die("lseek");
    }
    ssize_t n = write(fd, buf, BLOCK_SIZE);
    if (n != (ssize_t)BLOCK_SIZE) {
        fprintf(stderr, "write failed: expected %d bytes, wrote %ld\n", BLOCK_SIZE, (long)n);
        die("pwrite_block");
    }
}

/* Bitmap helper functions */
static void bitmap_set(uint8_t *bitmap, uint32_t index) {
    bitmap[index / 8] |= (uint8_t)(1U << (index % 8));
}

static int bitmap_test(const uint8_t *bitmap, uint32_t index) {
    return (bitmap[index / 8] >> (index % 8)) & 0x1;
}

/* Find first free inode in bitmap */
static int find_free_inode(const uint8_t *bitmap, uint32_t inode_count) {
    for (uint32_t i = 1; i < inode_count; ++i) {
        if (!bitmap_test(bitmap, i)) {
            return (int)i;
        }
    }
    return -1;
}

/* Find free directory entry slot in root directory */
static int find_free_dirent(const struct dirent *dirents, uint32_t current_entries) {
    /* First check within current entries */
    for (uint32_t i = 2; i < current_entries; ++i) {  /* Skip first two entries (. and ..) */
        if (dirents[i].inode == 0) {
            return (int)i;
        }
    }
    /* Then check if we can extend the directory within the block */
    uint32_t max_entries = BLOCK_SIZE / sizeof(struct dirent);
    if (current_entries < max_entries) {
        return (int)current_entries;  /* Next slot after current entries */
    }
    return -1;
}

/* Initialize journal header on disk */
static void init_journal(int fd) {
    uint8_t block[BLOCK_SIZE];
    memset(block, 0, sizeof(block));
    
    struct journal_header *jh = (struct journal_header *)block;
    jh->magic = JOURNAL_MAGIC;
    jh->nbytes_used = sizeof(struct journal_header);
    
    pwrite_block(fd, JOURNAL_BLOCK_IDX, block);
}

/* Read journal header */
static void read_journal_header(int fd, struct journal_header *jh) {
    uint8_t block[BLOCK_SIZE];
    pread_block(fd, JOURNAL_BLOCK_IDX, block);
    memcpy(jh, block, sizeof(struct journal_header));
}

/* Write journal header */
static void write_journal_header(int fd, const struct journal_header *jh) {
    uint8_t block[BLOCK_SIZE];
    pread_block(fd, JOURNAL_BLOCK_IDX, block);
    memcpy(block, jh, sizeof(struct journal_header));
    pwrite_block(fd, JOURNAL_BLOCK_IDX, block);
}

/* Append data to journal, return offset where appended (-1 if no space) */
static off_t journal_append(int fd, const void *data, size_t size) {
    struct journal_header jh;
    read_journal_header(fd, &jh);
    
    /* Check if there's enough space */
    if ((size_t)(JOURNAL_BLOCKS * BLOCK_SIZE - jh.nbytes_used) < size) {
        return -1;
    }
    
    /* Compute block and offset within the block */
    uint32_t offset_in_bytes = jh.nbytes_used;
    uint32_t block_idx = JOURNAL_BLOCK_IDX + (offset_in_bytes / BLOCK_SIZE);
    uint32_t offset_in_block = offset_in_bytes % BLOCK_SIZE;
    
    off_t result = offset_in_bytes;
    
    /* Write the data across journal blocks */
    size_t remaining = size;
    const uint8_t *src = (const uint8_t *)data;
    
    while (remaining > 0) {
        uint8_t block[BLOCK_SIZE];
        pread_block(fd, block_idx, block);
        
        size_t to_write = BLOCK_SIZE - offset_in_block;
        if (to_write > remaining) {
            to_write = remaining;
        }
        
        memcpy(block + offset_in_block, src, to_write);
        pwrite_block(fd, block_idx, block);
        
        src += to_write;
        remaining -= to_write;
        offset_in_block = 0;
        block_idx++;
    }
    
    /* Update journal header with new nbytes_used */
    jh.nbytes_used += size;
    write_journal_header(fd, &jh);
    
    return result;
}

/* Read data from journal */
static void journal_read(int fd, off_t offset, void *buf, size_t size) {
    uint32_t block_idx = JOURNAL_BLOCK_IDX + (offset / BLOCK_SIZE);
    uint32_t offset_in_block = offset % BLOCK_SIZE;
    
    uint8_t *dst = (uint8_t *)buf;
    size_t remaining = size;
    
    while (remaining > 0) {
        uint8_t block[BLOCK_SIZE];
        pread_block(fd, block_idx, block);
        
        size_t to_read = BLOCK_SIZE - offset_in_block;
        if (to_read > remaining) {
            to_read = remaining;
        }
        
        memcpy(dst, block + offset_in_block, to_read);
        
        dst += to_read;
        remaining -= to_read;
        offset_in_block = 0;
        block_idx++;
    }
}

/* Clear journal */
static void clear_journal(int fd) {
    uint8_t block[BLOCK_SIZE];
    memset(block, 0, sizeof(block));
    
    struct journal_header *jh = (struct journal_header *)block;
    jh->magic = JOURNAL_MAGIC;
    jh->nbytes_used = sizeof(struct journal_header);
    
    pwrite_block(fd, JOURNAL_BLOCK_IDX, block);
    
    for (uint32_t i = 1; i < JOURNAL_BLOCKS; ++i) {
        memset(block, 0, sizeof(block));
        pwrite_block(fd, JOURNAL_BLOCK_IDX + i, block);
    }
}

/* Create command: log metadata changes to journal */
static void cmd_create(const char *filename) {
    int fd = open(DEFAULT_IMAGE, O_RDWR | O_BINARY);
    if (fd < 0) {
        die("open");
    }
    
    /* Read superblock */
    struct superblock sb;
    {
        uint8_t block[BLOCK_SIZE];
        pread_block(fd, 0, block);
        memcpy(&sb, block, sizeof(sb));
    }
    
    if (sb.magic != FS_MAGIC) {
        fprintf(stderr, "Invalid filesystem magic\n");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    uint32_t inode_count = sb.inode_count;
    
    /* Check if journal is initialized */
    struct journal_header jh;
    read_journal_header(fd, &jh);
    
    if (jh.magic != JOURNAL_MAGIC) {
        /* Initialize journal */
        init_journal(fd);
        read_journal_header(fd, &jh);
    }
    
    /* Read inode bitmap */
    uint8_t inode_bitmap[BLOCK_SIZE];
    pread_block(fd, INODE_BMAP_IDX, inode_bitmap);
    
    /* Read data bitmap */
    uint8_t data_bitmap[BLOCK_SIZE];
    pread_block(fd, DATA_BMAP_IDX, data_bitmap);
    
    /* Find free inode */
    int new_inum = find_free_inode(inode_bitmap, inode_count);
    if (new_inum < 0) {
        fprintf(stderr, "No free inodes\n");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    /* Read root directory inode and data block */
    struct inode root_inode;
    {
        uint8_t block[BLOCK_SIZE];
        pread_block(fd, INODE_START_IDX, block);
        memcpy(&root_inode, block, sizeof(struct inode));
    }
    
    if (root_inode.type != 2) {
        fprintf(stderr, "Root is not a directory\n");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    /* Read root directory data block */
    uint8_t root_data[BLOCK_SIZE];
    pread_block(fd, root_inode.direct[0], root_data);
    
    struct dirent *dirents = (struct dirent *)root_data;
    uint32_t num_dirents = root_inode.size / sizeof(struct dirent);
    
    /* Find free directory entry slot */
    int dirent_slot = find_free_dirent(dirents, num_dirents);
    if (dirent_slot < 0) {
        fprintf(stderr, "No free directory entries in root\n");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    /* Check if there's space in journal for this transaction */
    size_t txn_size = sizeof(struct data_record) +  /* inode bitmap */
                      sizeof(struct data_record) +  /* inode table */
                      sizeof(struct data_record) +  /* root directory */
                      sizeof(struct commit_record);
    
    if ((size_t)(JOURNAL_BLOCKS * BLOCK_SIZE - jh.nbytes_used) < txn_size) {
        fprintf(stderr, "Journal is full, run install first\n");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    /* Prepare updated metadata in memory */
    /* 1. Updated inode bitmap */
    uint8_t new_inode_bitmap[BLOCK_SIZE];
    memcpy(new_inode_bitmap, inode_bitmap, BLOCK_SIZE);
    bitmap_set(new_inode_bitmap, (uint32_t)new_inum);
    
    /* 2. Updated inode table (create new inode) */
    uint8_t inode_table[2 * BLOCK_SIZE];
    {
        pread_block(fd, INODE_START_IDX, inode_table);
        pread_block(fd, INODE_START_IDX + 1, inode_table + BLOCK_SIZE);
    }
    
    struct inode *inodes = (struct inode *)inode_table;
    time_t now = time(NULL);
    inodes[new_inum].type = 1;  /* Regular file */
    inodes[new_inum].links = 1;
    inodes[new_inum].size = 0;
    memset(inodes[new_inum].direct, 0, sizeof(inodes[new_inum].direct));
    inodes[new_inum].ctime = (uint32_t)now;
    inodes[new_inum].mtime = (uint32_t)now;
    
    /* Update root directory size to include new entry */
    inodes[0].size = (dirent_slot + 1) * sizeof(struct dirent);
    inodes[0].mtime = (uint32_t)now;
    
    /* Determine which inode block needs to be logged */
    uint32_t inode_block_idx = (new_inum * INODE_SIZE) / BLOCK_SIZE;
    uint8_t inode_block_to_log[BLOCK_SIZE];
    memcpy(inode_block_to_log, inode_table + inode_block_idx * BLOCK_SIZE, BLOCK_SIZE);
    
    /* 3. Updated root directory */
    uint8_t new_root_data[BLOCK_SIZE];
    memcpy(new_root_data, root_data, BLOCK_SIZE);
    struct dirent *new_dirents = (struct dirent *)new_root_data;
    new_dirents[dirent_slot].inode = (uint32_t)new_inum;
    strncpy(new_dirents[dirent_slot].name, filename, NAME_LEN - 1);
    new_dirents[dirent_slot].name[NAME_LEN - 1] = '\0';
    
    /* Append transaction to journal */
    /* Log inode bitmap */
    {
        struct data_record rec;
        rec.hdr.type = REC_DATA;
        rec.hdr.size = sizeof(struct data_record);
        rec.block_no = INODE_BMAP_IDX;
        memcpy(rec.data, new_inode_bitmap, BLOCK_SIZE);
        
        if (journal_append(fd, &rec, sizeof(rec)) < 0) {
            fprintf(stderr, "Failed to append to journal\n");
            close(fd);
            exit(EXIT_FAILURE);
        }
    }
    
    /* Log inode table block */
    {
        struct data_record rec;
        rec.hdr.type = REC_DATA;
        rec.hdr.size = sizeof(struct data_record);
        rec.block_no = INODE_START_IDX + inode_block_idx;
        memcpy(rec.data, inode_block_to_log, BLOCK_SIZE);
        
        if (journal_append(fd, &rec, sizeof(rec)) < 0) {
            fprintf(stderr, "Failed to append to journal\n");
            close(fd);
            exit(EXIT_FAILURE);
        }
    }
    
    /* Log root directory block */
    {
        struct data_record rec;
        rec.hdr.type = REC_DATA;
        rec.hdr.size = sizeof(struct data_record);
        rec.block_no = root_inode.direct[0];
        memcpy(rec.data, new_root_data, BLOCK_SIZE);
        
        if (journal_append(fd, &rec, sizeof(rec)) < 0) {
            fprintf(stderr, "Failed to append to journal\n");
            close(fd);
            exit(EXIT_FAILURE);
        }
    }
    
    /* Log commit record */
    {
        struct commit_record rec;
        rec.hdr.type = REC_COMMIT;
        rec.hdr.size = sizeof(struct commit_record);
        
        if (journal_append(fd, &rec, sizeof(rec)) < 0) {
            fprintf(stderr, "Failed to append commit to journal\n");
            close(fd);
            exit(EXIT_FAILURE);
        }
    }
    
    close(fd);
    printf("Created file '%s' (inode %d)\n", filename, new_inum);
}

/* Install command: replay committed journal transactions */
static void cmd_install(void) {
    int fd = open(DEFAULT_IMAGE, O_RDWR | O_BINARY);
    if (fd < 0) {
        die("open");
    }
    
    /* Read superblock */
    struct superblock sb;
    {
        uint8_t block[BLOCK_SIZE];
        pread_block(fd, 0, block);
        memcpy(&sb, block, sizeof(sb));
    }
    
    if (sb.magic != FS_MAGIC) {
        fprintf(stderr, "Invalid filesystem magic\n");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    /* Check if journal exists */
    struct journal_header jh;
    read_journal_header(fd, &jh);
    
    if (jh.magic != JOURNAL_MAGIC) {
        fprintf(stderr, "Journal does not exist\n");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    /* Parse and replay journal */
    off_t offset = sizeof(struct journal_header);
    uint32_t txn_count = 0;
    
    while (offset < (off_t)jh.nbytes_used) {
        /* Read record header */
        struct rec_header rh;
        journal_read(fd, offset, &rh, sizeof(rh));
        
        if (rh.type == REC_DATA) {
            /* Read data record content: block_no + data */
            uint32_t block_no;
            uint8_t block_data[BLOCK_SIZE];
            journal_read(fd, offset + sizeof(rh), &block_no, sizeof(uint32_t));
            journal_read(fd, offset + sizeof(rh) + sizeof(uint32_t), block_data, BLOCK_SIZE);
            
            /* Write block to home location */
            pwrite_block(fd, block_no, block_data);
            
            offset += rh.size;
        } else if (rh.type == REC_COMMIT) {
            /* Transaction committed, move to next */
            txn_count++;
            offset += rh.size;
        } else {
            /* Unknown record type, stop parsing */
            fprintf(stderr, "Unknown record type at offset %ld\n", offset);
            break;
        }
        
        /* Safety check to avoid infinite loop */
        if (offset > (off_t)(JOURNAL_BLOCKS * BLOCK_SIZE)) {
            fprintf(stderr, "Journal offset overflow\n");
            break;
        }
    }
    
    /* Clear journal after successful replay */
    clear_journal(fd);
    
    close(fd);
    printf("Installed %u transactions\n", txn_count);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s create <name>\n", argv[0]);
        fprintf(stderr, "       %s install\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    if (strcmp(argv[1], "create") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s create <name>\n", argv[0]);
            exit(EXIT_FAILURE);
        }
        cmd_create(argv[2]);
    } else if (strcmp(argv[1], "install") == 0) {
        cmd_install();
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    
    return 0;
}
