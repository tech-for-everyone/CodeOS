/* CodeOS filesystem shim — statfs/statvfs entry points gio needs at link time.
 * CodeOS's VFS does not expose filesystem statistics yet, so these fail.
 */
struct statfs;
struct statvfs;

__attribute__((weak)) int statfs(const char *path, struct statfs *buf)
{
    (void)path; (void)buf;
    return -1;
}

__attribute__((weak)) int statvfs(const char *path, struct statvfs *buf)
{
    (void)path; (void)buf;
    return -1;
}
