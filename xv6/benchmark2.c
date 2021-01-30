#include "types.h"
#include "user.h"

int number_of_processes = 40;

int main(int argc, char *argv[])
{
  int j;
  for (j = 0; j < number_of_processes; j++)
  {
    int pid = fork();
    if (pid < 0)
    {
      printf(1, "Fork failed\n");
      continue;
    }
    if (pid == 0)
    {

        int flag = j%2;

             if(flag == 0)
             {
                 sleep(200);
                 for(volatile int i = 0; i<100000000; i++)
                     ;
             }
             else if(flag == 1)
             {
                 for(volatile int i = 0; i<100000000; i++)
                    ;
                sleep(200);    
             }
    //   printf(1, "Process: %d Finished\n", j);
      exit();
    }
    else{
    set_priority(100-(j%2),pid); // will only matter for PBS, comment it out if not implemented yet (better priorty for more IO intensive jobs)
    }
  }
  for (j = 0; j < number_of_processes+5; j++)
  {
    wait();
  }

  exit();
}