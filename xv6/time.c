#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char **argv) {
    int wtime, rtime, id;

    int pid = fork();
    if(pid < 0){
        printf(2, "Fork failed\n");
        exit();
    }
    else if(pid == 0){
        if(argc == 1){
            printf(1, "Timing a sleep program\n");
            sleep(5);
            exit();
        }
        else{
            printf(1, "Timing --> %s\n", argv[1]);
            if(exec(argv[1], argv + 1) < 0){
                printf(2, "exec %s failed\n", argv[1]);
                exit();
            }
        }
    }

    id = waitx(&wtime, &rtime);
    if(id < 0){
        printf(2, "Some error occured\n");
        exit();
    }
    printf(1, "%s Info:\nWait Time: %d\nRun time: %d\n", argc == 1 ? "default" : argv[1], wtime, rtime);
    
    exit();
}