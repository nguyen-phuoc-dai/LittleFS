/* Copyright 2019 wecheer.io. All Rights Reserved.
 * @author Nguyen Phuoc Dai
 */
 
 /**
 * @file esp_lfs.h
 * @brief x
 *
 * x
 */


#ifndef _ESP_LFS_H_
#define _ESP_LFS_H_

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration structure for esp_vfs_lfs_register
 */
typedef struct {
        const char* base_path;          /*!< File path prefix associated with the filesystem. */
        const char* partition_label;    /*!< Optional, label of LFS partition to use. If set to NULL, first partition with subtype=lfs will be used. */
        size_t max_files;               /*!< Maximum files that could be open at the same time. */
        bool format_if_mount_failed;    /*!< If true, it will format the file system if it fails to mount. */
} esp_vfs_lfs_conf_t;

/**
 * Register and mount LFS to VFS with given path prefix.
 *
 * @param   conf                      Pointer to esp_vfs_lfs_conf_t configuration structure
 *
 * @return  
 *          - ESP_OK                  if success
 *          - ESP_ERR_NO_MEM          if objects could not be allocated
 *          - ESP_ERR_INVALID_STATE   if already mounted or partition is encrypted
 *          - ESP_ERR_NOT_FOUND       if partition for LFS was not found
 *          - ESP_FAIL                if mount or format fails
 */
esp_err_t esp_vfs_lfs_register(const esp_vfs_lfs_conf_t * conf);

/**
 * Unregister and unmount LFS from VFS
 *
 * @param partition_label  Optional, label of the partition to unregister.
 *                         If not specified, first partition with subtype=lfs is used.
 *
 * @return  
 *          - ESP_OK if successful
 *          - ESP_ERR_INVALID_STATE already unregistered
 */
esp_err_t esp_vfs_lfs_unregister(const char* partition_label);

/**
 * Check if LFS is mounted
 *
 * @param partition_label  Optional, label of the partition to check.
 *                         If not specified, first partition with subtype=lfs is used.
 *
 * @return  
 *          - true    if mounted
 *          - false   if not mounted
 */
bool esp_lfs_mounted(const char* partition_label);

/**
 * Format the LFS partition
 *
 * @param partition_label  Optional, label of the partition to format.
 *                         If not specified, first partition with subtype=lfs is used.
 * @return  
 *          - ESP_OK      if successful
 *          - ESP_FAIL    on error
 */
esp_err_t esp_lfs_format(const char* partition_label);

/**
 * Get information for LFS
 *
 * @param partition_label           Optional, label of the partition to get info for.
 *                                  If not specified, first partition with subtype=lfs is used.
 * @param[out] total_bytes          Size of the file system
 * @param[out] used_bytes           Current used bytes in the file system
 *
 * @return  
 *          - ESP_OK                  if success
 *          - ESP_ERR_INVALID_STATE   if not mounted
 */
esp_err_t esp_lfs_info(const char* partition_label, size_t *total_bytes, size_t *used_bytes);

#ifdef __cplusplus
}
#endif

#endif /* _ESP_LFS_H_ */
