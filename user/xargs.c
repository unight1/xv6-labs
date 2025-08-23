#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: xargs <command> [args...]\n");
        exit(1);
    }

    char buf[512];  
    char *args[MAXARG];  
    int i, n, pos = 0;

    for (i = 1; i < argc; i++) {
        args[i-1] = argv[i];
    }

    while ((n = read(0, buf + pos, sizeof(buf) - pos - 1)) > 0) {
        for (i = pos; i < pos + n; i++) {
            if (buf[i] == '\n') {
                buf[i] = 0;  
                args[argc-1] = buf;  
                args[argc] = 0;  

                if (fork() == 0) {
                    exec(args[0], args);
                    fprintf(2, "xargs: exec %s failed\n", args[0]);
                    exit(1);
                } else {
                    wait(0);  
                }
                pos = 0;  
                break;
            }
        }
        if (i == pos + n) {
            pos += n;  
        }
    }

    exit(0);
}

