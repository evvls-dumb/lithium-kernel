#pragma once

#include <stddef.h>
#include <stdint.h>

#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_MMAP        9
#define SYS_MUNMAP      11
#define SYS_BRK         12
#define SYS_SCHED_YIELD 24
#define SYS_GETPID      39
#define SYS_FORK        57
#define SYS_EXECVE      59
#define SYS_EXIT        60
#define SYS_WAITPID     61
#define SYS_READDIR     78
#define SYS_PROCINFO    79
#define SYS_CHDIR       80
#define SYS_GETCWD      81
#define SYS_CREAT       82
#define SYS_DUP2        83

#define WNOHANG 1

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t state;
    uint32_t user_mode;
    uint64_t ticks;
    char     name[32];
} task_info_t;

long syscall0(long n);
long syscall1(long n, long a0);
long syscall2(long n, long a0, long a1);
long syscall3(long n, long a0, long a1, long a2);
long syscall4(long n, long a0, long a1, long a2, long a3);

long sys_read(int fd, void *buf, size_t count);
long sys_write(int fd, const void *buf, size_t count);
long sys_open(const char *path);
long sys_close(int fd);
long sys_getpid(void);
long sys_fork(void);
long sys_execve(const char *path, char *const argv[], char *const envp[]);
void sys_exit(int code) __attribute__((noreturn));
long sys_waitpid(int pid, int *status, int options);
long sys_sched_yield(void);
long sys_readdir(const char *path, uint32_t idx, char name[64], uint32_t *type);
long sys_procinfo(uint32_t idx, task_info_t *info);
long sys_chdir(const char *path);
long sys_getcwd(char *buf, size_t size);
long sys_creat(const char *path);
long sys_dup2(int oldfd, int newfd);

size_t u_strlen(const char *s);
int u_strcmp(const char *a, const char *b);
int u_strncmp(const char *a, const char *b, size_t n);
void u_write_str(int fd, const char *s);
void u_write_char(int fd, char c);
void u_write_uint(int fd, uint32_t value);
