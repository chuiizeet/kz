#ifndef LITE
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "sys.h"
#include "hb.h"
#include "fat.h"

enum device_type{
    DEVICE_NONE,
    DEVICE_ED64,
    DEVICE_HOMEBOY,
};

typedef struct {
    int         descriptor;
    fat_path_t *fp;
    fat_file_t  file;
    int         flags;
} desc_t;

typedef struct {
    desc_t      descriptor;
    uint32_t    pos;
} file_desc_t;

typedef struct {
    desc_t      base;
    long        pos;
    dirent_t    dirent;
} dir_desc_t;

static fat_ctxt_t fat;
static desc_t *desc_list[FOPEN_MAX];

static fat_path_t *wd;

static int fat_ready = 0;
static enum device_type device_type;

time_t time(time_t *tloc)
{
    if (tloc){
        *tloc = 0;
    }
    return 0;
}

static int read_sd(void *buf, uint32_t lba, uint32_t block_cnt){
    switch(device_type){
        case DEVICE_HOMEBOY:
            if(hb_sd_read(buf,lba,block_cnt)){
                errno = EIO;
                return -1;
            }
            return 0;
        default:
            errno = ENODEV;
            return -1;
    }
}

static int write_sd(void *buf, uint32_t lba, uint32_t block_cnt){
    switch(device_type){
        case DEVICE_HOMEBOY:
            if(hb_sd_write(buf,lba,block_cnt)){
                errno = EIO;
                return -1;
            }
            return 0;
        default:
            errno = ENODEV;
            return -1;
    }
}

static int init_fat(){
    if(fat_ready){
        return 0;
    }
    if(hb_sd_init() == 0){
        device_type = DEVICE_HOMEBOY;
    }else{
        errno = ENODEV;
        return -1;
    }
    if(fat_init(&fat,read_sd, write_sd)){
        return -1;
    }
    wd = fat_path(&fat, NULL, "", NULL);
    if(!wd){
        return -1;
    }
    fat_ready = 1;
    return 0;
}

static fat_path_t *get_origin(const char *path, const char **tail){
    if(path[0] == '/' || path[0] == '\\'){
        if(tail){
            *tail = &path[1];
        }
        return NULL;
    }else{
        if(tail){
            *tail = path;
        }
        return wd;
    }
}

static ino_t make_sn(fat_entry_t *entry){
    if((entry->attributes & FAT_ATTRIBUTE_DIRECTORY) && entry->cluster < 2){
        return fat.partition_lba;
    }
    return fat.partition_lba + 1 + entry->last.cluster * (fat.bytes_per_cluster / 0x20) + entry->last.p_offset / 0x20;
}

static int check_path(ino_t sn, fat_path_t *dir){
    for(fat_entry_t *ent = dir->entry_list.first;ent;ent = list_next(ent)){
        if(sn == make_sn(ent)){
            errno = EACCES;
            return -1;
        }
    }
    return 0;
}

static void *new_desc(size_t size, fat_path_t *fp, int flags){
    for(int i = 0;i < FOPEN_MAX;i++){
        if(!desc_list[i]){
            desc_t *desc = malloc(size);
            if(!desc){
                errno = ENOMEM;
                return NULL;
            }
            desc->descriptor = i;
            desc->fp = fp;
            fat_begin(fat_path_target(fp),&desc->file);
            desc->flags = flags;
            desc_list[i] = desc;
            return desc;
        }
    }
    errno = EMFILE;
    return NULL;
}

static int entry_access(fat_entry_t *entry, _Bool write){
    ino_t sn = make_sn(entry);
    if(write && check_path(sn,wd)){
        return -1;
    }
    for(int i = 0;i < FOPEN_MAX;i++){
        desc_t *desc = desc_list[i];
        if(!desc){
            continue;
        }
        if(write & check_path(sn,desc->fp)){
            return -1;
        }
        if((desc->flags & _FWRITE) && sn == make_sn(fat_path_target(desc->fp))){
            errno=EACCES;
            return -1;
        }
    }
    return 0;
}

static int ent_mode(fat_entry_t *ent){
    mode_t mode;
    if(ent->attributes & FAT_ATTRIBUTE_DIRECTORY){
        mode = S_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH;
    }else{
        mode = S_IFREG;
    }
    if(!(ent->attributes & FAT_ATTRIBUTE_HIDDEN)){
        mode |= S_IRUSR | S_IRGRP | S_IROTH;
    }
    if(!(ent->attributes & FAT_ATTRIBUTE_READONLY)){
        mode |= S_IWUSR;
    }
    return mode;
}

