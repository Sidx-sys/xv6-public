# Implementing Scheduling Algorithms in XV-6 Shell

## Task 1 
### Implementing the waitx syscall

The syscall is added to `proc.c`, its structure is similar to the `wait` syscall, where we are *waiting* for the process to end and when it ends, the function returns the `pid` of the process that ended along with the `wait time` and `run time` of the process through pointers.
Here, 
`wtime` is calculated in a very detail manner => increments of *time when it became RUNNABLE - time at which the process changes its state to RUNNING/SLEEPING*, since iotime is not really the time for which the process is **waiting**, whereas whenever the state of the program is `RUNNING` I increment `rtime`.

*as something that's covered later, when the process ends I explicitly remove the process from the queues during MLFQ more of like a precaution that a process that has ended doesn't remain in the queue*

The corresponding **time** program to test the waitx syscall is created in the file `time.c` which takes the process to be timed in the arguments and returns the wait time, run time and total time by that process.

### Implementing the `ps` user program
The syscall to implement the `ps` user program is `get_pinfo` which is present in the `proc.c` file. It loops through all the process in the which are defined in the `ptable` and prints the requested information of each of these processes. The `ps` user program just calls the syscall.
The `w_time` in the ps user program is the time a process is waiting to be scheduled. So if a process is **Running** or **Sleeping** `w_time` is zero. (since when a process is sleeping either its waiting for an I/O request to be completed or just exists as a init)

The default values where some attributes doesn't make sense are put to -1, for example the `cur_q` and `q[i]` is by default set to -1 since it is valid only for MLFQ scheduling algorithm.

## Task 2: Implementing different Scheduling algorithms
### First come - First served Algorithm
In this scheduling algorithm, basically priority is proportional to the creation time of the process. (and lower the value of priority, the *higher its priority* is). Also please note that this algorithm is non-preemptive. Hence when I run `benchmark &; benchmark &` (which is essentially a program which uses for quite a long time) on the shell it basically hangs and no further command is processes until these two are finished.

*This also implies that yield() has been disabled for the FCFS algorithm*

This shows that there is not much of a waiting time in FCFS and the algorithm is decently working

Implementation:
                ```
                for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
                if(p->state != RUNNABLE)
                    continue;
                else if(min_P == 0)
                    min_P = p;
                else if(min_P->ctime > p->ctime)
                    min_P = p;
                }
                ```
Essentially, we want to find the process with the minimum `ctime` (creation time) and assign the processor to this process.
 
### Priority based scheduling Algorithm
In this scheduling algorithm, we give priority to the process on the basis of the process' `priority` attribute (which by default is set to 60). The user is allowed to change the priority of a process with `PID` by using the `setPriority` user program or by the syscall `set_priority` defined in the `proc.c` file.
In this case too the lower the value of the priority is, the more the process' priority is.

When tested with `benchmark2` which sets the priority of odd indexed process with 99 priority and even indexed process with 100 priority. The result of the following test was if we kept the `DEBUG` mode to `D`, we can see that first all the processes with the 99 prioirty are completed and then all the process with the 100 priority (majorly seen while testing).

Implementation:
- Finding the process with the minimum priority
                ```
                for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
                if(p->state != RUNNABLE)
                    continue;
                if(min_P == 0)
                    min_P = p;
                else if(min_P->priority > p->priority){
                    min_P = p;
                }
                }
                ```
- Then running another for loop to search:
    1. If a process of lower priority has entered the `ptable` while the execution is going on, in that case the execution is halted and we again start from choosing the process with the minimum priority
                        ```
                        for(q = ptable.proc; q < &ptable.proc[NPROC]; q++){
                        if(q->state != RUNNABLE)
                            continue;

                        if(q->priority < min_P->priority){
                            flag = 1;
                            break;
                        }
                        }

                        if(flag) // to break the outer for loop
                            break;
                        ```
    2. Checking if a process has the same priority as the process with the minimum priority, then all such process are run in a round-robin fashion.
                        ```
                        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
                             if(p->priority == min_P->priority){
                                // execution

                                c->proc = p;
                                switchuvm(p);
                                p->state = RUNNING;

                                swtch(&(c->scheduler), p->context);
                                switchkvm();

                                // Process is done running for now.
                                // It should have changed its p->state before coming back.
                                c->proc = 0
                             }
                        }
                        ```
### Multi-level feedback queue Scheduling
In this scheduling algorithm, we have 5 queues with the lowest index queue being the one with the highest priority and the processes are scheduled according to the priority of queues in which they are. We have also implemented aging to prevent starvation of processes in our algorithm. This is also implemented in the `scheduler` function in the file `proc.c`. 

To maintain the queue I have made a `struct Queue` which has a process list `qu` of length `NPROC` and `q_end` which stores the index of the last process in the process list of that queue (if no process is added its value is `-1`)
                    ```
                    struct Queue{
                        struct proc *qu[NPROC];
                        int q_end;
                    };
                    ```


The flow of the algorithm is as follows:

- We check for processes that have been starved of the processor for too long, and accordingly shift them to the higher queue so that they get the CPU too. I have taken my `MAX_AGE` as the limit to waiting and set it to `50` ticks.
                    ```
                    for(int i = 1; i < 5; i++){
                                for(int j = 0; j <= queue[i].q_end; j++){

                                    struct proc *p = queue[i].qu[j];
                                    int age = ticks - p->q_enter_time;

                                    if(age > MAX_AGE){
                                        q_pop(p, i);
                                        q_push(p, i - 1);
                                    }
                                }
                            }
                    ```
- I then search for the process that is to be scheduled, by searching in order from highest priority to the lowest priority queue and choosing the first queue in which any process is available.
                    ```
                    for(int i = 0; i < 5; i++){
                                // cprintf("Process in queue %d: %d\n", i, q_end[i] + 1);
                                if(queue[i].q_end >= 0){
                                    p = queue[i].qu[0];
                                    q_pop(p, i); // error happens here
                                    break;
                                }
                            }
                    ```
- After finding the process we simply schedule it to the processor. Now comes a crucial part of managing the `max_ticks` section. Since every queue has its own *time slice*, we need to make sure that a process gets atleast that much ticks unless it voluntarily relinquishes control of the CPU (whether for I/O or it exits). So we have a check in `trap.c` file when it checks to interrupt the processor and yield the current process
                    ```
                    if (myproc() && myproc()->state == RUNNING && tf->trapno == T_IRQ0 + IRQ_TIMER){
                                    if(myproc()->ticks >= q_ticks_max[myproc()->cur_q]){
                                        queue_change(myproc());
                                        yield();
                                    }
                        }
                    ```
So A.T.Q we would only yield if the time slice given to that process (according to the queue in which it is) gets over. On which we *change its queue to a lower priority queue* and yieled the process.

- Back in the `proc.c` file, inside the scheduler after the processing of the scheduled proceess is done, we again check if the process is RUNNABLE or not, and then accordingly push it to the queue for further processing (if required only then, else the process exits).