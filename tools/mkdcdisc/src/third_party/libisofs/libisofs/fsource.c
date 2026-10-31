/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2022 Thomas Schmitt
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "fsource.h"
#include <stdlib.h>

/**
 * Values belong 1000 are reserved for libisofs usage
 */
unsigned int iso_fs_global_id = 1000;

void iso_file_source_ref(IsoFileSource *src)
{
    ++src->refcount;
}

void iso_file_source_unref(IsoFileSource *src)
{
    if (--src->refcount == 0) {
        src->class->free(src);
        free(src);
    }
}

void iso_filesystem_ref(IsoFilesystem *fs)
{
    ++fs->refcount;
}

void iso_filesystem_unref(IsoFilesystem *fs)
{
    if (--fs->refcount == 0) {
        fs->free(fs);
        free(fs);
    }
}

/* 
 * this are just helpers to invoque methods in class
 */

inline
char* iso_file_source_get_path(IsoFileSource *src)
{
    return src->class->get_path(src);
}

inline
char* iso_file_source_get_name(IsoFileSource *src)
{
    return src->class->get_name(src);
}

inline
int iso_file_source_lstat(IsoFileSource *src, struct stat *info)
{
    return src->class->lstat(src, info);
}

inline
int iso_file_source_access(IsoFileSource *src)
{
    return src->class->access(src);
}

inline
int iso_file_source_stat(IsoFileSource *src, struct stat *info)
{
    return src->class->stat(src, info);
}

inline
int iso_file_source_open(IsoFileSource *src)
{
    return src->class->open(src);
}

inline
int iso_file_source_close(IsoFileSource *src)
{
    return src->class->close(src);
}

inline
int iso_file_source_read(IsoFileSource *src, void *buf, size_t count)
{
    return src->class->read(src, buf, count);
}

inline
off_t iso_file_source_lseek(IsoFileSource *src, off_t offset, int flag)
{
    return src->class->lseek(src, offset, flag);
}

inline
int iso_file_source_readdir(IsoFileSource *src, IsoFileSource **child)
{
    return src->class->readdir(src, child);
}

inline
int iso_file_source_readlink(IsoFileSource *src, char *buf, size_t bufsiz)
{
    return src->class->readlink(src, buf, bufsiz);
}

inline
IsoFilesystem* iso_file_source_get_filesystem(IsoFileSource *src)
{
    return src->class->get_filesystem(src);
}


inline
int iso_file_source_get_aa_string(IsoFileSource *src,
                                  unsigned char **aa_string, int flag)
{
    if (src->class->version < 1) {
        *aa_string = NULL;
        return 1;
    }
    return src->class->get_aa_string(src, aa_string, flag);
}


/* @flag  bit0= Open and close src
          bit1= Try iso_file_source_lseek(, 0) (=SEEK_SET) with wanted_size
   @return <0 iso_file_source_lseek failed , >= 0 readable capacity
*/
off_t iso_file_source_lseek_capacity(IsoFileSource *src, off_t wanted_size,
                                     int flag)
{
    int ret, opened = 0;
    off_t end, old, reset;
    struct stat info;

    ret = iso_file_source_stat(src, &info);
    if (ret < 0) {
        end = -1;
        goto ex;
    }
    if (S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode) || 
        S_ISFIFO(info.st_mode) || S_ISSOCK(info.st_mode)) {
        /* open(2) on fifo can block and have side effects.
           Active Unix sockets have not been tested but they make as few sense
           as directories or symbolic links.
        */
        end = -1;
        goto ex;
    }

    if (flag & 1) {
        ret = iso_file_source_open(src);
        if (ret < 0) {
            end = -1;
            goto ex;
        }
        opened = 1;
    }
    old = iso_file_source_lseek(src, 0, 1);
    if (old < 0) {
        end = -1;
        goto ex;
    }
    if(flag & 2) {
        end = iso_file_source_lseek(src, wanted_size, 0);
    } else {
        end = iso_file_source_lseek(src, 0, 2);
    }
    if (end < 0) {
        end = -1;
        goto ex;
    }
    reset = iso_file_source_lseek(src, old, 0);
    if (reset != old) {
        end = -1;
        goto ex;
    }
ex:;
    if (opened) {
        iso_file_source_close(src);
    }
    return end;
}


/* Determine whether src is random-access readable and return its capacity.
   @flag bit0= For iso_file_source_lseek_capacity(): Open and close src
         bit1= wanted_size is valid
*/
off_t iso_file_source_determine_capacity(IsoFileSource *src, off_t wanted_size,
                                         int flag)
{
    int ret;
    off_t src_size, src_seek_size= -1;
    struct stat info;

    ret = iso_file_source_stat(src, &info);
    if (ret < 0) {
        return (off_t) -1;
    }
    if (S_ISREG(info.st_mode)) {
        return info.st_size;
    }
    src_size = iso_file_source_lseek_capacity(src, wanted_size, (flag & 1));
    if (src_size > 0) {
        return src_size;
    }
    if (!(flag & 2)) {
        if (src_size == 0) {
            return 0;
        }
        return -1;
    }
    src_seek_size= src_size;

    src_size = iso_file_source_lseek_capacity(src, wanted_size,
                                              2 | (flag & 1));
    if (src_size >= 0) {
        return src_size;
    } else if (src_seek_size >= 0) {
        return src_seek_size;
    }
    return -1;
}
