#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <time.h>

#define MAX_CMD_SIZE 256
#define MAXHOSTNAME 80
#define LOG_FILE "/tmp/server_log.txt"
#define PID_FILE "/tmp/server.pid"
#define PORT 6666
#define NUM_CLIENTS 50

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
FILE *logFile;

struct ClInfo {
    int id;
    int sock;
    struct sockaddr_in from;
};

void reusePort(int sock);
void *EchoServe(void *input);
void process_cmd(char* buf);
void run_yash(int psd, char *cmd);
void daemonize();
int lock_pid_file();
void update_log_file(struct sockaddr_in from, char *cmd);
static void sig_handler(int signo);

pid_t chid1, chid2;
int fd1, fd2;
int status;
int pfd[2], lpid, rpid, wpid;
int wait_cnt = 0;
int count = 0;
typedef struct job
{
  char *job_name;
  pid_t pgid;
  int index;
  char* status;
} job;

job *job_list;

char** str_handler(char* cmd, char* delimiter)
{
    char *str1, *str2, *token, *subtoken;
    char *saveptr1, *saveptr2;
    int j, i=0;
    char **array = (char**)malloc(10 * sizeof(char*));

    //printf("String: %s\n", cmd);
    //printf("Splitting based on '%s'\n", delimiter);
    for (j = 1, str1 = cmd; ; j++, str1 = NULL) {
        token = strtok_r(str1, delimiter, &saveptr1);
        if (token == NULL)
            break;
        //printf("%d: token %s\n", j, token);
        array[i++] = token;
    }
   return array;
}

char **get_tokens(char *cmd) {
    char **result = malloc(64 * sizeof(char *));
    char *token;
    int i = 0;

    token = strtok(cmd, " ");
    while (token != NULL) {
        result[i++] = token;
        token = strtok(NULL, " ");
    }
    result[i] = NULL;
    return result;
}

static void sig_handler(int signo) {
  switch(signo){
  case SIGINT:
  //printf("Sending SIGINT to group:%d\n",chid1);
    printf("caught SIGINT\n# ");
    kill(-chid1, SIGINT);
    break;
  case SIGTSTP:
  //printf("Sending SIGTSTP to group:%d\n",chid1);
    printf("caught SIGTSTP\n# ");
    kill(-chid1, SIGTSTP);
  break;
  }

}

