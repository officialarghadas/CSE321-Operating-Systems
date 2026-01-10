#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "Usage: test_scheduler tickets\n");
    exit(1);
  }

  int tickets = atoi(argv[1]);
  
  if(tickets < 1){
    fprintf(2, "tickets must be at least 1\n");
    exit(1);
  }

  // Set the ticket count for this process
  if(settickets(tickets) < 0){
    fprintf(2, "settickets failed\n");
    exit(1);
  }

  // Spin in an infinite loop
  while(1) {
    // Just consume CPU time
  }

  exit(0);
}
