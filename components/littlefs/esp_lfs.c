/* Copyright 2019 wecheer.io. All Rights Reserved.
 * @author Nguyen Phuoc Dai
 */

/**
 * @file esp_lfs.c
 * @brief x
 *
 * x
 */

#include "esp_lfs.h"
#include "lfs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_spi_flash.h"
#include "esp_image_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <unistd.h>
#include <dirent.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include "esp_vfs.h"
#include "esp_err.h"
#include "rom/spi_flash.h"
#include "lfs_api.h"

static const char* TAG = "LFS";

static esp_lfs_t *_efs[CONFIG_LFS_MAX_PARTITIONS];

static ssize_t write_p(void *ctx, int fd, const void *data, size_t size);
static off_t lseek_p(void *ctx, int fd, off_t size, int mode);
static ssize_t read_p(void *ctx, int fd, void *dst, size_t size);
static int open_p(void *ctx, const char *path, int flags, int mode);
static int close_p(void *ctx, int fd);
static int fstat_p(void *ctx, int fd, struct stat *st);
static int stat_p(void *ctx, const char *path, struct stat *st);
static int unlink_p(void *ctx, const char *path);
static int rename_p(void *ctx, const char *src, const char *dst);
static DIR *opendir_p(void *ctx, const char *name);
static struct dirent *readdir_p(void *ctx, DIR *pdir);
static int readdir_r_p(void *ctx, DIR *pdir, struct dirent *entry, struct dirent **out_dirent);
static long telldir_p(void *ctx, DIR *pdir);
static void seekdir_p(void *ctx, DIR *pdir, long offset);
static int closedir_p(void *ctx, DIR *pdir);
static int mkdir_p(void *ctx, const char *name, mode_t mode);
static int rmdir_p(void *ctx, const char *name);
static int fsync_p(void *ctx, int fd);

static int map_lfs_error(int res);
static esp_err_t esp_lfs_init(const esp_vfs_lfs_conf_t *conf);
static esp_err_t esp_lfs_by_label(const char *label, int *index);
static esp_err_t esp_lfs_get_empty(int *index);
static void esp_lfs_free(esp_lfs_t **efs);
static int get_free_fd(esp_lfs_t *efs);

static ssize_t write_p(void *ctx, int fd, const void *data, size_t size)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

	xSemaphoreTake(efs->lock, portMAX_DELAY);

    if (efs->fds[fd].file == NULL) {
    	xSemaphoreGive(efs->lock);
        errno = EBADF;
        return -1;
    }

    lfs_ssize_t written = lfs_file_write(efs->fs, efs->fds[fd].file, data, size);

	xSemaphoreGive(efs->lock);

    if (written < 0) {
        return map_lfs_error(written);
    }

    return written;
}

static off_t lseek_p(void *ctx, int fd, off_t size, int mode)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

    int lfs_mode = 0;

    if (mode == SEEK_SET) {
        lfs_mode = LFS_SEEK_SET;
    } else if (mode == SEEK_CUR) {
        lfs_mode = LFS_SEEK_CUR;
    } else if (mode == SEEK_END) {
        lfs_mode = LFS_SEEK_END;
    } else {
        errno = EINVAL;
        return -1;
    }

    xSemaphoreTake(efs->lock, portMAX_DELAY);

    if (efs->fds[fd].file == NULL) {
    	xSemaphoreGive(efs->lock);
        errno = EBADF;
        return -1;
    }

    lfs_soff_t pos = lfs_file_seek(efs->fs, efs->fds[fd].file, size, lfs_mode);

	if (pos >= 0) {
		pos = lfs_file_tell(efs->fs, efs->fds[fd].file);
	}

	xSemaphoreGive(efs->lock);

    if (pos < 0) {
        return map_lfs_error(pos);
    }

    return pos;
}

static ssize_t read_p(void *ctx, int fd, void *dst, size_t size)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

	xSemaphoreTake(efs->lock, portMAX_DELAY);

    if (efs->fds[fd].file == NULL) {
    	xSemaphoreGive(efs->lock);
        errno = EBADF;
        return -1;
    }

    lfs_ssize_t read = lfs_file_read(efs->fs, efs->fds[fd].file, dst, size);

    xSemaphoreGive(efs->lock);

    if (read < 0) {
        return map_lfs_error(read);
    }

    return read;
}

