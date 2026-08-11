#pragma once

#include "../../include/types.h"

#define SYS_READ     0
#define SYS_WRITE    1
#define SYS_OPEN     2
#define SYS_CLOSE    3
#define SYS_MMAP     9
#define SYS_MUNMAP   11
#define SYS_BRK      12
#define SYS_SCHED_YIELD 24
#define SYS_GETPID   39
#define SYS_FORK     57
#define SYS_EXECVE   59
#define SYS_EXIT     60
#define SYS_WAITPID  61
#define SYS_READDIR  78
#define SYS_PROCINFO 79
#define SYS_CHDIR    80
#define SYS_GETCWD   81
#define SYS_CREAT    82
#define SYS_DUP2     83

typedef struct syscall_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t user_rip, user_rflags, user_rsp;
} syscall_frame_t;

void syscall_init(void);
void syscall_set_kernel_stack(uint64_t stack_top);
long syscall_dispatch(syscall_frame_t *frame);

bool user_range_ok(const void *user_ptr, size_t len);
int copy_from_user(void *dst, const void *user_src, size_t len);
int copy_to_user(void *user_dst, const void *src, size_t len);
int copy_user_string(char *dst, const char *user_src, size_t dst_size);
