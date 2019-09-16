/* Copyright 2019 wecheer.io. All Rights Reserved.
 * @author Nguyen Phuoc Dai
 */

 /**
 * @file lfs_api.h
 * @brief x
 *
 * x
 */

#ifndef _LFS_API_H_
#define _LFS_API_H_

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lfs.h"
#include "esp_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vfs_fd
{
	lfs_file_t *file;				/*!< LittleFS file object */
    char *path;						/*!< Full path name of file */
} vfs_fd_t;

typedef struct vfs_lfs_dir
{
    DIR dir;                		/*!< Must be first...ESP32 VFS expects it... */
    struct dirent dirent;
    lfs_dir_t lfs_dir;
    long off;
} vfs_lfs_dir_t;

/**
 * @brief LittleFS definition structure
 */
typedef struct {
	lfs_t *fs;                         		/*!< Handle to the underlying LittleFS */
    SemaphoreHandle_t lock;                 /*!< FS lock */
    const esp_partition_t *partition;       /*!< The partition on which LittleFS is located */
    char base_path[ESP_VFS_PATH_MAX+1];     /*!< Mount point */
    bool by_label;                          /*!< Partition was mounted by label */
    struct lfs_config cfg;                  /*!< LittleFS Mount configuration */
    vfs_fd_t *fds;							/*!< File descriptors */
    size_t max_files;						/*!< Maximum files that could be open at the same time. */
    bool mounted;							/*!< Partition was mounted */
    uint32_t sector_sz;						/*!< Sector size */
} esp_lfs_t;

int lfs_api_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size);

int lfs_api_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size);

int lfs_api_erase(const struct lfs_config *c, lfs_block_t block);

int lfs_api_sync(const struct lfs_config *c);

#ifdef __cplusplus
}
#endif

#endif /* _LFS_API_H_ */
