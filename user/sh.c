#include "lib/ulib.h"

#define LINE_MAX 128
#define ARG_MAX  12

typedef struct {
    const char *input;
    const char *output;
} command_io_t;

static int run_command(char *line);

static char *skip_spaces(char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

static void trim_tail(char *s) {
    size_t n = u_strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static int split_args(char *line, char *argv[ARG_MAX]) {
    int argc = 0;
    char *p = skip_spaces(line);

    while (*p && argc < ARG_MAX - 1) {
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (!*p)
            break;
        *p++ = '\0';
        p = skip_spaces(p);
    }
    argv[argc] = 0;
    return argc;
}

static int parse_redirections(char *argv[ARG_MAX], command_io_t *io) {
    int dst = 0;
    if (io) {
        io->input = 0;
        io->output = 0;
    }

    for (int i = 0; argv[i]; i++) {
        if (u_strcmp(argv[i], "<") == 0 || u_strcmp(argv[i], ">") == 0) {
            char op = argv[i][0];
            if (!argv[i + 1]) {
                u_write_str(1, "sh: missing redirection target\n");
                return -1;
            }
            if (op == '<')
                io->input = argv[++i];
            else
                io->output = argv[++i];
            continue;
        }
        argv[dst++] = argv[i];
    }

    argv[dst] = 0;
    return dst;
}

static int read_line(char *buf, size_t cap) {
    size_t len = 0;

    while (len + 1 < cap) {
        char c = 0;
        long n = sys_read(0, &c, 1);
        if (n < 0)
            return -1;
        if (n == 0)
            continue;
        if (c == '\r')
            c = '\n';
        if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                u_write_str(1, "\b \b");
            }
            continue;
        }
        u_write_char(1, c);
        if (c == '\n') {
            buf[len] = '\0';
            return (int)len;
        }
        buf[len++] = c;
    }

    buf[len] = '\0';
    u_write_str(1, "\n");
    return (int)len;
}

static int setup_child_io(const command_io_t *io) {
    if (!io)
        return 0;

    if (io->input) {
        long fd = sys_open(io->input);
        if (fd < 0) {
            u_write_str(2, "sh: cannot open input: ");
            u_write_str(2, io->input);
            u_write_char(2, '\n');
            return 1;
        }
        if (sys_dup2((int)fd, 0) < 0) {
            u_write_str(2, "sh: dup2 input failed\n");
            sys_close((int)fd);
            return 1;
        }
        if (fd != 0)
            sys_close((int)fd);
    }

    if (io->output) {
        long fd = sys_creat(io->output);
        if (fd < 0) {
            u_write_str(2, "sh: cannot create output: ");
            u_write_str(2, io->output);
            u_write_char(2, '\n');
            return 1;
        }
        if (sys_dup2((int)fd, 1) < 0) {
            u_write_str(2, "sh: dup2 output failed\n");
            sys_close((int)fd);
            return 1;
        }
        if (fd != 1)
            sys_close((int)fd);
    }

    return 0;
}

static int run_external_with_io(char *argv[ARG_MAX], const command_io_t *io) {
    char path_buf[64];
    char *child_argv[ARG_MAX];
    char *envp[] = { "PATH=/bin", 0 };

    const char *path = argv[0];
    if (argv[0][0] != '/') {
        const char prefix[] = "/bin/";
        size_t off = 0;
        while (prefix[off]) {
            path_buf[off] = prefix[off];
            off++;
        }
        for (size_t i = 0; argv[0][i] && off + 1 < sizeof(path_buf); i++)
            path_buf[off++] = argv[0][i];
        path_buf[off] = '\0';
        path = path_buf;
    }

    int argc = 0;
    while (argv[argc] && argc < ARG_MAX - 1) {
        child_argv[argc] = argv[argc];
        argc++;
    }
    child_argv[argc] = 0;

    long pid = sys_fork();
    if (pid < 0) {
        u_write_str(1, "exec: fork failed\n");
        return 1;
    }
    if (pid == 0) {
        if (setup_child_io(io) != 0)
            sys_exit(126);
        sys_execve(path, child_argv, envp);
        u_write_str(2, "exec: not found: ");
        u_write_str(2, path);
        u_write_str(2, "\n");
        sys_exit(127);
    }

    int status = 0;
    long waited = sys_waitpid((int)pid, &status, 0);
    if (waited != pid) {
        u_write_str(1, "exec: wait failed\n");
        return 1;
    }
    return (status >> 8) & 0xff;
}

