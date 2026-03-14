#ifndef SH1106_IOCTL_H
#define SH1106_IOCTL_H

// Guard para usar el mismo header en kernel y userspace
#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

// Magic number único para este driver
#define SH1106_IOC_MAGIC 's'

// Comandos ioctl
#define SH1106_IOC_CLEAR            _IO(SH1106_IOC_MAGIC,  0)
#define SH1106_IOC_SET_CONTRAST     _IOW(SH1106_IOC_MAGIC, 1, int)
#define SH1106_IOC_INVERT           _IOW(SH1106_IOC_MAGIC, 2, int)
#define SH1106_IOC_FLIP_VERTICAL    _IOW(SH1106_IOC_MAGIC, 3, int)
#define SH1106_IOC_FLIP_HORIZONTAL  _IOW(SH1106_IOC_MAGIC, 4, int)
#define SH1106_IOC_POWER            _IOW(SH1106_IOC_MAGIC, 5, int)

#endif /* SH1106_IOCTL_H */
