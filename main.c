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
#include <utime.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/mman.h>

typedef enum file_type
{
  FT_NONE,
  FT_DIRECTORY,
  FT_REGULAR,
  FT_OTHER
} file_type;

#define PATH_MAX_P 200
#define DEFAULT_MMAP_THRESHOLD (512 * 1024)

volatile sig_atomic_t wakeup = 0;

char global_buffer[2048];

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

file_type get_file_type(const char *path){
  struct stat st;
  if(stat(path, &st) != 0)
    return FT_NONE;
  if(S_ISDIR(st.st_mode))
    return FT_DIRECTORY;
  if(S_ISREG(st.st_mode))
    return FT_REGULAR;
  return FT_OTHER;
}

/*time_t get_access_time(const char *path){
  struct stat st_acc;
  if(stat(path, &st_acc) == -1){
    printf("Could not access file at path to get access time: %s\n", path);
    return -1;
  }
  returnbo  st_acc.st_atime;
}

time_t get_modification_time(const char *path){
  struct stat st_modif;
  if(stat(path, &st_modif) == -1){
    printf("Could not access file at path to get modification time: %s\n", path);
    return -1;
  }
  return st_modif.st_mtime;
}

int set_modification_time(const char *path, time_t modification_time){
  struct utimbuf u;
  u.modtime = modification_time;
  u.actime = get_access_time(path);
    return utime(path, &u);
}*/

void msg(const char *message){
  time_t now = time(NULL);
  char ts[31];
  strftime(ts, sizeof(ts), "%Y/%m/%d %H:%M:%S", localtime(&now));
  printf("%s %s\n", ts, message);
  return;
}

void copy_file(const char *src, const char *dst){
  int src_file, dst_file;
  ssize_t b_read, b_written;

  src_file = open(src, O_RDONLY);
  if(src_file == -1){
    snprintf(global_buffer, sizeof(global_buffer), "Could not open source file at path during copying: %s", src);
    syslog(LOG_INFO, "%s", global_buffer);
    return;
  }
  dst_file = open(dst, O_WRONLY | O_TRUNC | O_CREAT, 0644);
  if(dst_file == -1){
    snprintf(global_buffer, sizeof(global_buffer), "Could not open destiantion file at path during copying: %s", dst);
    syslog(LOG_INFO, "%s", global_buffer);
    close(src_file);
    return;
  }

  int buf_size = 16384;
  char *buffer = (char*)malloc(buf_size);

  while((b_read = read(src_file, buffer,  buf_size)) > 0){
    b_written = write(dst_file, buffer, (ssize_t)b_read);
    if(b_read != b_written)
    {
      close(src_file);
      close(dst_file);
      free(buffer);
      snprintf(global_buffer, sizeof(global_buffer), "Error ocured during copying file from %s to %s", src, dst);
      syslog(LOG_INFO, "%s", global_buffer);
      return;
    }
  }

  close(src_file);
  close(dst_file);
  free(buffer);
  snprintf(global_buffer, sizeof(global_buffer), "Succesfully copied file from %s to %s", src, dst);
  syslog(LOG_INFO, "%s", global_buffer);
  return;
}

void copy_directory(const char *src, const char *dst){
  if(mkdir(dst, 0755) != 0){
    snprintf(global_buffer, sizeof(global_buffer), "Could not crate directory at %s", dst);
    syslog(LOG_INFO, "%s", global_buffer);
    return;
  }
  snprintf(global_buffer, sizeof(global_buffer), "Directory created at %s", dst);
  syslog(LOG_INFO, "%s", global_buffer);

  DIR *src_directory = opendir(src);
  if(src_directory == NULL){
    snprintf(global_buffer, sizeof(global_buffer), "Failed opening diresctory %s", src);
    syslog(LOG_INFO, "%s", global_buffer);
    return;
  }

  struct dirent *src_entry;
  while((src_entry = readdir(src_directory)) != NULL){
    if(strcmp(src_entry->d_name, ".") == 0 || strcmp(src_entry->d_name, "..") == 0)
      continue;

    char dst_entry_path[PATH_MAX], src_entry_path[PATH_MAX];
    snprintf(src_entry_path, sizeof(src_entry_path), "%s/%s", src, src_entry->d_name); 
    snprintf(dst_entry_path, sizeof(dst_entry_path), "%s/%s", dst, src_entry->d_name); 

    if (src_entry->d_type == DT_DIR)
      copy_directory(src_entry_path, dst_entry_path);
    else if (src_entry->d_type == DT_REG)
      copy_file(src_entry_path, dst_entry_path);
  }
  closedir(src_directory);
}