void check_input_redirection(char** tokens) {

char* file_name = NULL;
int i = 0;
while (tokens[i] != NULL) {
  //printf("Tokens here: %s\n", tokens[i]);
  if (strcmp(tokens[i], "<") == 0) {
        if (i==0) {
      printf("Error: Invalid command\n");
      exit(1);
    }
      file_name = tokens[i+1];
      //printf("Filename here %s", file_name);
      tokens[i] = NULL;
      tokens[i+1] = NULL;
      break;
  }
  i++;
}
  if (file_name != NULL) {
  fd1 = open(file_name, O_RDONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
  //printf("file descriptor: %d\n", fd1);
  if (fd1 == -1) {
      fprintf(stderr,"can't open %s\n", file_name);
  } else {
      //printf("Reading from %s", file_name);
      //printf("Dup2 %d", fd1);
      dup2(fd1,STDIN_FILENO);
      close(fd1);
  }
}
}


void check_output_redirection(char** tokens) {
char* file_name = NULL;
int i = 0;
while (tokens[i] != NULL) {
  if (strcmp(tokens[i], ">") == 0) {
    if (i==0) {
      printf("Error: Invalid command\n");
      exit(1);
    }
      file_name = tokens[i+1];
      //printf("FIlename here %s", file_name);
      tokens[i] = NULL;
      tokens[i+1] = NULL;
      break;
  }
  i++;
}
if (file_name != NULL) {
  fd2 = open(file_name, O_RDWR | O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
  //printf("file descriptor: %d\n", fd2);
  if (fd2 == -1) {
      fprintf(stderr,"can't create %s\n", file_name);
  } else {
      //printf("Writing to %s", file_name);
      //printf("Dup2 %d", fd2);
      dup2(fd2,STDOUT_FILENO);
      close(fd2);
  }
}
}


void execute_cmd(char **tokens) {
  //printf("Executing %s", tokens[0]);
  execvp(tokens[0], tokens);
}

void print_job (job *job_list)
{
  for (int i=1; i<count; i++) {
    printf ("[%d]-    %s    %s\n", job_list[i].index, job_list[i].status, job_list[i].job_name);
  }
  if (count != 0) {
  printf ("[%d]+    %s    %s\n", job_list[count].index, job_list[count].status, job_list[count].job_name);
  }
}

void add_to_joblist(char* cmd, pid_t sid) {
count++;
//printf("Adding job %d to index %d\n", sid, count);
job_list[count].job_name = malloc(strlen(cmd) + 1);
strcpy(job_list[count].job_name, cmd);
job_list[count].pgid = sid;
job_list[count].index = count;
job_list[count].status = "Stopped";

}


int update_jobstatus(pid_t pid, char *stat) {
    int i = 0;
    int found = 0;
  for (i=1; i<=count; i++) {
    if (job_list[i].pgid == pid) {
        //printf("Job found in job list %d total %d", i, count );
      found = 1;
      break;
    }
  }
  if (found == 1) {
    //printf("Updating job %d %s to %s\n", i, job_list[i].status, stat);
    job_list[i].status = stat;
    return i;
  } else {
    return -1;
  }
    
}

void put_job_in_background (char *cmd, char **tokens, pid_t chid1)
{
  add_to_joblist(cmd, chid1);
    if (kill (-chid1, SIGCONT) < 0)
      perror ("kill (SIGCONT)");
    update_jobstatus(chid1, "Running");
}

void bg_cmd_process1() {
    if (kill (-job_list[count].pgid, SIGCONT) < 0)
      perror ("kill (SIGCONT)");
    //printf("Job %d running in background!!\n", job_list[count].pgid);
    update_jobstatus(job_list[count].pgid, "Running");
}

void check_bg_process(char **tokens, pid_t chid1){
  int i = 0;
  while (tokens[i] != NULL) {
  if (strcmp(tokens[i], "&") == 0) {
      tokens[i] = NULL;
      break;
  }
  i++;
}

}

void fg_cmd_process1() {

  //printf("Bringing %d job to foreground", job_list[count].pgid);
  tcsetpgrp (STDIN_FILENO, job_list[count].pgid);
    if (kill (-job_list[count].pgid, SIGCONT) < 0)
      perror ("kill (SIGCONT)");
    //printf("Job %d running in foreground!!\n", job_list[count].pgid);
}

void update_job_list() {
    int i;
    for (i=1; i<=count; i++) {
      if (strcmp(job_list[i].status, "Done") ==0) {
        //printf("removing done job from list %d\n", i);
        break;
    }
    }
    if(i <=count){
        for(int x=i; x<=count; x++) {
          //printf("Updating element %d total %d\n", x, count);
          job_list[x] = job_list[x+1];
        }
        count--;
    }
}

void process_wait_status(int status, char *cmd, pid_t wpid) {
        if (WIFEXITED(status)) {
        //printf("child %d exited, status=%d\n", wpid, WEXITSTATUS(status));
        int id = update_jobstatus(wpid, "Done");
        if (id>0) {
        printf ("[%d]   %s    %s\n", job_list[id].index, job_list[id].status, job_list[id].job_name);
        wait_cnt++;
        }
        update_job_list();
        //printf("Job update done");
      } else if (WIFSIGNALED(status)) {
        //printf("child %d killed by signal %d\n", wpid, WTERMSIG(status));
      } else if (WIFSTOPPED(status)) {
        //printf("%d stopped by signal %d\n", wpid, WSTOPSIG(status));
        //printf("Adding %s to job list. Count: %d\n", cmd, count);
        add_to_joblist(cmd, wpid);
      } else if (WIFCONTINUED(status)) {
        //printf("Continuing %d\n",wpid);
        update_jobstatus(wpid, "Running");
        kill(wpid,SIGCONT);
      }
}

void fg_cmd_process(pid_t sid) {
    if (count <= 0) {
        printf("No jobs to bring to foreground.\n");
        return;
    }

    struct job fg_job = job_list[count];
    //printf("Bringing job %d to foreground\n", fg_job.pgid);
    //tcsetpgrp(STDOUT_FILENO, fg_job.pgid);

    if (kill(-fg_job.pgid, SIGCONT) < 0) {
        perror("kill (SIGCONT)");
    } else {
        //printf("Job %d running in foreground\n", fg_job.pgid);
    }


    int status;
    waitpid(-fg_job.pgid, &status, WUNTRACED);

    //tcsetpgrp(STDOUT_FILENO, getpid());
    //printf("Set terminal control to %d", getpgrp());
    int id;
    if (WIFEXITED(status)) {
        //printf("Job %d exited with status %d\n", fg_job.pgid, WEXITSTATUS(status));
        id = update_jobstatus(fg_job.pgid, "Done");
        if (id>0) {
        printf ("[%d]   %s    %s\n", job_list[id].index, job_list[id].status, job_list[id].job_name);
        }
    } else if (WIFSIGNALED(status)) {
        //printf("Job %d killed by signal %d\n", fg_job.pgid, WTERMSIG(status));
        id = update_jobstatus(fg_job.pgid, "Done");
        if (id>0) {
        printf ("[%d]   %s    %s\n", job_list[id].index, job_list[id].status, job_list[id].job_name);
        }
    } else if (WIFSTOPPED(status)) {
        //printf("Job %d stopped by signal %d\n", fg_job.pgid, WSTOPSIG(status));
        update_jobstatus(fg_job.pgid, "Stopped");
    }
    //printf("fg cmd done");
}

void bg_cmd_process(pid_t sid) {
    if (count <= 0) {
        printf("No jobs to run in background.\n");
        return;
    }

    struct job bg_job = job_list[count];

    //printf("Bringing job %d to background\n", bg_job.pgid);

    if (kill(bg_job.pgid, SIGCONT) < 0) {
        perror("kill (SIGCONT)");
    } else {
        //printf("Job %d running in background\n", bg_job.pgid);
        update_jobstatus(bg_job.pgid, "Running");
    }
}

void process_cmd(char* cmd) {
  int i = 0;
int sync_pipe[2];
char *pcmd;
char **tokens;
//job_list = (struct job *) malloc(MAX_CMD_SIZE * sizeof(struct job));
int is_foreground = 0, is_background=0;
wait_cnt = 0;

if (cmd == NULL || strcmp(cmd, "c\n")==0) {
  //printf("Exiting shell...");
  exit(0);
}
if (strcmp(cmd, "\n") == 0) {
  return;
}


//setsid();
pid_t sid = getpid();
//printf("Parent pid %d\n", sid);
cmd[strcspn(cmd, "\n")] = '\0';

if (strcmp(cmd, "") == 0) {
  return;
}

is_background = 0;
int len = strlen(cmd);
if (cmd[len - 1] == '&') {
    is_background = 1;
    cmd[len - 1] = '\0'; // Remove the '&' from the command
}


char **pipe_cmds = str_handler(cmd, "|");

pipe(pfd);
pipe(sync_pipe);

if (strcmp(pipe_cmds[0], "bg") == 0) {
  bg_cmd_process(sid);
  pipe_cmds[0] = " ";

}


if (strcmp(pipe_cmds[0], "fg") == 0) {
  fg_cmd_process(sid);
  //is_foreground = 1;
  pipe_cmds[0] = NULL;

}

if (pipe_cmds[0] != NULL) {
  wait_cnt ++;
  //printf("Processing command: %s\n", pipe_cmds[0]);
chid1 = fork();

if (chid1 == 0) {
  chid1 = getpid();
  //printf(" child process1: %d\n", chid1);
//setsid();
setpgid(0,0);
signal (SIGCHLD, SIG_DFL);


char **tokens = get_tokens(pipe_cmds[0]);
check_output_redirection(tokens);
check_input_redirection(tokens);


if (strcmp(tokens[0], "jobs") == 0) {
  print_job(job_list);
  exit(0);
}

if (pipe_cmds[1] != NULL) {
  //printf("Inside pipe condition");
  close(pfd[0]);
  dup2(pfd[1],STDOUT_FILENO);
} else {
  close(pfd[1]);
}

close(sync_pipe[0]);
write(sync_pipe[1], "1", 1);
close(sync_pipe[1]);
execute_cmd(tokens);

}
}

if (pipe_cmds[1] != NULL) {
    wait_cnt++;
  chid2 = fork();
  if (chid2 == 0) {

    setpgid(0, chid1);
    //printf("In child process 2 now: %d\n", chid2);
    close(pfd[1]);
    dup2(pfd[0],STDIN_FILENO);
    char **tokens1 = get_tokens(pipe_cmds[1]);
    check_output_redirection(tokens1);
    check_input_redirection(tokens1);
    execvp(tokens1[0], tokens1);
    //printf("Done child process 2: %d\n", chid2);
    }

}
//Parent
 close(sync_pipe[1]); // Close write end of the pipe
    char buf;
    // Wait for the signal from the child
    read(sync_pipe[0], &buf, 1);
    close(sync_pipe[0]);
    if (signal(SIGINT, sig_handler) == SIG_ERR) {
        printf("Error processing signal");
   }
    if (signal(SIGTSTP, sig_handler) == SIG_ERR) {
        printf("Error processing signal");
    }
    close(pfd[0]);
    close(pfd[1]);
    //printf("closed and waiting\n");


if (is_background == 1) {
    //setpgid(0, chid1);
    //  printf("Sending SIGTSTP to %d", chid1);
    //if (kill (-chid1, SIGTSTP) < 0)
    //  perror ("kill (SIGTSTP)");
    //add_to_joblist(cmd, chid1);
    add_to_joblist(cmd, chid1);
    //printf("Sending SIGCONT to %d", chid1);
    if (kill (-chid1, SIGCONT) < 0)
      perror ("kill (SIGCONT)");
    update_jobstatus(chid1, "Running");
    is_background = 0;
}

if (is_foreground == 1) {
  wpid = waitpid(job_list[count].pgid, &status, WUNTRACED);
  process_wait_status(status, job_list[count].job_name, wpid);
    is_foreground = 0;
    //tcsetpgrp(STDIN_FILENO, getpgrp());
    tcsetpgrp (1, job_list[count].pgid);
    wpid = waitpid(job_list[count].pgid, &status, WUNTRACED | WCONTINUED);
    process_wait_status(status, job_list[count].job_name, wpid);
    //tcsetpgrp(1, getpgrp());
    return;
}


    for(i = 0; i<wait_cnt; i++) {
      wpid = waitpid(-1, &status, WUNTRACED | WCONTINUED);
      //printf("wait done PID: %d\n", wpid);
      if (wpid == -1) {
        perror("waitpid");
        exit(EXIT_FAILURE);
      }
      process_wait_status(status, cmd, wpid);
    }
}

/*
void sig_handler(int signo) {
  switch(signo){
  case SIGCHLD:
  //printf("Sending SIGINT to group:%d\n",chid1);
    printf("caught SIGCHLD\n# ");
    //kill(-chid1, SIGINT);
    break;

}
}
*/

int main(int argc, char **argv) {
    //daemonize();  // Daemonize the process

    // Lock the PID file to ensure only one instance is running
    if (lock_pid_file() != 0) {
        syslog(LOG_ERR, "Another instance of the server is already running.");
        exit(EXIT_FAILURE);
    }



    int sd, psd;
    struct sockaddr_in server, from;
    socklen_t fromlen;
    struct ClInfo client;
    pthread_t th[NUM_CLIENTS];
    int i = 0;

    syslog(LOG_INFO, "Starting server daemon");

    // Open log file for appending communications
    logFile = fopen(LOG_FILE, "a");
    if (!logFile) {
        syslog(LOG_ERR, "Failed to open log file");
        exit(EXIT_FAILURE);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    // Create socket on which to send and receive
    sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sd < 0) {
        syslog(LOG_ERR, "Error creating socket");
        exit(EXIT_FAILURE);
    }

    reusePort(sd);

    // Bind socket
    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        syslog(LOG_ERR, "Error binding socket");
        close(sd);
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Server bound to port %d", PORT);

    // Accept TCP connections from clients
    listen(sd, 40);
    fromlen = sizeof(from);

    for (;;) {
    //if (signal(SIGCHLD, sig_handler) == SIG_ERR) {
    //    printf("Error processing signal");
    //}
        psd = accept(sd, (struct sockaddr *)&from, &fromlen);
        if (psd < 0) {
            syslog(LOG_ERR, "Accept failed");
            continue;
        }

        client.sock = psd;
        client.from = from;
        client.id = i + 1;
        struct ClInfo thread_client = client;

        // Log connection info
       // update_log_file(from, "New connection established");

        // Create thread for each client
        if (pthread_create(&th[i++], NULL, EchoServe, (void *)&thread_client) != 0) {
            syslog(LOG_ERR, "Thread creation failed");
            close(psd);
            continue;
        }

        syslog(LOG_INFO, "Thread created for client %s:%d", inet_ntoa(from.sin_addr), ntohs(from.sin_port));

        if (i >= NUM_CLIENTS) {
            i = 0;  // Recycle thread array
        }
    }

    fclose(logFile);
    close(sd);
    return 0;
}

void daemonize() {
    pid_t pid;

    // First fork
    pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "First fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);  // Parent exits
    }

    // Create new session
    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid() failed");
        exit(EXIT_FAILURE);
    }
    syslog(LOG_INFO, "Session created successfully");

    // Ignore SIGHUP signal
    signal(SIGHUP, SIG_IGN);

    // Second fork
    pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Second fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);  // Second parent exits
    }

    // // Change working directory to root
    // if (chdir("/") < 0) {
    //     syslog(LOG_ERR, "chdir() to root failed");
    //     exit(EXIT_FAILURE);
    // }

    // Set file permissions mask
    umask(0);

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect stdin, stdout, stderr to /dev/null
    open("/dev/null", O_RDONLY);  // stdin
    open("/dev/null", O_RDWR);    // stdout
    open("/dev/null", O_RDWR);    // stderr

    syslog(LOG_INFO, "Daemon started successfully");
}