static int open_p(void *ctx, const char *path, int flags, int mode)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

    int lfs_flags = 0;

    if ((flags & O_ACCMODE) == O_RDONLY) {
        lfs_flags = LFS_O_RDONLY;
    } else if ((flags & O_ACCMODE) == O_WRONLY) {
        lfs_flags = LFS_O_WRONLY;
    } else if ((flags & O_ACCMODE) == O_RDWR) {
        lfs_flags = LFS_O_RDWR;
    }

    if (flags & O_CREAT) {
        lfs_flags |= LFS_O_CREAT;
    }

    if (flags & O_EXCL) {
        lfs_flags |= LFS_O_EXCL;
    }

    if (flags & O_TRUNC) {
        lfs_flags |= LFS_O_TRUNC;
    }

    if (flags & O_APPEND) {
        lfs_flags |= LFS_O_APPEND;
    }

    lfs_file_t *file = (lfs_file_t *) malloc(sizeof(lfs_file_t));
    if (file == NULL) {
        errno = ENOMEM;
        return -1;
    }

    char *file_name = strdup(path);
    if (file_name == NULL) {
        free(file);
        errno = ENOMEM;
        return -1;
    }

	xSemaphoreTake(efs->lock, portMAX_DELAY);

	int fd = get_free_fd(efs);
    if (fd == -1) {
    	xSemaphoreGive(efs->lock);
        free(file_name);
        free(file);
        errno = ENFILE;
        return -1;
    }

    int err = lfs_file_open(efs->fs, file, path, lfs_flags);
    if (err < 0) {
    	xSemaphoreGive(efs->lock);
        free(file_name);
        free(file);
        return map_lfs_error(err);
    }

    efs->fds[fd].file = file;
    efs->fds[fd].path = file_name;

    xSemaphoreGive(efs->lock);

    return fd;
}

static int close_p(void *ctx, int fd)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

	xSemaphoreTake(efs->lock, portMAX_DELAY);

    if (efs->fds[fd].file == NULL) {
    	xSemaphoreGive(efs->lock);
        errno = EBADF;
        return -1;
    }

    int err = lfs_file_close(efs->fs, efs->fds[fd].file);

    free(efs->fds[fd].path);
    free(efs->fds[fd].file);
    memset(&efs->fds[fd], 0L, sizeof(vfs_fd_t));

    xSemaphoreGive(efs->lock);

    return map_lfs_error(err);
}

static int fstat_p(void *ctx, int fd, struct stat *st)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

	xSemaphoreTake(efs->lock, portMAX_DELAY);

    if (efs->fds[fd].file == NULL) {
    	xSemaphoreGive(efs->lock);
        errno = EBADF;
        return -1;
    }

    struct lfs_info lfs_info;
    int err = lfs_stat(efs->fs, efs->fds[fd].path, &lfs_info);

    xSemaphoreGive(efs->lock);

    if (err < 0) {
        return map_lfs_error(err);
    }

    memset(st, 0L, sizeof(stat));
    st->st_size = lfs_info.size;
    if (lfs_info.type == LFS_TYPE_DIR) {
        st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
    } else {
        st->st_mode = S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO;
    }

    return 0;
}

static int stat_p(void *ctx, const char *path, struct stat *st)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

	xSemaphoreTake(efs->lock, portMAX_DELAY);

    struct lfs_info lfs_info;
    int err = lfs_stat(efs->fs, path, &lfs_info);

    xSemaphoreGive(efs->lock);

    if (err < 0) {
        return map_lfs_error(err);
    }

    memset(st, 0L, sizeof(stat));
    st->st_size = lfs_info.size;
    if (lfs_info.type == LFS_TYPE_DIR) {
        st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
    } else {
        st->st_mode = S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO;
    }

    return 0;
}