int remove_directory(const char *path)
{
  DIR *dir = opendir(path); 
  if (dir == NULL) return -1;

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL){
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    char ent_path[PATH_MAX];
    snprintf(ent_path, sizeof(ent_path), "%s/%s", path, entry->d_name);

    switch (entry->d_type)
    {
      case DT_DIR: 
        {
          int res = remove_directory(ent_path);
          if (res != 0){
            closedir(dir);
            return res;
          }
          snprintf(global_buffer, sizeof(global_buffer), "Directory removed: %s", ent_path);
          syslog(LOG_INFO, "%s", global_buffer);
        }
        break;
      case DT_REG:
        if (remove(ent_path) == 0) {
          snprintf(global_buffer, sizeof(global_buffer), "File removed: %s", ent_path);
          syslog(LOG_INFO, "%s", global_buffer);
        }
        else
        {
          closedir(dir);
          return -3;
        }
        break;
      default:
        snprintf(global_buffer, sizeof(global_buffer), "Unsupported item type: %s", ent_path);
        syslog(LOG_INFO, "%s", global_buffer);
        closedir(dir);
        return -2;
    }
  }
  closedir(dir);
  if (remove(path) != 0) 
    return -3;
  
  snprintf(global_buffer, sizeof(global_buffer), "Directory removed: %s", path);
  syslog(LOG_INFO, "%s", global_buffer);

  return 0;
}

static void copy_file_smart(const char *src, const char *dst, off_t threshold)
{
    struct stat st;
    if (stat(src, &st) != 0) {
        snprintf(global_buffer, sizeof(global_buffer),
                 "stat failed: %s", src);
        syslog(LOG_ERR, "%s", global_buffer);
        return;
    }

    int src_fd = open(src, O_RDONLY);
    if (src_fd == -1) {
        snprintf(global_buffer, sizeof(global_buffer),
                 "Cannot open source: %s", src);
        syslog(LOG_ERR, "%s", global_buffer);
        return;
    }

    int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd == -1) {
        snprintf(global_buffer, sizeof(global_buffer),
                 "Cannot open destination: %s", dst);
        syslog(LOG_ERR, "%s", global_buffer);
        close(src_fd);
        return;
    }

    int ok;
    if (st.st_size > threshold) {
        void *mapped = mmap(NULL, (size_t)st.st_size, PROT_READ,
                            MAP_PRIVATE, src_fd, 0);
        if (mapped == MAP_FAILED) {
            syslog(LOG_ERR, "mmap failed: %s", src);
            close(src_fd); close(dst_fd);
            return;
        }
        ok = (write(dst_fd, mapped, (size_t)st.st_size) == st.st_size);
        munmap(mapped, (size_t)st.st_size);
        if (ok)
            syslog(LOG_INFO, "Copied (mmap): %s -> %s", src, dst);
        else
            syslog(LOG_ERR, "mmap write incomplete: %s -> %s", src, dst);
    } else {
        char buf[16384];
        ssize_t n;
        ok = 1;
        while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
            if (write(dst_fd, buf, (size_t)n) != n) {
                ok = 0;
                break;
            }
        }
        if (n < 0) ok = 0;
        if (ok)
            syslog(LOG_INFO, "Copied (read/write): %s -> %s", src, dst);
        else
            syslog(LOG_ERR, "Copy failed: %s -> %s", src, dst);
    }

    if (ok) {
        struct timespec times[2] = { st.st_atim, st.st_mtim };
        if (futimens(dst_fd, times) != 0)
            syslog(LOG_ERR, "futimens failed: %s", dst);
    }

    close(src_fd);
    close(dst_fd);
}

static void remove_file(const char *path)
{
    if (unlink(path) == 0)
        syslog(LOG_INFO, "Removed file: %s", path);
    else
        syslog(LOG_ERR, "Failed to remove file: %s", path);
}

