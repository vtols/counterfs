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
struct entry
{
    char *name, *buf;
    unsigned int c;
    entry *prev, *next;
};

entry *ehead, *etail;

/* Allocate copy string in heap */
char *
alloc_str(const char *s)
{
    char *t = (char *) malloc(strlen(s) + 1);
    strcpy(t, s);
    return t;
}

void
add_entry(const char *nm)
{
    entry *ent = (entry *) malloc(sizeof(entry));
    char *name = alloc_str(nm);
    ent->name = name;
    ent->buf = (char *) malloc(MAXLEN);
    ent->c = 0;
    ent->next = NULL;
    sprintf(ent->buf, "%d\n", 0);

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
        free(ent->buf);
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
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(ent->buf);
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

    if ((fi->flags & 3) != O_RDONLY)
        return -EACCES;

    if (has_subdir(path))
        return -ENOENT;

    if (!find_entry(path + 1, &ent))
        return -ENOENT;

    sprintf(ent->buf, "%d\n", ent->c++);

    return 0;
}

int
counter_read(const char *path, char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi)
{
    size_t len;
    entry *ent;
    (void) fi;
    if (!find_entry(path + 1, &ent))
        return -ENOENT;

    len = strlen(ent->buf);
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, ent->buf + offset, size);
    } else
        size = 0;

    return size;
}

int 
counter_create(const char *path, mode_t mod, struct fuse_file_info *fi)
{
    (void) fi;

    if (has_subdir(path))
        return -ENOENT;

    add_entry(path + 1);

    return 0;
}

/* Dummy function, sometimes necessary to create file */
int
counter_utimens(const char *path, const struct timespec tv[2])
{
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
    ent->name = alloc_str(to + 1);

    return 0;
}

struct fuse_operations counter_oper = {
    .getattr  = counter_getattr,
    .unlink   = counter_unlink,
    .rename   = counter_rename,
    .open     = counter_open,
    .read     = counter_read,
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
