#include "lib/ulib.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;

    for (int i = 1; i < argc; i++) {
        if (i > 1)
            u_write_char(1, ' ');
        u_write_str(1, argv[i]);
    }
    u_write_char(1, '\n');
    return 0;
}
