/**
 * @file vfs_mock.h
 * @brief VFS path translation for simulator
 *
 * When __SIMULATOR_BUILD__ is defined, this file provides wrapper functions
 * that translate ESP-IDF VFS paths to simulator filesystem paths.
 *
 * Example: /sdcard/firmwares -> sdcard/firmwares
 */

#ifndef VFS_MOCK_H
#define VFS_MOCK_H

#ifdef __SIMULATOR_BUILD__
    #include <sys/stat.h>
    #include <dirent.h>
    #include <stdio.h>
    #include <unistd.h>
    #include <fcntl.h>

    #ifdef __cplusplus
    extern "C" {
    #endif

    /**
     * @brief Translate ESP-IDF VFS path to simulator filesystem path
     * @param esp_path ESP-IDF style path (e.g., "/sdcard/firmwares")
     * @return Simulator filesystem path (e.g., "sdcard/firmwares")
     *
     * Note: Returns pointer to static buffer, do not free
     */
    const char* vfs_translate_path(const char* esp_path);

    // NOTE: To use path translation, include this header AFTER all system headers
    // The following macros will redefine stat/opendir/fopen/access to use path translation
    // Only include this in .c files that actually need file system access

    // Undefine standard function macros to prepare for redefinition
    #undef stat
    #undef opendir
    #undef fopen
    #undef access

    // Redefine as wrapper macros
    #define stat(path, st)    (stat)(vfs_translate_path(path), st)
    #define opendir(path)    (opendir)(vfs_translate_path(path))
    #define fopen(path, m)   (fopen)(vfs_translate_path(path), m)
    #define access(path, m)  (access)(vfs_translate_path(path), m)

    #ifdef __cplusplus
    }
    #endif

#endif // __SIMULATOR_BUILD__

#endif // VFS_MOCK_H
