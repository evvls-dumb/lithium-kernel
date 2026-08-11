#include "lib/ulib.h"

static int list_path(const char *path) {
    char name[64];
    uint32_t type = 0;
    int seen = 0;

    for (uint32_t i = 0; ; i++) {
        long rc = sys_readdir(path, i, name, &type);
        if (rc < 0)
            break;
        u_write_str(1, type == 1 ? "d " : "- ");
        u_write_str(1, name);
        u_write_char(1, '\n');
        seen++;
    }

    if (!seen) {
        u_write_str(2, "ls: cannot read: ");
        u_write_str(2, path);
        u_write_char(2, '\n');
        return 1;
    }
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;

    const char *path = argc > 1 ? argv[1] : ".";
    return list_path(path);
}
