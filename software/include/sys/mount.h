/* sys/mount.h -- P32W Unix: mounting filesystems (root only) */
#ifndef _SYS_MOUNT_H
#define _SYS_MOUNT_H
int mount(const char *special, const char *dir, int flags);   /* a block device's filesystem on dir */
int umount(const char *special);
#endif
