#define MAX_AGE 50

// Per-CPU state
struct cpu {
    uchar apicid;               // Local APIC ID
    struct context *scheduler;  // swtch() here to enter scheduler
    struct taskstate ts;        // Used by x86 to find stack for interrupt
    struct segdesc gdt[NSEGS];  // x86 global descriptor table
    volatile uint started;      // Has the CPU started?
    int ncli;                   // Depth of pushcli nesting.
    int intena;                 // Were interrupts enabled before pushcli?
    struct proc *proc;          // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;
// imported the global variable from spinlock.c to obbain the clock ticks
extern uint ticks;


//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
    uint edi;
    uint esi;
    uint ebx;
    uint ebp;
    uint eip;
};

enum procstate { UNUSED,
                 EMBRYO,
                 SLEEPING,
                 RUNNABLE,
                 RUNNING,
                 ZOMBIE };

// Per-process state
struct proc {
    uint sz;                     // Size of process memory (bytes)
    pde_t *pgdir;                // Page table
    char *kstack;                // Bottom of kernel stack for this process
    enum procstate state;        // Process state
    int pid;                     // Process ID
    struct proc *parent;         // Parent process
    struct trapframe *tf;        // Trap frame for current syscall
    struct context *context;     // swtch() here to run process
    void *chan;                  // If non-zero, sleeping on chan
    int killed;                  // If non-zero, have been killed
    struct file *ofile[NOFILE];  // Open files
    struct inode *cwd;           // Current directory
    char name[16];               // Process name (debugging)
    int ctime;                   // Process creation time
    int rtime;                   // process running time
    int etime;                   // process end time
    int wtime;                   // process wait time
    int made_runnable;           // last time recorded when process made runnable
    int priority;                // priority of the process => default 60
    int n_run;                   // number of times the process was picked up by the scheduler
    int cur_q;                   // The current queue of the process in case of MLFQ scheduling
    int ticks;                   // The no.of ticks it has recieved in a particular queue
    int q_ticks[5];              // To store the number of ticks recieved in each queue
    int q_enter_time;            // To store the time at which it entered the queue to prevent starvation
    int q_change;                // Flag to convey queue change upon ending of time slice       
    int last_run;                // Stores the last time the process is run
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap