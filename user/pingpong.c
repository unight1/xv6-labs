#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int parent_to_child[2];  
    int child_to_parent[2];
    pipe(parent_to_child);
    pipe(child_to_parent);
    char buf[1];  
    if(fork() == 0) {  
        close(parent_to_child[1]);  
        close(child_to_parent[0]);  
        read(parent_to_child[0], buf, 1);
        printf("%d: received ping\n", getpid());
        
        write(child_to_parent[1], buf, 1);
        
        close(parent_to_child[0]);
        close(child_to_parent[1]);
        exit(0);
    } else {  
        close(parent_to_child[0]);  
        close(child_to_parent[1]);  
        
        write(parent_to_child[1], "a", 1);
        
        read(child_to_parent[0], buf, 1);
        printf("%d: received pong\n", getpid());
        
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        exit(0);
    }
}
