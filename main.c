#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>
#include <assert.h>

char *basepath;
char *deffile;

// Thanks to rofs-filtered for this function (https://github.com/gburca/rofs-filtered/blob/master/rofs-filtered.c)
static char* concat_path(const char* p1, const char* p2) {
    assert(p1 && p2);

    size_t p1len = strlen(p1);
    size_t p2len = strlen(p2);

    assert(p1len && p2len);

    // Need room for path separator and null terminator
    char *path = malloc(p1len + p2len + 2);
    if (!path) return NULL;

    strcpy(path, p1);

    if ((path[p1len - 1] != '/') && (p2[0] != '/')) {
        // Add a "/" if neither p1 nor p2 has it.
        strcat(path, "/");
    } else if (path[p1len - 1] == '/') {
        // If p1 ends in '/', we don't need it from p2
        while (p2[0] == '/') p2++;
    }

    strcat(path, p2);

    return path;
}

static char* translate_path(const char* path)
{
    return concat_path(basepath, path);
}

static int getattr_callback(const char *path, struct stat *stbuf)
{
  path = translate_path(path);
  if (path == NULL) {
    return -EINVAL;
  }
  
  memset(stbuf, 0, sizeof(struct stat));

  errno = 0;
  if (lstat(path, stbuf) != -1) {
    return 0;
  }

  if (errno != ENOENT) {
    return -errno;
  }

  errno = 0;
  if (lstat(deffile, stbuf) != -1) {
    return 0;
  }

  return -errno;
}

typedef struct deffs_handle {
  DIR *os_dirh;
  int os_fh;
} deffs_handle;

static deffs_handle* alloc_handle(struct fuse_file_info *fi)
{
  deffs_handle *hdl = malloc(sizeof hdl);
  hdl->os_dirh = NULL;
  hdl->os_fh = -1;
  fi->fh = (uint64_t)hdl;
  return hdl;
}

static void free_handle(struct fuse_file_info *fi)
{
  deffs_handle *hdl = (deffs_handle*)fi->fh;
  if (hdl == NULL) {
    return;
  }

  if (hdl->os_dirh != NULL) {
    closedir(hdl->os_dirh);
    hdl->os_dirh = NULL;
  }

  if (hdl->os_fh != -1) {
    close(hdl->os_fh);
    hdl->os_fh = -1;
  }

  fi->fh = 0;
  free(hdl);
}

static int opendir_callback(const char *path, struct fuse_file_info *fi)
{
  path = translate_path(path);
  if (path == NULL) {
    return -EINVAL;
  }

  errno = 0;
  DIR *fh = opendir(path);
  if (fh == NULL) {
    return -errno;
  }

  deffs_handle *hdl = alloc_handle(fi);
  hdl->os_dirh = fh;
  return 0;
}

static int releasedir_callback(const char *path, struct fuse_file_info *fi)
{
  free_handle(fi);
  return 0;
}

static int readdir_callback(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi)
{
  (void)offset;

  deffs_handle *hdl = (deffs_handle*)fi->fh;
  if (hdl == NULL || hdl->os_dirh == NULL) {
    return -EINVAL;
  }

  errno = 0;
  struct dirent *data = readdir(hdl->os_dirh);
  if (data == NULL) {
    return -errno;
  }

  filler(buf, data->d_name, NULL, data->d_off);
  return 0;
}

static int open_callback(const char *path, struct fuse_file_info *fi)
{
  path = translate_path(path);
  if (path == NULL) {
    return -EINVAL;
  }

  int flags = fi->flags;
  if ((flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_CREAT) || (flags & O_EXCL) || (flags & O_TRUNC)) {
      return -EPERM;
  }

  errno = 0;
  int fh = open(path, flags);
  if (fh == -1 && errno == ENOENT) {
    errno = 0;
    fh = open(deffile, fi->flags);
  }

  if (fh == -1) {
    return -errno;
  }

  deffs_handle *hdl = alloc_handle(fi);
  hdl->os_fh = fh;
  return 0;
}

static int release_callback(const char *path, struct fuse_file_info *fi)
{
  free_handle(fi);
  return 0;
}

static int read_callback(const char *path, char *buf, size_t size, off_t offset,
                         struct fuse_file_info *fi)
{
  deffs_handle *hdl = (deffs_handle*)fi->fh;
  if (hdl == NULL || hdl->os_fh == -1) {
    return -EINVAL;
  }

  return read(hdl->os_fh, buf, size);
}

static struct fuse_operations deffs_operations = {
    .getattr = getattr_callback,

    .open = open_callback,
    .release = release_callback,
    .read = read_callback,

    .opendir = opendir_callback,
    .releasedir = releasedir_callback,
    .readdir = readdir_callback,
};

// -odeffile=meow

static int deffs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
  if (key == FUSE_OPT_KEY_NONOPT && basepath == NULL) {
    basepath = strdup(arg);
    return 0;
  }
  if (key == FUSE_OPT_KEY_OPT && deffile == NULL) {
    int arglen = strlen(arg);
    if (arglen < 9) {
      return 1;
    }
    if (arg[0] != 'd' || arg[1] != 'e' || arg[2] != 'f' || arg[3] != 'f' || arg[4] != 'i' || arg[5] != 'l' || arg[6] != 'e' || arg[7] != '=') {
      return 1;
    }
    deffile = strdup(arg + 8);
    return 0;
  }
  return 1;
}

int main(int argc, char *argv[])
{
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  fuse_opt_parse(&args, NULL, NULL, deffs_opt_proc);
  return fuse_main(args.argc, args.argv, &deffs_operations, NULL);
}