static void delete_desc(int file){
    desc_t *desc = desc_list[file];
    fat_free(desc->fp);
    free(desc);
    desc_list[file] = NULL;
}

static void *get_desc(int file){
    if(file < 0 || file >= FOPEN_MAX){
        errno = EBADF;
        return NULL;
    }
    void *desc = desc_list[file];
    if(!desc){
        errno = EBADF;
        return NULL;
    }
    return desc;
}

static int seek_file(file_desc_t *fdesc){
    fat_file_t *file = &fdesc->descriptor.file;
    if(fdesc->pos == file->p_offset){
        return 0;
    }
    if(fdesc->pos < file->p_offset){
        fat_rewind(file);
    }
    int e = errno;
    errno = 0;
    fat_advance(file,fdesc->pos - file->p_offset,NULL);
    if(errno != 0){
        return -1;
    }
    errno = e;
    return 0;
}

int close(int file){
    file_desc_t *fdesc = get_desc(file);
    if(!fdesc){
        return -1;
    }
    delete_desc(fdesc->descriptor.descriptor);
    return fat_flush(&fat);
}

int creat(const char *path, mode_t mode){
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int open(const char *path, int open_flags, ...){
    if(init_fat()){
        return -1;
    }
    int flags = open_flags + 1;
    const char *tail;
    fat_path_t *origin = get_origin(path,&tail);
    int e = errno;
    errno = 0;
    fat_path_t *fp = fat_path(&fat, origin, tail, NULL);
    fat_entry_t *entry;
    if(errno == 0){
        entry = fat_path_target(fp);
        if((open_flags & O_CREAT) && (open_flags & O_EXCL)){
            errno = EEXIST;
            goto error;
        }
        if((entry->attributes & FAT_ATTRIBUTE_DIRECTORY) && (flags &_FWRITE)){
            errno = EISDIR;
            goto error;
        }
        if(entry_access(entry, flags & _FWRITE)){
            goto error;
        }
        errno = e;
    }
    else{
        if(errno == ENOENT && (open_flags & O_CREAT)){
            va_list va;
            va_start(va,open_flags);
            mode_t mode = va_arg(va, mode_t);
            va_end(va);
            uint8_t attributes = FAT_ATTRIBUTE_ARCHIVE;
            if(!(mode & S_IRUSR)){
                attributes |= FAT_ATTRIBUTE_HIDDEN;
            }
            if(!(mode & S_IWUSR)){
                attributes |= FAT_ATTRIBUTE_READONLY;
            }
            if(fp){
                fat_free(fp);
            }
            fp = fat_create_path(&fat, origin, tail, attributes);
            if(!fp){
                goto error;
            }
            entry = fat_path_target(fp);
            errno = e;
        }
        else{
            goto error;
        }
    }
    if((open_flags & O_TRUNC) && entry->size > 0){
        if(fat_resize(entry,0,NULL)){
            goto error;
        }
    }
    file_desc_t *desc = new_desc(sizeof(*desc),fp,flags);
    if(!desc){
        goto error;
    }
    desc->pos = 0;
    return desc->descriptor.descriptor;
error:
    if(fp){
        fat_free(fp);
    }
    return -1;
}

int write(int file, void *buf, uint32_t byte_cnt){
    file_desc_t *fdesc = get_desc(file);
    if(!fdesc){
        return -1;
    }
    desc_t *desc = &fdesc->descriptor;
    fat_entry_t *entry = fat_path_target(desc->fp);
    if(!(desc->flags & _FWRITE)){
        errno = EBADF;
        return -1;
    }
    if(desc->flags & _FAPPEND){
        fdesc->pos = desc->file.size;
    }
    if(fdesc->pos > desc->file.size){
        uint32_t size = desc->file.size;
        if(fat_resize(entry,fdesc->pos + byte_cnt, &desc->file)){
            return -1;
        }
        uint32_t advance = size - desc->file.p_offset;
        if(fat_advance(&desc->file,advance,NULL) != advance){
            return -1;
        }
        uint32_t cnt = fdesc->pos - size;
        if(fat_rw(&desc->file,FAT_WRITE,NULL,cnt,&desc->file,NULL) != cnt){
            return -1;
        }
    }else{
        uint32_t new_offset = fdesc->pos + byte_cnt;
        if(new_offset > desc->file.size){
            if(fat_resize(entry,new_offset,&desc->file)){
                return -1;
            }
        }
        if(seek_file(fdesc)){
            return -1;
        }
    }
    uint32_t cnt = fat_rw(&desc->file, FAT_WRITE, buf, byte_cnt, &desc->file, NULL);
    fdesc->pos += cnt;
    return cnt;
}

int read(int file, void *buf, uint32_t byte_cnt){
    file_desc_t *fd = get_desc(file);
    if(!fd){
        return -1;
    }
    if(!(fd->descriptor.flags & _FREAD)){
        errno = EBADF;
        return -1;
    }
    if(byte_cnt == 0){
        return 0;
    }
    if(seek_file(fd)){
        return -1;
    }
    uint32_t cnt = fat_rw(&fd->descriptor.file, FAT_READ,buf, byte_cnt,&fd->descriptor.file,NULL);
    fd->pos += cnt;
    return cnt;
}

static fat_path_t *wd_path(const char *path){
    const char *tail;
    fat_path_t *origin = get_origin(path,&tail);
    return fat_path(&fat,origin,tail,NULL);
}

DIR *opendir(const char *dir){
    if(init_fat()){
        return NULL;
    }
    int e = errno;
    errno = 0;
    fat_path_t *fp = wd_path(dir);
    if(errno == 0){
        errno = e;
    }
    else{
        goto error;
    }
    fat_entry_t *ent = fat_path_target(fp);
    if(!(ent->attributes & FAT_ATTRIBUTE_DIRECTORY)){
        errno = ENOTDIR;
        goto error;
    }
    if(entry_access(ent,0)){
        goto error;
    }
    dir_desc_t *dd = new_desc(sizeof(*dd), fp, _FREAD);
    if(!dd){
        goto error;
    }
    dd->pos = 0;
    return (void*)dd;
error:
    if(fp){
        fat_free(fp);
    }
    return NULL;
}

char *getcwd(char *buf, size_t size){
    if(init_fat()){
        return NULL;
    }
    if(size == 0){
        errno = EINVAL;
        return NULL;
    }
    char *p = buf;
    char *end = buf + size;
    for(fat_entry_t *ent = wd->entry_list.first;ent;ent = list_next(ent)){
        char *name = ent->long_name;
        while(*name && p!=end){
            *p++ = *name++;
        }
        if((ent == wd->entry_list.first || ent!= wd->entry_list.last) && p != end){
            *p++='/';
        }
        if(p == end){
            errno = ERANGE;
            return NULL;
        }
    }
    *p = 0;
    return buf;
}

int closedir(DIR *dir){
    dir_desc_t *dd = (void*)dir;
    delete_desc(dd->base.descriptor);
    return 0;
}

dirent_t *readdir(DIR *dir){
    dir_desc_t *dd = (void*)dir;
    fat_entry_t ent;
    do{
        if(fat_dir(&dd->base.file,&ent)){
            return NULL;
        } 
    }while(ent.attributes & FAT_ATTRIBUTE_LABEL);
    dd->pos++;
    dirent_t *dirent = &dd->dirent;
    dirent->dir_ino = make_sn(&ent);
    strcpy(dirent->dir_name,ent.long_name);
    dirent->mode = ent_mode(&ent);
    dirent->create_time = ent.create;
    dirent->modify_time = ent.modify_time;
    dirent->size = ent.size;
    return &dd->dirent;
}

int chdir(const char *path){
    if(init_fat()){
        return -1;
    }
    int e = errno;
    errno = 0;
    fat_path_t *fp = wd_path(path);
    if(errno == 0){
        if(!(fat_path_target(fp)->attributes & FAT_ATTRIBUTE_DIRECTORY)){
            errno = ENOTDIR;
            goto error;
        }else{
            errno = e;
        }
    }else{
        goto error;
    }
    fat_free(wd);
    wd = fp;
    return 0;
error:
    if(fp){
        fat_free(fp);
    }
    return -1;
}

void reset_disk(void){
    fat_ready = 0;
    for(int i = 0;i<FOPEN_MAX;i++){
        if(desc_list[i]){
            delete_desc(i);
        }
    }
    if(wd){
        fat_free(wd);
        wd = NULL;
    }
}

#endif