int lock_pid_file() {
    int pid_file_fd;
    char pid_str[10];

    // Open or create the PID file
    pid_file_fd = open(PID_FILE, O_RDWR | O_CREAT, 0640);
    if (pid_file_fd < 0) {
        syslog(LOG_ERR, "Could not open PID file");
        return -1;  // Could not open PID file
    }

    // Try to lock the PID file
    if (lockf(pid_file_fd, F_TLOCK, 0) < 0) {
        syslog(LOG_ERR, "Could not lock PID file, another instance is running");
        return -1;  // Could not lock PID file
    }

    // Write the PID of the current process into the PID file
    sprintf(pid_str, "%d\n", getpid());
    write(pid_file_fd, pid_str, strlen(pid_str));

    return 0;
}

void run_yash(int psd, char *cmd) {
    //printf("Before running yash");
    int saved_stdout = dup(STDOUT_FILENO);  // Save original stdout
    dup2(psd, STDOUT_FILENO);               // Redirect stdout to client socket

    //printf("trying before");
    // Execute the command using the process_cmd function
    process_cmd(cmd);
    //printf("trying to print");
    //printf("# ");
    // Ensure output is flushed to the client
    fflush(stdout);

    // Restore the original stdout descriptor
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);  // Close the duplicated descriptor
    //printf("after running yash");
    
}


