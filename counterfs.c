#define FUSE_USE_VERSION  26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>

/* Maximum buffer length (number and newline) */
#define MAXLEN 12

typedef struct entry entry;
typedef struct entry_data entry_data;
typedef struct entry_rw_data entry_rw_data;

struct entry
{
    char *name;
    entry_data *data;
    entry *prev, *next;
};

struct entry_data
{
    size_t size;
    unsigned int c;
    time_t atime, mtime;
    nlink_t nlink;
};

struct entry_rw_data
{
    char *rbuf, *wbuf;
    size_t wsize;
};

entry *ehead, *etail;

void
add_entry(const char *nm, entry_data *data)
{
    entry *ent = (entry *) malloc(sizeof(entry));
    char *name = strdup(nm);
    ent->name = name;
    ent->prev = ent->next = NULL;
    if (data) {
        ent->data = data;
        data->nlink++;
    } else {
        ent->data = (entry_data *) malloc(sizeof(entry_data));
        ent->data->nlink = 1;
        ent->data->size = 2;
        ent->data->c = 0;
    }

    if (!ehead)
        ehead = etail = ent;
    else {
        etail->next = ent;
        ent->prev = etail;
        etail = ent;
    }
}

int
find_entry(const char *nm, entry **ent)
{
    entry *cur = ehead;
    *ent = NULL;

    while (cur) {
        if (strcmp(nm, cur->name) == 0) {
            *ent = cur;
            return 1;
        }
        cur = cur->next;
    }
    return 0;
}

int
remove_entry(const char *nm)
{
    entry *ent;
    if (find_entry(nm, &ent)) {
        if (ent->prev)
            ent->prev->next = ent->next;
        else
            ehead = ent->next;
        if (ent->next)
            ent->next->prev = ent->prev;
        else
            etail = ent->prev;
        ent->data->nlink--;
        if (!ent->data->nlink)
            free(ent->data);
        free(ent->name);
        free(ent);
        return 1;
    }
    return 0;
}

/* Check that path has subdirectories */
int
has_subdir(const char *path)
{
    return strchr(path + 1, '/') != NULL;
}

int
counter_getattr(const char *path, struct stat *stbuf)
{
    entry *ent;
    int res = 0;
    memset(stbuf, 0, sizeof(struct stat));

    if (!has_subdir(path + 1) && find_entry(path + 1, &ent)) {
        stbuf->st_mode = S_IFREG | 0666;
        stbuf->st_nlink = ent->data->nlink;
        stbuf->st_size = ent->data->size;
        stbuf->st_atime = ent->data->atime;
        stbuf->st_mtime = ent->data->mtime;
    }
    else if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    else
        res = -ENOENT;

    return res;
}

int
counter_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi)
{
    entry *cur;

    (void) offset;
    (void) fi;

    if(strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    cur = ehead;
    while (cur) {
        filler(buf, cur->name, NULL, 0);
        cur = cur->next;
    }

    return 0;
}

int
counter_open(const char *path, struct fuse_file_info *fi)
{
    entry *ent;
    entry_rw_data *rw;

    if (has_subdir(path))
        return -ENOENT;

    if (!find_entry(path + 1, &ent))
        return -ENOENT;

    rw = (entry_rw_data *) malloc(sizeof(entry_rw_data));
    rw->rbuf  = (char *) malloc(MAXLEN);
    rw->wbuf  = (char *) malloc(MAXLEN);
    rw->wsize = 0;
    memset(rw->wbuf, 0, MAXLEN);
    fi->direct_io = 1;
    fi->fh = (uint64_t) rw;

    sprintf(rw->rbuf, "%u\n", ent->data->c);
    ent->data->size = strlen(rw->rbuf);
    ent->data->c++;

    return 0;
}

int
counter_truncate(const char *path, off_t offset)
{
    entry *ent;
    if (has_subdir(path))
        return -ENOENT;

    if (!find_entry(path + 1, &ent))
        return -ENOENT;
        
    return 0;
}

int
counter_read(const char *path, char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi)
{
    size_t len;
    entry_rw_data *rw = (entry_rw_data *) fi->fh;

    len = strlen(rw->rbuf);
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, rw->rbuf + offset, size);
        return size;
    }
    
    return 0;
}

int
counter_write(const char *path, const char *buf, size_t size,
              off_t offset, struct fuse_file_info *fi)
{
    entry_rw_data *rw = (entry_rw_data *) fi->fh;

    if (rw && offset < MAXLEN) {
        if (offset + size > MAXLEN - 1)
            size = MAXLEN - offset - 1;
        if (offset + size > rw->wsize)
            rw->wsize = offset + size;
        memcpy(rw->wbuf + offset, buf, size);
        return size;
    }
    
    return 0;
}

int 
counter_create(const char *path, mode_t mod, struct fuse_file_info *fi)
{
    (void) fi;

    if (has_subdir(path))
        return -ENOENT;

    add_entry(path + 1, NULL);

    return 0;
}

int
counter_utimens(const char *path, const struct timespec tv[2])
{
    entry *ent;

    if (has_subdir(path))
        return -ENOENT;

    if (!find_entry(path + 1, &ent))
        return -ENOENT;

    ent->data->atime = tv[0].tv_sec;
    ent->data->mtime = tv[1].tv_sec;

    return 0;
}

int
counter_unlink(const char *path)
{
    if (has_subdir(path))
        return -ENOENT;

    if (!remove_entry(path + 1))
        return -ENOENT;

    return 0;
}

int
counter_rename(const char *from, const char *to)
{
    entry *ent;

    if (has_subdir(from))
        return -ENOENT;

    if (!find_entry(from + 1, &ent))
        return -ENOENT;

    remove_entry(to + 1);
    free(ent->name);
    ent->name = strdup(to + 1);

    return 0;
}

int
counter_flush(const char *path, struct fuse_file_info *fi)
{
    entry *ent;
    unsigned int new_c = 0;
    int size;
    entry_rw_data *rw = (entry_rw_data *) fi->fh;
    printf("Flushing\n");

    if (has_subdir(path))
        return -ENOENT;

    if (!find_entry(path + 1, &ent))
        return -ENOENT;
    
    if (rw && rw->wsize) {
        sscanf(rw->wbuf, "%u", &new_c);
        
        ent->data->c = new_c;
        sprintf(rw->wbuf, "%u%n", new_c, &size);
        ent->data->size = size;
    }
    
    return 0;
}

int
counter_release(const char *path, struct fuse_file_info *fi)
{
    if (fi->fh) {
        entry_rw_data *rw = (entry_rw_data *) fi->fh;
        free(rw->rbuf);
        free(rw->wbuf);
        free(rw);
    }

    return 0;
}

int
counter_link(const char *from, const char *to)
{
    entry *ent;

    if (has_subdir(from))
        return -ENOENT;

    if (!find_entry(from + 1, &ent))
        return -ENOENT;

    add_entry(to + 1, ent->data);

    return 0;
}

struct fuse_operations counter_oper = {
    .getattr  = counter_getattr,
    .unlink   = counter_unlink,
    .rename   = counter_rename,
    .link     = counter_link,
    .truncate = counter_truncate,
    .open     = counter_open,
    .read     = counter_read,
    .write    = counter_write,
    .flush    = counter_flush,
    .release  = counter_release,
    .readdir  = counter_readdir,
    .create   = counter_create,
    .utimens  = counter_utimens,
};

int
main(int argc, char *argv[])
{
    ehead = etail = NULL;
    return fuse_main(argc, argv, &counter_oper, NULL);
}
