/*
 * Copy one encrypted TAL-KV entry between two Linux tuyadb images.
 *
 * TAL-KV stores its encrypted values in a LittleFS volume beginning at
 * 0x9000.  Copying the encrypted entry lets service data be migrated without
 * needing the product's encryption seed/key.  The destination image should
 * always be a disposable copy; this tool does not provide rollback.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lfs.h"

#define TUYA_UF_OFFSET     0x9000L
#define TUYA_UF_BLOCK_SIZE 4096
#define TUYA_UF_BLOCKS     24

struct file_block_device {
    FILE *file;
    const char *path;
};

static int image_seek(const struct lfs_config *cfg, lfs_block_t block,
                      lfs_off_t off)
{
    struct file_block_device *dev = cfg->context;
    long absolute = TUYA_UF_OFFSET +
                    (long)block * TUYA_UF_BLOCK_SIZE + (long)off;

    return fseek(dev->file, absolute, SEEK_SET) == 0 ? LFS_ERR_OK : LFS_ERR_IO;
}

static int image_read(const struct lfs_config *cfg, lfs_block_t block,
                      lfs_off_t off, void *buffer, lfs_size_t size)
{
    struct file_block_device *dev = cfg->context;

    if (image_seek(cfg, block, off) != LFS_ERR_OK)
        return LFS_ERR_IO;
    return fread(buffer, 1, size, dev->file) == size ? LFS_ERR_OK : LFS_ERR_IO;
}

static int image_prog(const struct lfs_config *cfg, lfs_block_t block,
                      lfs_off_t off, const void *buffer, lfs_size_t size)
{
    struct file_block_device *dev = cfg->context;

    if (image_seek(cfg, block, off) != LFS_ERR_OK)
        return LFS_ERR_IO;
    if (fwrite(buffer, 1, size, dev->file) != size)
        return LFS_ERR_IO;
    return fflush(dev->file) == 0 ? LFS_ERR_OK : LFS_ERR_IO;
}

/* The Linux adapter models erase as a no-op too; the backing file permits
 * ordinary overwrites and LittleFS still supplies its transactional metadata. */
static int image_erase(const struct lfs_config *cfg, lfs_block_t block)
{
    (void)cfg;
    (void)block;
    return LFS_ERR_OK;
}

static int image_sync(const struct lfs_config *cfg)
{
    struct file_block_device *dev = cfg->context;

    if (fflush(dev->file) != 0)
        return LFS_ERR_IO;
    return fsync(fileno(dev->file)) == 0 ? LFS_ERR_OK : LFS_ERR_IO;
}

static void config_init(struct lfs_config *cfg, struct file_block_device *dev)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->context = dev;
    cfg->read = image_read;
    cfg->prog = image_prog;
    cfg->erase = image_erase;
    cfg->sync = image_sync;
    cfg->read_size = TUYA_UF_BLOCK_SIZE;
    cfg->prog_size = TUYA_UF_BLOCK_SIZE;
    cfg->block_size = TUYA_UF_BLOCK_SIZE;
    cfg->block_count = TUYA_UF_BLOCKS;
    cfg->cache_size = TUYA_UF_BLOCK_SIZE;
    cfg->lookahead_size = 8;
    cfg->block_cycles = 500;
}

static int copy_entry(const char *source_path, const char *dest_path,
                      const char *entry)
{
    struct file_block_device source = {.path = source_path};
    struct file_block_device dest = {.path = dest_path};
    struct lfs_config source_cfg;
    struct lfs_config dest_cfg;
    lfs_t source_lfs;
    lfs_t dest_lfs;
    lfs_file_t source_file;
    lfs_file_t dest_file;
    lfs_soff_t size;
    uint8_t *value = NULL;
    int result = 1;

    source.file = fopen(source.path, "rb");
    dest.file = fopen(dest.path, "r+b");
    if (!source.file || !dest.file) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        goto out_files;
    }

    config_init(&source_cfg, &source);
    config_init(&dest_cfg, &dest);
    if (lfs_mount(&source_lfs, &source_cfg) != LFS_ERR_OK) {
        fprintf(stderr, "source LittleFS mount failed\n");
        goto out_files;
    }
    if (lfs_mount(&dest_lfs, &dest_cfg) != LFS_ERR_OK) {
        fprintf(stderr, "destination LittleFS mount failed\n");
        goto out_source_mount;
    }

    if (lfs_file_open(&source_lfs, &source_file, entry, LFS_O_RDONLY) !=
        LFS_ERR_OK) {
        fprintf(stderr, "source entry not found: %s\n", entry);
        goto out_mounts;
    }
    size = lfs_file_size(&source_lfs, &source_file);
    if (size <= 0 || size > 4096) {
        fprintf(stderr, "invalid source entry size: %ld\n", (long)size);
        goto out_source_file;
    }
    value = malloc((size_t)size);
    if (!value || lfs_file_read(&source_lfs, &source_file, value,
                                (lfs_size_t)size) != size) {
        fprintf(stderr, "source entry read failed\n");
        goto out_source_file;
    }
    lfs_file_close(&source_lfs, &source_file);

    if (lfs_file_open(&dest_lfs, &dest_file, entry,
                      LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
        fprintf(stderr, "destination entry open failed\n");
        goto out_mounts;
    }
    if (lfs_file_write(&dest_lfs, &dest_file, value, (lfs_size_t)size) != size ||
        lfs_file_close(&dest_lfs, &dest_file) != LFS_ERR_OK) {
        fprintf(stderr, "destination entry write failed\n");
        goto out_mounts;
    }

    printf("copied %s (%ld encrypted bytes)\n", entry, (long)size);
    result = 0;
    goto out_mounts;

out_source_file:
    lfs_file_close(&source_lfs, &source_file);
out_mounts:
    lfs_unmount(&dest_lfs);
out_source_mount:
    lfs_unmount(&source_lfs);
out_files:
    free(value);
    if (dest.file)
        fclose(dest.file);
    if (source.file)
        fclose(source.file);
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s SOURCE_DB DEST_DB ENTRY\n", argv[0]);
        return 2;
    }
    return copy_entry(argv[1], argv[2], argv[3]);
}
