#include "ulib.h"

long syscall0(long n) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n)
                      : "rcx", "r11", "memory");
    return ret;
}

long syscall1(long n, long a0) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a0)
                      : "rcx", "r11", "memory");
    return ret;
}

long syscall2(long n, long a0, long a1) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a0), "S"(a1)
                      : "rcx", "r11", "memory");
    return ret;
}

long syscall3(long n, long a0, long a1, long a2) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a0), "S"(a1), "d"(a2)
                      : "rcx", "r11", "memory");
    return ret;
}

long syscall4(long n, long a0, long a1, long a2, long a3) {
    long ret;
    register long r10 __asm__("r10") = a3;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a0), "S"(a1), "d"(a2), "r"(r10)
                      : "rcx", "r11", "memory");
    return ret;
}

long sys_read(int fd, void *buf, size_t count) {
    return syscall3(SYS_READ, fd, (long)buf, (long)count);
}

long sys_write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (long)buf, (long)count);
}

long sys_open(const char *path) {
    return syscall1(SYS_OPEN, (long)path);
}

long sys_close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

long sys_getpid(void) {
    return syscall0(SYS_GETPID);
}

long sys_fork(void) {
    return syscall0(SYS_FORK);
}

long sys_execve(const char *path, char *const argv[], char *const envp[]) {
    return syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp);
}

void sys_exit(int code) {
    syscall1(SYS_EXIT, code);
    for (;;) { }
}

long sys_waitpid(int pid, int *status, int options) {
    return syscall3(SYS_WAITPID, pid, (long)status, options);
}

long sys_sched_yield(void) {
    return syscall0(SYS_SCHED_YIELD);
}

long sys_readdir(const char *path, uint32_t idx, char name[64], uint32_t *type) {
    return syscall4(SYS_READDIR, (long)path, idx, (long)name, (long)type);
}

long sys_procinfo(uint32_t idx, task_info_t *info) {
    return syscall2(SYS_PROCINFO, idx, (long)info);
}

long sys_chdir(const char *path) {
    return syscall1(SYS_CHDIR, (long)path);
}

long sys_getcwd(char *buf, size_t size) {
    return syscall2(SYS_GETCWD, (long)buf, (long)size);
}

long sys_creat(const char *path) {
    return syscall1(SYS_CREAT, (long)path);
}

long sys_dup2(int oldfd, int newfd) {
    return syscall2(SYS_DUP2, oldfd, newfd);
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memset(void *dst, int value, size_t n) {
    unsigned char *d = dst;
    while (n--)
        *d++ = (unsigned char)value;
    return dst;
}

size_t u_strlen(const char *s) {
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

int u_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int u_strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

void u_write_str(int fd, const char *s) {
    sys_write(fd, s, u_strlen(s));
}

void u_write_char(int fd, char c) {
    sys_write(fd, &c, 1);
}

void u_write_uint(int fd, uint32_t value) {
    char buf[10];
    size_t n = 0;

    if (value == 0) {
        u_write_char(fd, '0');
        return;
    }

    while (value && n < sizeof(buf)) {
        buf[n++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (n > 0)
        u_write_char(fd, buf[--n]);
}