static int unlink_p(void *ctx, const char *path)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

	xSemaphoreTake(efs->lock, portMAX_DELAY);

	int err = lfs_remove(efs->fs, path);

	xSemaphoreGive(efs->lock);

	return map_lfs_error(err);
}

static int rename_p(void *ctx, const char *src, const char *dst)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

	xSemaphoreTake(efs->lock, portMAX_DELAY);

	int err = lfs_rename(efs->fs, src, dst);

	xSemaphoreGive(efs->lock);

	return map_lfs_error(err);
}

static DIR *opendir_p(void *ctx, const char *name)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

    vfs_lfs_dir_t *vfs_dir = (vfs_lfs_dir_t *) malloc(sizeof(vfs_lfs_dir_t));
    if (vfs_dir == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    memset(vfs_dir, 0L, sizeof(vfs_lfs_dir_t));

    xSemaphoreTake(efs->lock, portMAX_DELAY);

    int err = lfs_dir_open(efs->fs, &vfs_dir->lfs_dir, name);

    xSemaphoreGive(efs->lock);

    if (err != LFS_ERR_OK) {
        free(vfs_dir);
        vfs_dir = NULL;
        map_lfs_error(err);
    }

    return (DIR *) vfs_dir;
}

static struct dirent *readdir_p(void *ctx, DIR *pdir)
{
	vfs_lfs_dir_t *vfs_dir = (vfs_lfs_dir_t *) pdir;

    if (vfs_dir == NULL) {
        errno = EBADF;
        return NULL;
    }

    struct dirent *out_dirent = NULL;

    int err = readdir_r_p(ctx, pdir, &vfs_dir->dirent, &out_dirent);
    if (err != 0) {
        errno = err;
    }

    return out_dirent;
}

static int readdir_r_p(void *ctx, DIR *pdir, struct dirent *entry, struct dirent **out_dirent)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

    vfs_lfs_dir_t *vfs_dir = (vfs_lfs_dir_t *) pdir;
    if (vfs_dir == NULL) {
        errno = EBADF;
        return errno;
    }

    xSemaphoreTake(efs->lock, portMAX_DELAY);

    struct lfs_info lfs_info;
    int err = lfs_dir_read(efs->fs, &vfs_dir->lfs_dir, &lfs_info);

    xSemaphoreGive(efs->lock);

    if (err == 0) {
        *out_dirent = NULL;
        return 0;
    }

    if (err < 0) {
        map_lfs_error(err);
        return errno;
    }

    entry->d_ino = 0;
	if (lfs_info.type == LFS_TYPE_REG) {
		entry->d_type = DT_REG;
	} else if (lfs_info.type == LFS_TYPE_DIR) {
		entry->d_type = DT_DIR;
	} else {
		entry->d_type = DT_UNKNOWN;
	}
	size_t len = strlcpy(entry->d_name, lfs_info.name, sizeof(entry->d_name));

	// This "shouldn't" happen, but the LFS name length can be customized and may
	// be longer than what's provided in "struct dirent"
	if (len >= sizeof(entry->d_name)) {
		errno = ENAMETOOLONG;
		return errno;
	}

	vfs_dir->off++;

	*out_dirent = entry;

	return 0;
}

static long telldir_p(void *ctx, DIR *pdir)
{
    vfs_lfs_dir_t *vfs_dir = (vfs_lfs_dir_t *) pdir;

    if (vfs_dir == NULL) {
        errno = EBADF;
        return errno;
    }

    return vfs_dir->off;
}

static void seekdir_p(void *ctx, DIR *pdir, long offset)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

    vfs_lfs_dir_t *vfs_dir = (vfs_lfs_dir_t *) pdir;
    if (vfs_dir == NULL) {
        errno = EBADF;
        return;
    }

    xSemaphoreTake(efs->lock, portMAX_DELAY);

    // ESP32 VFS expects simple 0 to n counted directory offsets but lfs
    // doesn't so we need to "translate"...
    int err = lfs_dir_rewind(efs->fs, &vfs_dir->lfs_dir);
    if (err >= 0) {
        for (vfs_dir->off = 0; vfs_dir->off < offset; ++vfs_dir->off) {
            struct lfs_info lfs_info;
            err = lfs_dir_read(efs->fs, &vfs_dir->lfs_dir, &lfs_info);
            if (err < 0) {
                break;
            }
        }
    }

    xSemaphoreGive(efs->lock);

    if (err < 0) {
        map_lfs_error(err);
        return;
    }

    return;
}

static int closedir_p(void *ctx, DIR *pdir)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

    vfs_lfs_dir_t *vfs_dir = (vfs_lfs_dir_t *) pdir;
    if (vfs_dir == NULL) {
        errno = EBADF;
        return errno;
    }

    xSemaphoreTake(efs->lock, portMAX_DELAY);

    int err = lfs_dir_close(efs->fs, &vfs_dir->lfs_dir);

    xSemaphoreGive(efs->lock);

    free(vfs_dir);

    return map_lfs_error(err);
}

static int mkdir_p(void *ctx, const char *name, mode_t mode)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

	xSemaphoreTake(efs->lock, portMAX_DELAY);

	int err = lfs_mkdir(efs->fs, name);

	xSemaphoreGive(efs->lock);

	return map_lfs_error(err);
}

static int rmdir_p(void *ctx, const char *name)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

	xSemaphoreTake(efs->lock, portMAX_DELAY);

	int err = lfs_remove(efs->fs, name);

	xSemaphoreGive(efs->lock);

	return map_lfs_error(err);
}

static int fsync_p(void *ctx, int fd)
{
	esp_lfs_t *efs = (esp_lfs_t *)ctx;

	xSemaphoreTake(efs->lock, portMAX_DELAY);

    if (efs->fds[fd].file == NULL) {
    	xSemaphoreGive(efs->lock);
        errno = EBADF;
        return -1;
    }

    int err = lfs_file_sync(efs->fs, efs->fds[fd].file);

	xSemaphoreGive(efs->lock);

	return map_lfs_error(err);
}

static int map_lfs_error(int res)
{
    switch (res) {
    case LFS_ERR_OK:
        return 0;

    case LFS_ERR_IO:
    case LFS_ERR_CORRUPT:
        return EIO;

    case LFS_ERR_NOENT:
        return ENOENT;

    case LFS_ERR_EXIST:
        return EEXIST;

    case LFS_ERR_NOTDIR:
        return ENOTDIR;

    case LFS_ERR_ISDIR:
        return EISDIR;

    case LFS_ERR_NOTEMPTY:
        return ENOTEMPTY;

    case LFS_ERR_BADF:
        return EBADF;

    case LFS_ERR_NOMEM:
        return ENOMEM;

    case LFS_ERR_NOSPC:
        return ENOSPC;

    case LFS_ERR_INVAL:
        return EINVAL;

    default:
        return res;
    }
}

