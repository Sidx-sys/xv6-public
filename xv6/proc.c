#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// Data structures for MLFQ, not in ifdef since used in a function
struct Queue{
    struct proc *qu[NPROC];
    int q_end;
};

struct Queue queue[5];

#ifdef MLFQ
void queue_init(void){
    for(int i = 0; i < 5; i++)
        queue[i].q_end  = -1;
}
#endif

struct {
    struct spinlock lock;
    struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);


void update_ticks(struct proc *p){
    acquire(&ptable.lock);
    p->ticks++;
    p->q_ticks[p->cur_q]++;
    release(&ptable.lock);
}

void queue_change(struct proc *p){
    acquire(&ptable.lock);
    p->q_change = 1;
    release(&ptable.lock);
}

void q_push(struct proc *p, int q_no){

    if(p != 0){
    // Adding the process to the queue
    p->q_enter_time = ticks;
    p->cur_q = q_no;
    queue[q_no].q_end++;
    queue[q_no].qu[queue[q_no].q_end] = p;
    }
}

void q_pop(struct proc *p, int q_no){
    int found = -1;
    
    // checking if the process exists in the queue
    for(int i = 0; i <= queue[q_no].q_end; i++){
        if(p->pid == queue[q_no].qu[i]->pid){
            found = i;
            break;
        }
    }

    if(found < 0)
       return;

    // if found, remove the process from the queue
    for(int i = found; i < queue[q_no].q_end; i++){
        queue[q_no].qu[i] = queue[q_no].qu[i + 1];
    }
    queue[q_no].qu[queue[q_no].q_end] = 0;
    queue[q_no].q_end--;
}

