#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_CMD_SIZE 256
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
job_list = (struct job *) malloc(MAX_CMD_SIZE * sizeof(struct job));
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

int main1() {

char *cmd = malloc(MAX_CMD_SIZE);



while (1) {
  //sleep(3);
//printf("# ");


//fgets(cmd, MAX_CMD_SIZE, stdin);

cmd = readline("# ");
//printf("Command is: %s\n", cmd);

process_cmd(cmd);
}
return 0;

}