void update_log_file(struct sockaddr_in from, char *cmd) {
    char time_now[64];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(time_now, sizeof(time_now), "%b %d %H:%M:%S", tm);

    pthread_mutex_lock(&lock);  // Ensure thread-safe log access
    fprintf(logFile, "%s yashd[%s:%d]: %s\n", time_now, inet_ntoa(from.sin_addr), ntohs(from.sin_port), cmd);
    fflush(logFile);  // Ensure the log is written to disk
    pthread_mutex_unlock(&lock);  // Release lock
}

void *EchoServe(void *input) {
    struct ClInfo client = *(struct ClInfo *)input;
    job_list = (struct job *) malloc(MAX_CMD_SIZE * sizeof(struct job));
    char buf[512], cmd[512];
    int rc;

    // Log client connection
    syslog(LOG_INFO, "Serving %s:%d", inet_ntoa(client.from.sin_addr), ntohs(client.from.sin_port));

    for (;;) {
        syslog(LOG_INFO, "Waiting for client input...");

        if ((rc = recv(client.sock, buf, sizeof(buf), 0)) < 0) {
            syslog(LOG_ERR, "Error receiving message");
            close(client.sock);
            pthread_exit(NULL);  // Exit thread on error
        }

        if (rc > 0) {
            buf[rc] = '\0';
            syslog(LOG_INFO, "Received: %s", buf);
            printf("Received %s\n", buf);
            update_log_file(client.from, buf);
            printf("udpated log file %s\n", buf);
            run_yash(client.sock, buf);  // Execute the command and send output to client
            send(client.sock, "\n# ", 3, 0);
        } else {
            syslog(LOG_INFO, "Client disconnected");
            close(client.sock);
            pthread_exit(NULL);  // Exit thread when client disconnects
        }
    }

    return NULL;
}

void reusePort(int s) {
    int one = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) == -1) {
        syslog(LOG_ERR, "Error setting socket options");
        exit(EXIT_FAILURE);
    }
}
