/*
 * Authoritative microconf build configuration.
 *
 * Install/build systems may regenerate this file, but consumers must not
 * override these values with conflicting preprocessor definitions.
 */

#ifndef MCONF_CONFIG_H
#define MCONF_CONFIG_H

#if defined(CONFIG_MICROCONF) || defined(__ZEPHYR__)
#if defined(__has_include)
#if __has_include(<zephyr/autoconf.h>)
#include <zephyr/autoconf.h>
#endif
#endif

#ifdef CONFIG_MICROCONF_MAX_ENTRIES
#define MCONF_CONFIG_MAX_ENTRIES CONFIG_MICROCONF_MAX_ENTRIES
#endif

#ifdef CONFIG_MICROCONF_MAX_KEY_LEN
#define MCONF_CONFIG_MAX_KEY_LEN CONFIG_MICROCONF_MAX_KEY_LEN
#endif

#ifdef CONFIG_MICROCONF_ENABLE_NAMES
#define MCONF_CONFIG_ENABLE_NAMES 1
#else
#define MCONF_CONFIG_ENABLE_NAMES 0
#endif

#ifdef CONFIG_MICROCONF_ENABLE_FLOAT
#define MCONF_CONFIG_ENABLE_FLOAT 1
#else
#define MCONF_CONFIG_ENABLE_FLOAT 0
#endif
#endif /* CONFIG_MICROCONF || __ZEPHYR__ */

#ifndef MCONF_CONFIG_MAX_ENTRIES
#define MCONF_CONFIG_MAX_ENTRIES 64
#endif

#ifndef MCONF_CONFIG_MAX_KEY_LEN
#define MCONF_CONFIG_MAX_KEY_LEN 48
#endif

#ifndef MCONF_CONFIG_ENABLE_NAMES
#define MCONF_CONFIG_ENABLE_NAMES 1
#endif

#ifndef MCONF_CONFIG_ENABLE_FLOAT
#define MCONF_CONFIG_ENABLE_FLOAT 1
#endif

#endif
