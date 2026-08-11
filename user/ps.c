#include "lib/ulib.h"

static const char *state_name(uint32_t state) {
    switch (state) {
    case 0: return "ready";
    case 1: return "run";
    case 2: return "block";
    case 3: return "dead";
    default: return "?";
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    task_info_t info;
    int seen = 0;

    u_write_str(1, "PID PPID STATE MODE TICKS NAME\n");
    for (uint32_t i = 0; ; i++) {
        long rc = sys_procinfo(i, &info);
        if (rc < 0)
            break;

        u_write_uint(1, info.pid);
        u_write_char(1, ' ');
        u_write_uint(1, info.ppid);
        u_write_char(1, ' ');
        u_write_str(1, state_name(info.state));
        u_write_char(1, ' ');
        u_write_str(1, info.user_mode ? "user " : "kern ");
        u_write_uint(1, (uint32_t)info.ticks);
        u_write_char(1, ' ');
        u_write_str(1, info.name);
        u_write_char(1, '\n');
        seen++;
    }

    return seen > 0 ? 0 : 1;
}