void pinit(void) {
    initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid() {
    return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void) {
    int apicid, i;

    if (readeflags() & FL_IF)
        panic("mycpu called with interrupts enabled\n");

    apicid = lapicid();
    // APIC IDs are not guaranteed to be contiguous. Maybe we should have
    // a reverse map, or reserve a register to store &cpus[i].
    for (i = 0; i < ncpu; ++i) {
        if (cpus[i].apicid == apicid)
            return &cpus[i];
    }
    panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void) {
    struct cpu *c;
    struct proc *p;
    pushcli();
    c = mycpu();
    p = c->proc;
    popcli();
    return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc *
allocproc(void) {
    struct proc *p;
    char *sp;

    acquire(&ptable.lock);

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        if (p->state == UNUSED)
            goto found;

    release(&ptable.lock);
    return 0;

found:
    p->state = EMBRYO;
    p->pid = nextpid++;

    release(&ptable.lock);

    // Allocate kernel stack.
    if ((p->kstack = kalloc()) == 0) {
        p->state = UNUSED;
        return 0;
    }
    sp = p->kstack + KSTACKSIZE;

    // Leave room for trap frame.
    sp -= sizeof *p->tf;
    p->tf = (struct trapframe *)sp;

    // Set up new context to start executing at forkret,
    // which returns to trapret.
    sp -= 4;
    *(uint *)sp = (uint)trapret;

    sp -= sizeof *p->context;
    p->context = (struct context *)sp;
    memset(p->context, 0, sizeof *p->context);
    p->context->eip = (uint)forkret;

    // creation time of process
    p->ctime = ticks;
    p->rtime = p->wtime = p->made_runnable = 0;
    // default priority
    p->priority = 60;
    // default pick num
    p->n_run = 0;
    // defalut in no queue
    p->cur_q = -1;
    for(int i = 0; i < 5; i++)
        p->q_ticks[i] = -1;
    
    p->ticks = 0;
    p->q_change = 0;
    p->last_run = p->ctime;

    // initialize q_ticks to 0 if in MLFQ scheduling algorithm
    #ifdef MLFQ
        for(int i = 0;i < 5; i++)
            p->q_ticks[i] = 0;
    #endif

    return p;
}

//PAGEBREAK: 32
// Set up first user process.
void userinit(void) {
    struct proc *p;
    extern char _binary_initcode_start[], _binary_initcode_size[];

    p = allocproc();

    initproc = p;
    if ((p->pgdir = setupkvm()) == 0)
        panic("userinit: out of memory?");
    inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
    p->sz = PGSIZE;
    memset(p->tf, 0, sizeof(*p->tf));
    p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
    p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
    p->tf->es = p->tf->ds;
    p->tf->ss = p->tf->ds;
    p->tf->eflags = FL_IF;
    p->tf->esp = PGSIZE;
    p->tf->eip = 0;  // beginning of initcode.S

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    // this assignment to p->state lets other cores
    // run this process. the acquire forces the above
    // writes to be visible, and the lock is also needed
    // because the assignment might not be atomic.
    acquire(&ptable.lock);

    p->made_runnable = ticks;
    p->state = RUNNABLE;
    // now since the process is runnable, we can put it in the q[0] for scheduling
    #ifdef MLFQ
        q_push(p, 0);
    #endif

    release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n) {
    uint sz;
    struct proc *curproc = myproc();

    sz = curproc->sz;
    if (n > 0) {
        if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
            return -1;
    } else if (n < 0) {
        if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
            return -1;
    }
    curproc->sz = sz;
    switchuvm(curproc);
    return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void) {
    int i, pid;
    struct proc *np;
    struct proc *curproc = myproc();

    // Allocate process.
    if ((np = allocproc()) == 0) {
        return -1;
    }

    // Copy process state from proc.
    if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0) {
        kfree(np->kstack);
        np->kstack = 0;
        np->state = UNUSED;
        return -1;
    }
    np->sz = curproc->sz;
    np->parent = curproc;
    *np->tf = *curproc->tf;

    // Clear %eax so that fork returns 0 in the child.
    np->tf->eax = 0;

    for (i = 0; i < NOFILE; i++)
        if (curproc->ofile[i])
            np->ofile[i] = filedup(curproc->ofile[i]);
    np->cwd = idup(curproc->cwd);

    safestrcpy(np->name, curproc->name, sizeof(curproc->name));

    pid = np->pid;

    acquire(&ptable.lock);
    np->made_runnable = ticks;
    np->state = RUNNABLE;
    #ifdef MLFQ
        q_push(np, 0);
    #endif

    release(&ptable.lock);

    return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void) {
    struct proc *curproc = myproc();
    struct proc *p;
    int fd;

    if (curproc == initproc)
        panic("init exiting");

    // Close all open files.
    for (fd = 0; fd < NOFILE; fd++) {
        if (curproc->ofile[fd]) {
            fileclose(curproc->ofile[fd]);
            curproc->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(curproc->cwd);
    end_op();
    curproc->cwd = 0;

    acquire(&ptable.lock);

    // Parent might be sleeping in wait().
    wakeup1(curproc->parent);

    // Pass abandoned children to init.
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->parent == curproc) {
            p->parent = initproc;
            if (p->state == ZOMBIE)
                wakeup1(initproc);
        }
    }

    // Jump into the scheduler, never to return.
    // registering the time for process exit
    curproc->etime = ticks;
    curproc->state = ZOMBIE;
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void) {
    struct proc *p;
    int havekids, pid;
    struct proc *curproc = myproc();

    acquire(&ptable.lock);
    for (;;) {
        // Scan through table looking for exited children.
        havekids = 0;
        for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if (p->parent != curproc)
                continue;
            havekids = 1;
            if (p->state == ZOMBIE) {
                // Found one.
                #ifdef MLFQ
                    q_pop(p, p->cur_q);
                #endif
                pid = p->pid;
                kfree(p->kstack);
                p->kstack = 0;
                freevm(p->pgdir);
                p->pid = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;
                p->state = UNUSED;
                release(&ptable.lock);
                return pid;
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || curproc->killed) {
            release(&ptable.lock);
            return -1;
        }

        // Wait for children to exit.  (See wakeup1 call in proc_exit.)
        sleep(curproc, &ptable.lock);  //DOC: wait-sleep
    }
}

// Custom syscall waitx, extending wait syscall
int waitx(int *wtime, int *rtime) {
    struct proc *p;
    int havekids, pid;
    struct proc *curproc = myproc();

    acquire(&ptable.lock);
    for (;;) {
        // Scan through table looking for exited children.
        havekids = 0;
        for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if (p->parent != curproc)
                continue;
            havekids = 1;
            if (p->state == ZOMBIE) {
                // Found one.
                // added additional data as per requirements
                *wtime = p->wtime;
                *rtime = p->rtime;
                #ifdef MLFQ
                    q_pop(p, p->cur_q);
                #endif
                pid = p->pid;
                kfree(p->kstack);
                p->kstack = 0;
                freevm(p->pgdir);
                p->pid = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;
                p->state = UNUSED;
                release(&ptable.lock);
                return pid;
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || curproc->killed) {
            release(&ptable.lock);
            return -1;
        }

        // Wait for children to exit.  (See wakeup1 call in proc_exit.)
        sleep(curproc, &ptable.lock);  //DOC: wait-sleep
    }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler
void scheduler(void){
    struct cpu *c = mycpu();
    c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    #ifdef RR
        struct proc *p;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->state != RUNNABLE)
                continue;

            p->n_run++;
            // Switch to chosen process.  It is the process's job
            // to release ptable.lock and then reacquire it
            // before jumping back to us.
            c->proc = p;
            switchuvm(p);
            p->state = RUNNING;
            p->wtime += ticks - p->made_runnable;

            swtch(&(c->scheduler), p->context);
            switchkvm();

            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;
        }
    #else
    #ifdef FCFS
        struct proc *min_P = 0, *p;
        
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->state != RUNNABLE)
                continue;
            else if(min_P == 0)
                min_P = p;
            else if(min_P->ctime > p->ctime)
                min_P = p;
        }
        if(min_P == 0){
            release(&ptable.lock);
            continue;
        }

        p = min_P;
        p->n_run++;
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        switchuvm(p);
        p->wtime += ticks - p->made_runnable;
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
    #else
    #ifdef PBS
        struct proc *p;
        // Priority based scheduling algorithm
        struct proc *min_P = 0, *q;
        int flag = 0;

        for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if(p->state != RUNNABLE)
                continue;
            if(min_P == 0)
                min_P = p;
            else if(min_P->priority > p->priority){
                min_P = p;
            }
        }

        if(min_P == 0){
            release(&ptable.lock);
            continue;
        }
        // Doing round robin for process with same priority

        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->state != RUNNABLE)
                    continue;
            
            for(q = ptable.proc; q < &ptable.proc[NPROC]; q++){
                if(q->state != RUNNABLE)
                    continue;

                if(q->priority < min_P->priority){
                    flag = 1;
                    break;
                }
            }

            if(flag)
                break;

            if(p->priority == min_P->priority){
                #ifdef D
                    cprintf("Process with PID: %d and priority %d is scheduled\n", p->pid, p->priority);
                #endif

                // since scheduled
                p->n_run++;
                // Switch to chosen process.  It is the process's job
                // to release ptable.lock and then reacquire it
                // before jumping back to us.
                c->proc = p;
                switchuvm(p);
                p->wtime += ticks - p->made_runnable;
                p->state = RUNNING;

                swtch(&(c->scheduler), p->context);
                switchkvm();

                // Process is done running for now.
                // It should have changed its p->state before coming back.
                c->proc = 0;
            }
        }
    #else
    #ifdef MLFQ
        // checking for proceseses that have been waiting for too long
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

        struct proc *p = 0;

        // searching for process to schedule
        for(int i = 0; i < 5; i++){
            // cprintf("Process in queue %d: %d\n", i, q_end[i] + 1);
            if(queue[i].q_end >= 0){
                p = queue[i].qu[0];
                q_pop(p, i); // error happens here
                break;
            }
        }

        // scheduling the process
        if(p != 0){
            // picked by the scheduler 
            p->n_run++;

            c->proc = p;
            switchuvm(p);
            p->wtime += ticks - p->made_runnable;
            p->state = RUNNING;

            swtch(&(c->scheduler), p->context);
            switchkvm();

            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;

            // will have to check upon the end of the given time slice
            if(p->state == RUNNABLE) {
                if(p->q_change){
                    p->ticks = 0;
                    p->q_change = 0;

                    if(p->cur_q != 4)
                        q_push(p, p->cur_q + 1);
                    else
                        q_push(p, p->cur_q);
                }
                else{
                    p->ticks = 0;
                    q_push(p, p->cur_q);
                }
            }
        }
    #endif 
    #endif
    #endif
    #endif
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void) {
    int intena;
    struct proc *p = myproc();

    if (!holding(&ptable.lock))
        panic("sched ptable.lock");
    if (mycpu()->ncli != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched running");
    if (readeflags() & FL_IF)
        panic("sched interruptible");
    intena = mycpu()->intena;
    swtch(&p->context, mycpu()->scheduler);
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void) {
    acquire(&ptable.lock);  //DOC: yieldlock
    myproc()->made_runnable = ticks;
    myproc()->state = RUNNABLE;
    sched();
    release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void) {
    static int first = 1;
    // Still holding ptable.lock from scheduler.
    release(&ptable.lock);

    if (first) {
        // Some initialization functions must be run in the context
        // of a regular process (e.g., they call sleep), and thus cannot
        // be run from main().
        first = 0;
        iinit(ROOTDEV);
        initlog(ROOTDEV);
    }

    // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk) {
    struct proc *p = myproc();

    if (p == 0)
        panic("sleep");

    if (lk == 0)
        panic("sleep without lk");

    // Must acquire ptable.lock in order to
    // change p->state and then call sched.
    // Once we hold ptable.lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup runs with ptable.lock locked),
    // so it's okay to release lk.
    if (lk != &ptable.lock) {   //DOC: sleeplock0
        acquire(&ptable.lock);  //DOC: sleeplock1
        release(lk);
    }
    // Go to sleep.
    p->chan = chan;
    p->wtime += ticks - p->made_runnable;
    p->state = SLEEPING;

    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    if (lk != &ptable.lock) {  //DOC: sleeplock2
        release(&ptable.lock);
        acquire(lk);
    }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan) {
    struct proc *p;

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        if (p->state == SLEEPING && p->chan == chan){
            p->made_runnable = ticks;
            p->state = RUNNABLE;
            #ifdef MLFQ
                p->ticks = 0;
                q_push(p, p->cur_q);
            #endif
        }
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan) {
    acquire(&ptable.lock);
    wakeup1(chan);
    release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid) {
    struct proc *p;

    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->pid == pid) {
            p->killed = 1;
            // Wake process from sleep if necessary.
            if (p->state == SLEEPING){
                p->made_runnable = ticks;
                p->state = RUNNABLE;
                #ifdef MLFQ
					q_push(p, p->cur_q);
				#endif
            }
            release(&ptable.lock);
            return 0;
        }
    }
    release(&ptable.lock);
    return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void) {
    static char *states[] = {
        [UNUSED] "unused",
        [EMBRYO] "embryo",
        [SLEEPING] "sleep ",
        [RUNNABLE] "runble",
        [RUNNING] "run   ",
        [ZOMBIE] "zombie"};
    int i;
    struct proc *p;
    char *state;
    uint pc[10];

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state == UNUSED)
            continue;
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        cprintf("%d %s %s", p->pid, state, p->name);
        if (p->state == SLEEPING) {
            getcallerpcs((uint *)p->context->ebp + 2, pc);
            for (i = 0; i < 10 && pc[i] != 0; i++)
                cprintf(" %p", pc[i]);
        }
        cprintf("\n");
    }
}

