/* LittleFS filesystem example
*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lfs.h"

static const char *TAG = "example";

void app_main(void)
{
	ESP_LOGI(TAG, "Free heap: %u\n", xPortGetFreeHeapSize());

	ESP_LOGI(TAG, "Initializing LFS");

	esp_vfs_lfs_conf_t conf = {
			.base_path = "/lfs",
			.partition_label = "littlefs",
			.max_files = 5,
			.format_if_mount_failed = true
	};

	// Use settings defined above to initialize and mount LittleFS filesystem.
	// Note: esp_vfs_lfs_register is an all-in-one convenience function.
	esp_err_t res = esp_vfs_lfs_register(&conf);

	if (res != ESP_OK) {
		if (res == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount or format filesystem");
		} else if (res == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find LittleFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(res));
		}
		return;
	}

	ESP_LOGI(TAG, "Free heap: %u\n", xPortGetFreeHeapSize());

	size_t total = 0, used = 0;
	res = esp_lfs_info(conf.partition_label, &total, &used);
	if (res != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(res));
	} else {
		ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
		if (used > total) {
			ESP_LOGW(TAG, "Checking on LittleFS was really required!");
			while (1) {
				;
			}
		}
	}

	// Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(TAG, "Opening file /lfs/hello.txt");
    FILE* f = fopen("/lfs/hello.txt", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello World!\n");
    fclose(f);
    ESP_LOGI(TAG, "File written");

    // Check if destination file exists before renaming
    struct stat st;
    if (stat("/lfs/foo.txt", &st) == 0) {
        // Delete it if it exists
        unlink("/lfs/foo.txt");
    }

    // Rename original file
    ESP_LOGI(TAG, "Renaming file");
    if (rename("/lfs/hello.txt", "/lfs/foo.txt") != 0) {
        ESP_LOGE(TAG, "Rename failed");
        return;
    }

    // Open renamed file for reading
    ESP_LOGI(TAG, "Reading file");
    f = fopen("/lfs/foo.txt", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);
    // strip newline
    char* pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);

	res = esp_vfs_lfs_unregister(conf.partition_label);
	if (res == ESP_OK) {
		ESP_LOGI(TAG, "LittleFS was unregistered");
	} else {
		ESP_LOGI(TAG, "Failed to unregister LittleFS partition");
	}
}
