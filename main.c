#include <stdint.h>
#include <stdio.h>
#include <stdbool.h> 
#include <string.h>
#include <sys/syslog.h>
#include <unistd.h>     
#include <sys/types.h>  
#include <sys/stat.h>  
#include <stdlib.h>   
#include <syslog.h>
#include <signal.h>
#include <time.h>

#define PATH_MAX 200
volatile sig_atomic_t wakeup = 0;

void pUse(){
  printf("Do: ./SyncDaemon [Source_path] [Dest_path] [-OPTIONS*]\n"); 
  printf("OPTIONS:\n-t sleep time (in seconds, minimal 10) before daemon starts\n");
  printf("-R recursive synchronization (folders will be recursively synchronized)\n");
}

void handle_SIGUSR1(int sig) {
  wakeup = 1;
}

void Demonize(){
  pid_t pid;
  pid = fork();
  
  if(pid < 0)
    exit(EXIT_FAILURE);
  if(pid > 0)
    exit(EXIT_SUCCESS);
  if(setsid() < 0)
    exit(EXIT_FAILURE);
  
  pid = fork();

  if(pid < 0)
    exit(EXIT_FAILURE);
  if(pid > 0)
    exit(EXIT_SUCCESS);
  
  umask(0);
  chdir("/");
  
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
}

int main(int _argc, char** _argv){
  if(_argc < 3){
    pUse();
    return -1;
  }

  char src[PATH_MAX], dst[PATH_MAX];
  realpath(_argv[1], src);
  realpath(_argv[2], dst);
  
  uint32_t tSleep = 5 * 60;
  bool recursive = false;
  
  for(int i=3; i<_argc; i++){
    if(_argv[i][0] != '-' || strlen(_argv[i]) != 2){
      pUse();
      return -1;
    }
    if(_argv[i][1] == 'R') recursive = true;
    else if(_argv[i][1] == 't'){
      if(i++ >= _argc){
        pUse();
        return -1;
      }
      tSleep = atoi(_argv[i]);
    }
  }
  if(tSleep < 10) tSleep = 10;
  
  //----------------------------------------
  //sprawdzić czy ścieżki src i dst istnieją 
  //----------------------------------------
  
  if(strcmp(src, dst) == 0){
    printf("Destination directory is the same as source directory\n");
    return -1;
  }
  
  Demonize();
  
  openlog("syncd", LOG_PID, LOG_DAEMON);
  syslog(LOG_NOTICE, "Sync daemon started\n");
  
  struct sigaction sa;
  sa.sa_handler = handle_SIGUSR1;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGUSR1, &sa, NULL); 
  
  while(1){
    syslog(LOG_INFO, "Daemon sleeping\n");
    
    struct timespec ts = {tSleep, 0};   
    nanosleep(&ts, NULL);
    
    if (wakeup) {
      syslog(LOG_INFO, "Woke up by SIGUSR1");
      wakeup = 0;
    } else {
      syslog(LOG_INFO, "Woke up naturally");
    }

    //wywołajcie funkcję sync z argumentami path, recursive 
  }

  syslog(LOG_NOTICE, "Sync daemon terminated\n");
  closelog();

  return 0;
}
