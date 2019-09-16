# Integrate LittleFS into ESP32-IDF and make it as backend of VFS framework
LittleFS is a port of [LittleFS](https://github.com/geky/littlefs) to the ESP32-IDF and it can function as backend of VFS framework

To use this component as official component of ESP32-IDF, we need to modify two files of ESP32-IDF SDK

First, add "lfs": 0x83 to SUBTYPES in gen_esp32part.py file (it was located in ESP_IDF_PATH/components/partition_table)

```
SUBTYPES = {
    APP_TYPE: {
        "factory": 0x00,
        "test": 0x20,
    },
    DATA_TYPE: {
        ...
		"spiffs": 0x82,
		"lfs": 0x83,
    },
}

```

Second, add ESP_PARTITION_SUBTYPE_DATA_LFS = 0x83 to 'esp_partition_subtype_t' enum in file esp_partition.h (it was located in ESP_IDF_PATH/components/spi_flash/include)

```
/**
 * @brief Partition subtype
 * @note Keep this enum in sync with PartitionDefinition class gen_esp32part.py
 */
typedef enum {
	...
	ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x82,                                 //!< SPIFFS partition
	ESP_PARTITION_SUBTYPE_DATA_LFS = 0x83,									  //!< LITTLEFS partition
	...
} esp_partition_subtype_t;
```