static esp_err_t esp_lfs_init(const esp_vfs_lfs_conf_t *conf)
{
	int index;

    // Find if such partition is already mounted
    if (esp_lfs_by_label(conf->partition_label, &index) == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    if (esp_lfs_get_empty(&index) != ESP_OK) {
        ESP_LOGE(TAG, "max mounted partitions reached");
        return ESP_ERR_INVALID_STATE;
    }

    // Find LittleFS in partition table by label
    const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, conf->partition_label);
    if (!partition) {
        ESP_LOGE(TAG, "littlefs partition could not be found");
        return ESP_ERR_NOT_FOUND;
    }

    if (partition->encrypted) {
        ESP_LOGE(TAG, "littlefs can not run on encrypted partition");
        return ESP_ERR_INVALID_STATE;
    }

    // Allocate data structuse for internal operations
    esp_lfs_t *efs = malloc(sizeof(esp_lfs_t));
    if (efs == NULL) {
        ESP_LOGE(TAG, "esp_lfs_t structure could not be malloced");
        return ESP_ERR_NO_MEM;
    }
    memset(efs, 0L, sizeof(esp_lfs_t));

    ESP_LOGD(TAG, "SPI_FLASH_SEC_SIZE=%d, partition size=0x%X", SPI_FLASH_SEC_SIZE, partition->size);

    // Configure callbacks for LittleFS filesystem
    efs->cfg.read  = lfs_api_read;
    efs->cfg.prog  = lfs_api_prog;
    efs->cfg.erase = lfs_api_erase;
    efs->cfg.sync  = lfs_api_sync;

    efs->sector_sz = SPI_FLASH_SEC_SIZE;
    efs->cfg.read_size   = 256;	//128;
    efs->cfg.prog_size   = 256;	//256;
    efs->cfg.cache_size  = 1024;
    efs->cfg.block_size  = efs->sector_sz;
    efs->cfg.block_count = partition->size / efs->cfg.block_size;
    efs->cfg.lookahead_size = 256;
    efs->cfg.block_cycles = 500;

    efs->by_label = conf->partition_label != NULL;

    efs->lock = xSemaphoreCreateMutex();
    if (efs->lock == NULL) {
        ESP_LOGE(TAG, "mutex lock could not be created");
        esp_lfs_free(&efs);
        return ESP_ERR_NO_MEM;
    }

    efs->max_files = conf->max_files;
    efs->fds = malloc(efs->max_files * sizeof(vfs_fd_t));
    if (efs->fds == NULL) {
        ESP_LOGE(TAG, "fd buffer could not be malloced");
        esp_lfs_free(&efs);
        return ESP_ERR_NO_MEM;
    }
    memset(efs->fds, 0L, efs->max_files * sizeof(vfs_fd_t));

    efs->fs = malloc(sizeof(lfs_t));
    if (efs->fs == NULL) {
        ESP_LOGE(TAG, "littlefs could not be malloced");
        esp_lfs_free(&efs);
        return ESP_ERR_NO_MEM;
    }
    memset(efs->fs, 0L, sizeof(lfs_t));

    efs->cfg.context = (void *)efs;
    efs->partition = partition;

    int err = lfs_mount(efs->fs, &efs->cfg);
    if (conf->format_if_mount_failed && err != LFS_ERR_OK) {
    	ESP_LOGW(TAG, "mount failed, %i. formatting...", err);

    	// Try to format the partition
    	ESP_LOGI(TAG, "lfs formatting ...");

    	memset(efs->fs, 0, sizeof(lfs_t));
    	err = lfs_format(efs->fs, &efs->cfg);
    	if (err != LFS_ERR_OK) {
            ESP_LOGE(TAG, "format lfs failed, %d", err);
            esp_lfs_free(&efs);
            return ESP_FAIL;
    	}

    	memset(efs->fs, 0, sizeof(lfs_t));
    	err = lfs_mount(efs->fs, &efs->cfg);
    }
    if (err != LFS_ERR_OK) {
    	ESP_LOGE(TAG, "mount lfs failed, %d", err);
        esp_lfs_free(&efs);
        return ESP_FAIL;
    }
    efs->mounted = true;
    _efs[index] = efs;
	return ESP_OK;
}

