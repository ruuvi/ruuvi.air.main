if(SB_CONFIG_BOOTLOADER_MCUBOOT)
	ExternalZephyrProject_Add(
        	APPLICATION firmware_loader
        	SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/fw_loader
	)

	set(mcuboot_EXTRA_ZEPHYR_MODULES "${CMAKE_CURRENT_LIST_DIR}/mcuboot_hook" CACHE INTERNAL "mcuboot_hook directory")
endif()

set(b0_EXTRA_ZEPHYR_MODULES "${CMAKE_CURRENT_LIST_DIR}/b0_hook" CACHE INTERNAL "b0_hook directory")
