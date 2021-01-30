#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char **argv){
    // basic error handling
    if(argc != 3){
        printf(2, "Usage: setPriority <new_priority> <pid>\n");
        exit();
    }

    int new_priority = atoi(argv[1]);
    int pid = atoi(argv[2]);
    int old_priority = set_priority(new_priority, pid);

    printf(1, "Updated the priority of process %d with old priority = %d to new priority = %d\n", pid, old_priority, new_priority);
    exit();
}