void get_psinfo(void) {
    struct proc *p;

    static char *states[] = {
        [UNUSED] "unused",
        [EMBRYO] "embryo",
        [SLEEPING] "sleep ",
        [RUNNABLE] "runnable",
        [RUNNING] "running",
        [ZOMBIE] "zombie"};

    cprintf("PID \t Priority State \t r_time w_time \t n_run \t cur_q \t q0 \t q1 \t q2 \t q3 \t q4 \n");

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        
        int pid = p->pid;
        if(pid == 0)
            continue;
        int priority = p->priority;
        char *state = states[p->state];
        int r_time = p->rtime;
        // w_time is the time process has been waiting in a queue
        int w_time = ticks - p->last_run;
        #ifdef MLFQ
            w_time = ticks - p->q_enter_time;
        #endif

        if(p->state == RUNNING || p->state == SLEEPING)
            w_time = 0;

        int n_run = p->n_run;
        // change if scheduling through MLFQ
        int cur_q = p->cur_q;
        if(p->state == SLEEPING)
            cur_q = -1;

        cprintf("%d \t %d \t %s \t %d \t %d \t %d \t %d \t %d \t %d \t %d \t %d \t %d\n", pid, priority, state, r_time, w_time, n_run, cur_q, p->q_ticks[0], p->q_ticks[1], p->q_ticks[2], p->q_ticks[3], p->q_ticks[4]);  
    }
}

int set_priority(int new_priority, int pid){
    // error handling
    if(new_priority > 100 || new_priority < 0){
        cprintf("Invalid priority provided\n");
        return 0;
    }

    // cprintf("New Priority: %d, pid: %d\n", new_priority, pid);

    struct proc *p;
    int old_priority = -1,  to_yield = 0;

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->pid == pid){
            acquire(&ptable.lock);
            old_priority = p->priority;
            p->priority = new_priority;

            // to check whether to yield it or not
            if(old_priority > new_priority)
                to_yield = 1;

            release(&ptable.lock);
            break;
        }
    }

    if(to_yield == 1)
        yield();

    return old_priority;
}