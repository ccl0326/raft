#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../src/aio.h"

#include "fs.h"

#define TEST_DIR_TEMPLATE "./tmp/%s/raft-test-XXXXXX"

char *test_dir_fs_type_supported[] = {"tmpfs", "ext4",
#if defined(RAFT_ENABLE_BTRFS_TESTS)
                                      "btrfs",
#endif
#if defined(RAFT_ENABLE_XFS_TESTS)
                                      "xfs",
#endif
#if defined(RAFT_ENABLE_ZFS_TESTS)
                                      "zfs",
#endif
                                      NULL};

char *test_dir_fs_type_btrfs[] = {"btrfs", NULL};

char *test_dir_fs_type_aio[] = {
#if defined(RAFT_ENABLE_BTRFS_TESTS)
    "btrfs",
#endif
    "ext4",
#if defined(RAFT_ENABLE_XFS_TESTS)
    "xfs",
#endif
    NULL};

char *test_dir_fs_type_no_aio[] = {"tmpfs",
#if defined(RAFT_ENABLE_ZFS_TESTS)

                                   "zfs",
#endif
                                   NULL};

MunitParameterEnum dir_fs_btrfs_params[] = {
    {TEST_DIR_FS_TYPE, test_dir_fs_type_btrfs},
    {NULL, NULL},
};

MunitParameterEnum dir_fs_supported_params[] = {
    {TEST_DIR_FS_TYPE, test_dir_fs_type_supported},
    {NULL, NULL},
};

MunitParameterEnum dir_fs_aio_params[] = {
    {TEST_DIR_FS_TYPE, test_dir_fs_type_aio},
    {NULL, NULL},
};

MunitParameterEnum dir_fs_no_aio_params[] = {
    {TEST_DIR_FS_TYPE, test_dir_fs_type_no_aio},
    {NULL, NULL},
};

char *test_dir_setup(const MunitParameter params[])
{
    const char *fs_type = munit_parameters_get(params, TEST_DIR_FS_TYPE);
    char *dir;

    if (fs_type == NULL) {
        fs_type = "tmpfs";
    }

    dir = munit_malloc(strlen(TEST_DIR_TEMPLATE) + strlen(fs_type) + 1);

    sprintf(dir, TEST_DIR_TEMPLATE, fs_type);

    if (mkdtemp(dir) == NULL) {
        munit_error(strerror(errno));
    }

    return dir;
}

static int test_dir__remove(const char *path,
                            const struct stat *sbuf,
                            int type,
                            struct FTW *ftwb)
{
    (void)sbuf;
    (void)type;
    (void)ftwb;

    return remove(path);
}