static esp_err_t esp_lfs_by_label(const char *label, int *index)
{
    int i;
    esp_lfs_t * p;

    // Scan through partions to find desirable one
    for (i = 0; i < CONFIG_LFS_MAX_PARTITIONS; i++) {
        p = _efs[i];
        if (p) {
            if (!label && !p->by_label) {
                *index = i;
                return ESP_OK;
            }
            if (label && p->by_label && strncmp(label, p->partition->label, 17) == 0) {
                *index = i;
                return ESP_OK;
            }
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t esp_lfs_get_empty(int *index)
{
    int i;

    for (i = 0; i < CONFIG_LFS_MAX_PARTITIONS; i++) {
        if (_efs[i] == NULL) {
            *index = i;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static void esp_lfs_free(esp_lfs_t **efs)
{
	esp_lfs_t *e = *efs;

	if (*efs == NULL) {
		return;
	}
	*efs = NULL;

	if (e->fs) {
		lfs_unmount(e->fs);
		free(e->fs);
	}
	vSemaphoreDelete(e->lock);
	free(e->fds);
	free(e);
}

static int get_free_fd(esp_lfs_t *efs)
{
    for (int i = 0; i < efs->max_files; i++) {
        if (efs->fds[i].file == NULL) {
            return i;
        }
    }
    return -1;
}

esp_err_t esp_vfs_lfs_register(const esp_vfs_lfs_conf_t *conf)
{
	int index;
	esp_err_t err;
	esp_vfs_t vfs = {};

	ESP_LOGD(TAG, "%s", __func__);

	assert(conf->base_path);

    err = esp_lfs_init(conf);
    if (err != ESP_OK) {
        return err;
    }

    if (esp_lfs_by_label(conf->partition_label, &index) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    vfs.flags = ESP_VFS_FLAG_CONTEXT_PTR;
    vfs.write_p = &write_p;
    vfs.lseek_p = &lseek_p;
    vfs.read_p = &read_p;
    vfs.open_p = &open_p;
    vfs.close_p = &close_p;
    vfs.fstat_p = &fstat_p;
    vfs.stat_p = &stat_p;
    vfs.link = NULL;
    vfs.unlink_p = &unlink_p;
    vfs.rename_p = &rename_p;
    vfs.opendir_p = &opendir_p;
    vfs.readdir_p = &readdir_p;
    vfs.readdir_r_p = &readdir_r_p;
    vfs.telldir_p = &telldir_p;
    vfs.seekdir_p = &seekdir_p;
    vfs.closedir_p = &closedir_p;
    vfs.mkdir_p = &mkdir_p;
    vfs.rmdir_p = &rmdir_p;
    vfs.fsync_p = &fsync_p;

    strlcat(_efs[index]->base_path, conf->base_path, ESP_VFS_PATH_MAX + 1);
    err = esp_vfs_register(conf->base_path, &vfs, _efs[index]);
    if (err != ESP_OK) {
        esp_lfs_free(&_efs[index]);
        return err;
    }

    return ESP_OK;
}

esp_err_t esp_vfs_lfs_unregister(const char* partition_label)
{
    int index;

    ESP_LOGD(TAG, "%s", __func__);

    if (esp_lfs_by_label(partition_label, &index) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_vfs_unregister(_efs[index]->base_path);
    if (err != ESP_OK) {
        return err;
    }

    esp_lfs_free(&_efs[index]);
    return ESP_OK;
}

bool esp_lfs_mounted(const char* partition_label)
{
    int index;

    ESP_LOGD(TAG, "%s", __func__);

    if (esp_lfs_by_label(partition_label, &index) != ESP_OK) {
        return false;
    }
    return _efs[index]->mounted;
}

esp_err_t esp_lfs_info(const char* partition_label, size_t *total_bytes, size_t *used_bytes)
{
	int index;

	ESP_LOGD(TAG, "%s", __func__);

    if (esp_lfs_by_label(partition_label, &index) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t allocated_blocks = lfs_fs_size(_efs[index]->fs);
    *total_bytes = _efs[index]->partition->size;
    *used_bytes = allocated_blocks * _efs[index]->cfg.block_size;

    return ESP_OK;
}

esp_err_t esp_lfs_format(const char* partition_label)
{
    esp_err_t err;
    int index;

    ESP_LOGD(TAG, "%s", __func__);

    err = esp_lfs_by_label(partition_label, &index);
    if (err != ESP_OK) {
    	// The partition is not mounted yet
    	return ESP_ERR_NOT_FOUND;
    }

    lfs_unmount(_efs[index]->fs);

    int res = lfs_format(_efs[index]->fs, &_efs[index]->cfg);
    if (res != LFS_ERR_OK) {
        ESP_LOGE(TAG, "format lfs failed, %d", res);
        // The partition was previously mounted, but format failed, don't
        // try to mount the partition back (it will probably fail).
    	return ESP_FAIL;
    }

	res = lfs_mount(_efs[index]->fs, &(_efs[index]->cfg));
	if (res != LFS_ERR_OK) {
		ESP_LOGE(TAG, "mount lfs failed, %d", res);
		return ESP_FAIL;
	}
	_efs[index]->mounted = true;

    return ESP_OK;
}
