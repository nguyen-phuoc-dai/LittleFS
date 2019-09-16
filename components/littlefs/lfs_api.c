/* Copyright 2019 wecheer.io. All Rights Reserved.
 * @author Nguyen Phuoc Dai
 */

 /**
 * @file lfs_api.c
 * @brief x
 *
 * x
 */

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_lfs.h"
#include "esp_vfs.h"
#include "lfs_api.h"

static const char* TAG = "LFS";

int lfs_api_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
	esp_lfs_t *efs = (esp_lfs_t *) (c->context);

	ESP_LOGD(TAG, "%s - block=0x%08x off=0x%08x size=%d", __func__, block, off, size);

	esp_err_t err = esp_partition_read(efs->partition, (block * efs->sector_sz) + off, buffer, size);
    if (err != ESP_OK ) {
        ESP_LOGE(TAG, "failed to read addr %08x, size %08x, err %d", (block * efs->sector_sz) + off, size, err);
        return LFS_ERR_IO;
    }
	return LFS_ERR_OK;
}

int lfs_api_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
	esp_lfs_t *efs = (esp_lfs_t *) (c->context);

	ESP_LOGD(TAG, "%s - block=0x%08x off=0x%08x size=%d", __func__, block, off, size);

	esp_err_t err = esp_partition_write(efs->partition, (block * efs->sector_sz) + off, buffer, size);
    if (err != ESP_OK ) {
        ESP_LOGE(TAG, "failed to write addr %08x, size %08x, err %d", (block * efs->sector_sz) + off, size, err);
        return LFS_ERR_IO;
    }
	return LFS_ERR_OK;
}

int lfs_api_erase(const struct lfs_config *c, lfs_block_t block)
{
	esp_lfs_t *efs = (esp_lfs_t *) (c->context);

	ESP_LOGD(TAG, "%s - block=0x%08x", __func__, block);

	esp_err_t err = esp_partition_erase_range(efs->partition, block * efs->sector_sz, efs->sector_sz);
    if (err != ESP_OK ) {
        ESP_LOGE(TAG, "failed to erase addr %08x, size %08x, err %d", (block * efs->sector_sz), block, err);
        return LFS_ERR_IO;
    }
	return LFS_ERR_OK;
}

int lfs_api_sync(const struct lfs_config *c)
{
	ESP_LOGD(TAG, "%s", __func__);

	return LFS_ERR_OK;
}
