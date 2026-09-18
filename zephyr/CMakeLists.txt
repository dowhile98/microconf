# Copyright (c) 2026
# SPDX-License-Identifier: MIT

if(CONFIG_MICROCONF)
  zephyr_library()

  zephyr_library_sources(
    ../src/mconf.c
  )

  zephyr_include_directories(
    ../include
    include
  )

  if(CONFIG_MICROCONF_BACKEND_EEPROM)
    zephyr_library_sources(src/mconf_zephyr_eeprom.c)
  endif()

  if(CONFIG_MICROCONF_BACKEND_FLASH)
    zephyr_library_sources(src/mconf_zephyr_flash.c)
  endif()

  if(CONFIG_MICROCONF_SHELL)
    zephyr_library_sources(src/mconf_zephyr_shell.c)
  endif()
endif()
