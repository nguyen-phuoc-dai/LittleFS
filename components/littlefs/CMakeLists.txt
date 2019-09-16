set(COMPONENT_ADD_INCLUDEDIRS "include")
set(COMPONENT_PRIV_INCLUDEDIRS "." "littlefs")
set(COMPONENT_SRCS "littlefs/lfs_util.c"
				   "littlefs/lfs.c"
				   "esp_lfs.c"
				   "lfs_api.c")
				   
set(COMPONENT_REQUIRES spi_flash)
set(COMPONENT_PRIV_REQUIRES bootloader_support)

register_component()
