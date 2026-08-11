#include "lib/ulib.h"

static int cat_fd(int fd, const char *label) {
    char buf[96];
    for (;;) {
        long n = sys_read(fd, buf, sizeof(buf));
        if (n < 0) {
            u_write_str(2, "cat: read failed: ");
            u_write_str(2, label);
            u_write_char(2, '\n');
            return 1;
        }
        if (n == 0)
            break;
        sys_write(1, buf, (size_t)n);
    }

    return 0;
}

static int cat_one(const char *path) {
    long fd = sys_open(path);
    if (fd < 0) {
        u_write_str(2, "cat: open failed: ");
        u_write_str(2, path);
        u_write_char(2, '\n');
        return 1;
    }

    int rc = cat_fd((int)fd, path);
    sys_close((int)fd);
    return rc;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc < 2)
        return cat_fd(0, "stdin");

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (cat_one(argv[i]) != 0)
            rc = 1;
    }
    return rc;
}