void test_dir_tear_down(char *dir)
{
    int rv;

    rv = chmod(dir, 0755);
    munit_assert_int(rv, ==, 0);

    rv = nftw(dir, test_dir__remove, 10, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
    munit_assert_int(rv, ==, 0);

    free(dir);
}

/**
 * Join the given @dir and @filename into @path.
 */
static void test_dir__join(const char *dir, const char *filename, char *path)
{
    strcpy(path, dir);
    strcat(path, "/");
    strcat(path, filename);
}

void test_dir_write_file(const char *dir,
                         const char *filename,
                         const void *buf,
                         const size_t n)
{
    char path[256];
    int fd;
    int rv;

    test_dir__join(dir, filename, path);

    fd = open(path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    munit_assert_int(fd, !=, -1);

    rv = write(fd, buf, n);
    munit_assert_int(rv, ==, n);

    close(fd);
}

void test_dir_write_file_with_zeros(const char *dir,
                                    const char *filename,
                                    const size_t n)
{
    void *buf = munit_malloc(n);

    test_dir_write_file(dir, filename, buf, n);

    free(buf);
}

void test_dir_append_file(const char *dir,
                          const char *filename,
                          const void *buf,
                          const size_t n)
{
    char path[256];
    int fd;
    int rv;

    test_dir__join(dir, filename, path);

    fd = open(path, O_APPEND | O_RDWR, S_IRUSR | S_IWUSR);

    munit_assert_int(fd, !=, -1);

    rv = write(fd, buf, n);
    munit_assert_int(rv, ==, n);

    close(fd);
}

void test_dir_overwrite_file(const char *dir,
                             const char *filename,
                             const void *buf,
                             const size_t n,
                             const off_t whence)
{
    char path[256];
    int fd;
    int rv;
    off_t size;

    test_dir__join(dir, filename, path);

    fd = open(path, O_RDWR, S_IRUSR | S_IWUSR);

    munit_assert_int(fd, !=, -1);

    /* Get the size of the file */
    size = lseek(fd, 0, SEEK_END);

    if (whence == 0) {
        munit_assert_int(size, >=, n);
        lseek(fd, 0, SEEK_SET);
    } else if (whence > 0) {
        munit_assert_int(whence, <=, size);
        munit_assert_int(size - whence, >=, n);
        lseek(fd, whence, SEEK_SET);
    } else {
        munit_assert_int(-whence, <=, size);
        munit_assert_int(-whence, >=, n);
        lseek(fd, whence, SEEK_END);
    }

    rv = write(fd, buf, n);
    munit_assert_int(rv, ==, n);

    close(fd);
}

void test_dir_overwrite_file_with_zeros(const char *dir,
                                        const char *filename,
                                        const size_t n,
                                        const off_t whence)
{
    void *buf;

    buf = munit_malloc(n);
    memset(buf, 0, n);

    test_dir_overwrite_file(dir, filename, buf, n, whence);

    free(buf);
}

void test_dir_truncate_file(const char *dir,
                            const char *filename,
                            const size_t n)
{
    char path[256];
    int fd;
    int rv;

    test_dir__join(dir, filename, path);

    fd = open(path, O_RDWR, S_IRUSR | S_IWUSR);

    munit_assert_int(fd, !=, -1);

    rv = ftruncate(fd, n);
    munit_assert_int(rv, ==, 0);

    close(fd);
}

void test_dir_read_file(const char *dir,
                        const char *filename,
                        void *buf,
                        const size_t n)
{
    char path[256];
    int fd;
    int rv;

    test_dir__join(dir, filename, path);

    fd = open(path, O_RDONLY);
    if (fd == -1) {
        munit_logf(MUNIT_LOG_ERROR, "read file '%s': %s", path,
                   strerror(errno));
    }

    rv = read(fd, buf, n);
    munit_assert_int(rv, ==, n);

    close(fd);
}

void test_dir_unexecutable(const char *dir)
{
    int rv;

    rv = chmod(dir, 0);
    munit_assert_int(rv, ==, 0);
}

void test_dir_unreadable_file(const char *dir, const char *filename)
{
    char path[256];
    int rv;

    test_dir__join(dir, filename, path);

    rv = chmod(path, 0);
    munit_assert_int(rv, ==, 0);
}

bool test_dir_has_file(const char *dir, const char *filename)
{
    char path[256];
    int fd;

    test_dir__join(dir, filename, path);

    fd = open(path, O_RDONLY);
    if (fd == -1) {
        munit_assert_int(errno, ==, ENOENT);
        return false;
    }

    close(fd);

    return true;
}

void test_dir_fill(const char *dir, const size_t n)
{
    char path[256];
    const char *filename = ".fill";
    struct statvfs fs;
    size_t size;
    int fd;
    int rv;

    rv = statvfs(dir, &fs);
    munit_assert_int(rv, ==, 0);

    size = fs.f_bsize * fs.f_bavail;

    if (n > 0) {
        munit_assert_int(size, >=, n);
    }

    test_dir__join(dir, filename, path);

    fd = open(path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    munit_assert_int(fd, !=, -1);

    rv = posix_fallocate(fd, 0, size);
    munit_assert_int(rv, ==, 0);

    /* If n is zero, make sure any further write fails with ENOSPC */
    if (n == 0) {
        char buf[4096];
        int i;

        rv = lseek(fd, 0, SEEK_END);
        munit_assert_int(rv, !=, -1);

        for (i = 0; i < 40; i++) {
            rv = write(fd, buf, sizeof buf);
            if (rv < 0) {
                break;
            }
        }

        munit_assert_int(rv, ==, -1);
        munit_assert_int(errno, ==, ENOSPC);
    }

    close(fd);
}

void test_aio_fill(aio_context_t *ctx, unsigned n)
{
    char buf[256];
    int fd;
    int rv;
    int limit;
    int used;

    /* Figure out how many events are available. */
    fd = open("/proc/sys/fs/aio-max-nr", O_RDONLY);
    munit_assert_int(fd, !=, -1);

    rv = read(fd, buf, sizeof buf);
    munit_assert_int(rv, !=, -1);

    close(fd);

    limit = atoi(buf);

    /* Figure out how many events are in use. */
    fd = open("/proc/sys/fs/aio-nr", O_RDONLY);
    munit_assert_int(fd, !=, -1);

    rv = read(fd, buf, sizeof buf);
    munit_assert_int(rv, !=, -1);

    close(fd);

    used = atoi(buf);

    rv = io_setup(limit - used - n, ctx);
    munit_assert_int(rv, ==, 0);
}

void test_aio_destroy(aio_context_t ctx)
{
    int rv;

    rv = io_destroy(ctx);
    munit_assert_int(rv, ==, 0);
}
