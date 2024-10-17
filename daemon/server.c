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

int main(int argc, char **argv) {
    daemonize();  // Daemonize the process

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
    int saved_stdout = dup(STDOUT_FILENO);  // Save original stdout
    dup2(psd, STDOUT_FILENO);               // Redirect stdout to client socket

    // Execute the command using the process_cmd function
    process_cmd(cmd);

    // Ensure output is flushed to the client
    fflush(stdout);

    // Restore the original stdout descriptor
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);  // Close the duplicated descriptor
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
            update_log_file(client.from, buf);
            run_yash(client.sock, buf);  // Execute the command and send output to client
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