static int run_smoke_line(const char *line) {
    char buf[LINE_MAX];
    size_t i = 0;

    u_write_str(1, "lithium$ ");
    u_write_str(1, line);
    u_write_char(1, '\n');

    while (line[i] && i + 1 < sizeof(buf)) {
        buf[i] = line[i];
        i++;
    }
    buf[i] = '\0';
    return run_command(buf);
}

static int run_command(char *line) {
    char *argv[ARG_MAX];
    command_io_t io;
    trim_tail(line);
    int argc = split_args(line, argv);
    if (argc == 0)
        return 0;
    argc = parse_redirections(argv, &io);
    if (argc < 0)
        return 1;
    if (argc == 0)
        return 0;

    if (u_strcmp(argv[0], "help") == 0) {
        u_write_str(1, "builtins: help cd pwd pid exec exit\n");
        u_write_str(1, "commands: echo ls cat ps smoke init sh\n");
        u_write_str(1, "redirection: cmd > file, cmd < file\n");
        return 0;
    }
    if (u_strcmp(argv[0], "cd") == 0) {
        const char *path = argc > 1 ? argv[1] : "/";
        if (sys_chdir(path) < 0) {
            u_write_str(1, "cd: no such directory: ");
            u_write_str(1, path);
            u_write_char(1, '\n');
            return 1;
        }
        return 0;
    }
    if (u_strcmp(argv[0], "pwd") == 0) {
        char cwd[128];
        if (sys_getcwd(cwd, sizeof(cwd)) < 0) {
            u_write_str(1, "pwd: failed\n");
            return 1;
        }
        u_write_str(1, cwd);
        u_write_char(1, '\n');
        return 0;
    }
    if (u_strcmp(argv[0], "pid") == 0) {
        u_write_uint(1, (uint32_t)sys_getpid());
        u_write_char(1, '\n');
        return 0;
    }
    if (u_strcmp(argv[0], "exec") == 0) {
        if (argc < 2) {
            u_write_str(1, "exec: missing path\n");
            return 1;
        }
        return run_external_with_io(&argv[1], &io);
    }
    if (u_strcmp(argv[0], "exit") == 0)
        return -1;

    return run_external_with_io(argv, &io);
}

static int smoke_main(void) {
    u_write_str(1, "[user] sh starting pid=");
    u_write_uint(1, (uint32_t)sys_getpid());
    u_write_str(1, "\n");

    if (run_smoke_line("echo hello from user sh") != 0)
        return 1;
    if (run_smoke_line("ls /") != 0)
        return 1;
    u_write_str(1, "lithium$ cd /etc\n");
    if (sys_chdir("/etc") != 0)
        return 1;
    if (run_smoke_line("cat hostname") != 0)
        return 1;
    u_write_str(1, "lithium$ cd /\n");
    if (sys_chdir("/") != 0)
        return 1;
    if (run_smoke_line("echo redirected from sh > /tmp/redir") != 0)
        return 1;
    if (run_smoke_line("cat < /tmp/redir") != 0)
        return 1;
    if (run_smoke_line("cat /etc/motd") != 0)
        return 1;
    if (run_smoke_line("ps") != 0)
        return 1;

    u_write_str(1, "[user] sh smoke OK\n");
    return 0;
}

static int interactive_main(void) {
    char line[LINE_MAX];

    u_write_str(1, "[user] interactive sh ready pid=");
    u_write_uint(1, (uint32_t)sys_getpid());
    u_write_str(1, "\n");

    for (;;) {
        u_write_str(1, "lithium$ ");
        int n = read_line(line, sizeof(line));
        if (n < 0)
            return 1;
        int rc = run_command(line);
        if (rc < 0)
            return 0;
    }
}

int main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc > 1 && u_strcmp(argv[1], "--smoke") == 0)
        return smoke_main();
    return interactive_main();
}
