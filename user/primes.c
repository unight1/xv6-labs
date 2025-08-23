#include "kernel/types.h"
#include "user/user.h"

void sieve(int in_fd) {
    int prime;
    if (read(in_fd, &prime, sizeof(int)) == 0) {
        close(in_fd);
        exit(0);
    }
    printf("prime %d\n", prime);

    int p[2];
    pipe(p);
    
    if (fork() == 0) {
        close(p[1]);
        sieve(p[0]);
    } else {
        close(p[0]);
        int num;
        while (read(in_fd, &num, sizeof(int)) > 0) {
            if (num % prime != 0) {
                write(p[1], &num, sizeof(int));
            }
        }
        close(in_fd);
        close(p[1]);
        wait(0);
    }
}

int main() {
    int p[2];
    pipe(p);
    
    if (fork() == 0) {
        close(p[1]);
        sieve(p[0]);
    } else {
        close(p[0]);
        for (int i = 2; i <= 35; i++) {
            write(p[1], &i, sizeof(int));
        }
        close(p[1]);
        wait(0);
    }
    exit(0);
}

