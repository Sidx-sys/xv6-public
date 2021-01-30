## RR Algorithm
Performance of the RR algorithm:
Avg(wait time) =  135 ticks
Avg(run time) = 12 ticks
Avg(sleep time) = 1105 ticks

## FCFS Algorithm
Performance of the FCFS algorithm for `benchmark2` on average (as found using the `time` user program):
Avg(wait time) = 10 ticks
Avg(run time) = 12 ticks
Avg(sleep time) = 1048 ticks

## PBS Algorithm
Performance of the PBS algorithm for `benchmark2` on average:
Avg(wait time) = 5 ticks
Avg(run time) = 12 ticks
Avg(sleep time) =  1069 ticks

## MLFQ Algorithm
Performance of the MLFQ algorithm:
Avg(wait time) =  105 ticks
Avg(run time) = 9 ticks
Avg(sleep time) = 1200 ticks

It can be seen that on an a*verage the minimum sleep time* or *idle time* of the CPU is in the **FCFS algorithm** since there is no spread in the type of the process due to the absence of the overhead caused due to context switching.


## Process exploiting the time slices in MLFQ algorithm
If a process voluntarily relinquishes the CPU, for reason such as I/O its inserted in the tail of the same queue (*rather than actually being demoted to a lower priority queue*). Since the process knows the time slice of each of the queue levels, the process can exploit this mechanism by **giving up control of the CPU a few ticks before the time slice ends** due to which it stays in the higher priority zone and gets the most out of the execution time it gets.