static void compare_dirs(const char *src, const char *dst,
                         bool recursive, off_t threshold)
{
    DIR *src_dir = opendir(src);
    if (!src_dir) {
        syslog(LOG_ERR, "Cannot open source dir: %s", src);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(src_dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 ||
            strcmp(ent->d_name, "..") == 0) continue;

        char src_path[PATH_MAX], dst_path[PATH_MAX];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, ent->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, ent->d_name);

        struct stat src_st;
        if (lstat(src_path, &src_st) != 0) continue;

        if (S_ISREG(src_st.st_mode)) {
            struct stat dst_st;
            bool dst_exists = (lstat(dst_path, &dst_st) == 0 &&
                                S_ISREG(dst_st.st_mode));

            if (!dst_exists) {
                syslog(LOG_INFO, "New file: %s", src_path);
                copy_file_smart(src_path, dst_path, threshold);
            } else if (src_st.st_mtime > dst_st.st_mtime) {
                syslog(LOG_INFO, "Updated file: %s", src_path);
                copy_file_smart(src_path, dst_path, threshold);
            }

        } else if (S_ISDIR(src_st.st_mode) && recursive) {
            struct stat dst_st;
            if (lstat(dst_path, &dst_st) != 0) {
                if (mkdir(dst_path, 0755) == 0)
                    syslog(LOG_INFO, "Created dir: %s", dst_path);
                else {
                    syslog(LOG_ERR, "mkdir failed: %s", dst_path);
                    continue;
                }
            }
            compare_dirs(src_path, dst_path, true, threshold);
        }
    }
    closedir(src_dir);
}

static void remove_extras(const char *src, const char *dst, bool recursive)
{
    DIR *dst_dir = opendir(dst);
    if (!dst_dir) {
        syslog(LOG_ERR, "Cannot open dest dir: %s", dst);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dst_dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 ||
            strcmp(ent->d_name, "..") == 0) continue;

        char src_path[PATH_MAX], dst_path[PATH_MAX];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, ent->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, ent->d_name);

        struct stat src_st;
        bool in_src = (lstat(src_path, &src_st) == 0);

        struct stat dst_st;
        if (lstat(dst_path, &dst_st) != 0) continue;

        if (S_ISREG(dst_st.st_mode)) {
            if (!in_src || !S_ISREG(src_st.st_mode))
                remove_file(dst_path);
        } else if (S_ISDIR(dst_st.st_mode) && recursive) {
            if (!in_src || !S_ISDIR(src_st.st_mode)) {
                if (remove_directory(dst_path) == 0)
                    syslog(LOG_INFO, "Removed dir: %s", dst_path);
                else
                    syslog(LOG_ERR, "Failed to remove dir: %s", dst_path);
            } else {
                remove_extras(src_path, dst_path, true);
            }
        }
    }
    closedir(dst_dir);
}

void sync_dirs(const char *src, const char *dst,
               bool recursive, off_t threshold)
{
    syslog(LOG_INFO, "Sync started: %s -> %s", src, dst);
    remove_extras(src, dst, recursive);
    compare_dirs(src, dst, recursive, threshold);
    syslog(LOG_INFO, "Sync finished: %s -> %s", src, dst);
}

int main(int _argc, char** _argv){
  if(_argc < 3){
    pUse();
    return -1;
  }

  char src[PATH_MAX_P], dst[PATH_MAX_P];
  realpath(_argv[1], src);
  realpath(_argv[2], dst);
  
  uint32_t tSleep = 5 * 60;
  bool recursive = false;
  off_t mmap_threshold = DEFAULT_MMAP_THRESHOLD;
  
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
    else if(_argv[i][1] == 'm'){
       if(++i >= _argc){ pUse(); return -1; }
          mmap_threshold = (off_t)atol(_argv[i]);
       }
  }
  if(tSleep < 10) tSleep = 10;
  
  if(get_file_type(src) != FT_DIRECTORY){
    msg("Source directory does not exist or is not a directory");
    return -1;
  }

  if(get_file_type(dst) != FT_DIRECTORY){
    msg("Destination directory does not exist or is not a directory");
    return -1;
  }
  if(strcmp(src, dst) == 0){  

    msg("Destination directory is the same as source directory");
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

    sync_dirs(src, dst, recursive, mmap_threshold); 
  }

  syslog(LOG_NOTICE, "Sync daemon terminated\n");
  closelog();

  return 0;
}
