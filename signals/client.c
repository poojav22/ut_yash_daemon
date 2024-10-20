#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#define MAXHOSTNAME 80
#define BUFSIZE 1024
#define PORT 6666

char buf[BUFSIZE];
char rbuf[BUFSIZE];
void GetUserInput();
void cleanup(char *buf);

int childpid;
int rc, cc;
int sd;

// Handle SIGINT (Ctrl+C)
void handle_sigint(int sig) {
    printf("\nCaught signal %d (Ctrl+C), notifying server...\n", sig);
    char msg[BUFSIZE + 4];  // Allocate space for "CTL " 
    snprintf(msg, sizeof(msg), "CTL c\n");  // Format the message with "CTL c"
    int sent = send(sd, msg, strlen(msg), 0);  // Send control signal to the server
    if (sent < 0) {
        perror("Error sending Ctrl+C message to server");
    } else {
        printf("Sent message to server: %s (bytes sent: %d)\n", msg, sent);  // Log the actual sent bytes
    }
}

// Handle SIGTSTP (Ctrl+Z)
void handle_sigtstp(int sig) {
    printf("\nCaught signal %d (Ctrl+Z), notifying server...\n", sig);
    char msg[BUFSIZE + 4];  // Allocate space for "CTL " and signal
    snprintf(msg, sizeof(msg), "CTL z\n");  // Format the message with "CTL z"
    int sent = send(sd, msg, strlen(msg), 0);  // Send control signal to the server
    if (sent < 0) {
        perror("Error sending Ctrl+Z message to server");
    } else {
        printf("Sent message to server: %s (bytes sent: %d)\n", msg, sent);  // Log the actual sent bytes
    }
}

// Main function
int main(int argc, char **argv ) {
    
    struct sockaddr_in server;
    struct sockaddr_in client;
    struct hostent *hp, *gethostbyname();
    struct sockaddr_in from;
    struct sockaddr_in addr;
    int fromlen;
    int length;
    char ThisHost[80];

    // Set up signal handlers in the parent process
    signal(SIGINT, handle_sigint);   // Catch Ctrl+C
    signal(SIGTSTP, handle_sigtstp); // Catch Ctrl+Z

    strcpy(ThisHost, "localhost");

    if  ( (hp = gethostbyname(ThisHost)) == NULL ) {
        fprintf(stderr, "Can't find host %s\n", argv[1]);
        exit(-1);
    }
    bcopy(hp->h_addr, &(server.sin_addr), hp->h_length);

    if  ( (hp = gethostbyname(argv[1])) == NULL ) {
        addr.sin_addr.s_addr = inet_addr(argv[1]);
        if ((hp = gethostbyaddr((char *) &addr.sin_addr.s_addr,
                                sizeof(addr.sin_addr.s_addr),AF_INET)) == NULL) {
            fprintf(stderr, "Can't find host %s\n", argv[1]);
            exit(-1);
        }
    }
    bcopy(hp->h_addr, &(server.sin_addr), hp->h_length);

    server.sin_family = AF_INET; 
    server.sin_port = htons(PORT);
    sd = socket(AF_INET, SOCK_STREAM, 0);

    if (sd < 0) {
        perror("opening stream socket");
        exit(-1);
    }

    if (connect(sd, (struct sockaddr *) &server, sizeof(server)) < 0) {
        close(sd);
        perror("connecting stream socket");
        exit(0);
    }

    fromlen = sizeof(from);
    if (getpeername(sd, (struct sockaddr *)&from, &fromlen) < 0) {
        perror("couldn't get peername\n");
        exit(1);
    }
    
    printf("yash <%s>\n", inet_ntoa(from.sin_addr));
    printf("# ");
    fflush(stdout);

    // Fork to create child process for user input
    childpid = fork();
    if (childpid == 0) {
        // Child process ignores both SIGINT and SIGTSTP
        signal(SIGINT, SIG_IGN); 
        signal(SIGTSTP, SIG_IGN);
        GetUserInput();  // Child handles user input
    }

    // Parent process handles server responses
    for (;;) {
        cleanup(rbuf);
        if ((rc = recv(sd, rbuf, sizeof(buf), 0)) < 0) {
            perror("receiving stream message");
            exit(-1);
        }
        if (rc > 0) {
            rbuf[rc] = '\0';
            printf("%s", rbuf);
            fflush(stdout);
        } else {
            printf("Disconnected..\n");
            close(sd);
            exit(0);
        }
    }
}

void cleanup(char *buf) {
    int i;
    for (i = 0; i < BUFSIZE; i++) buf[i] = '\0';
}

// This function automatically prepends "CMD " to commands and handles sending control signals
void GetUserInput() {
    while (1) {
        cleanup(buf);  // Clear buffer
        fgets(buf, BUFSIZE, stdin);  // Read user input
        //read(buf, BUFSIZE, stdin);

        // If user input is just plain text (a command), prepend "CMD "
        if (strncmp(buf, "quit", 4) == 0) {  // If user types quit, send CTL d
            char msg[BUFSIZE + 4];
            snprintf(msg, sizeof(msg), "CTL d\n");  // Prepend "CTL d" to quit message
            send(sd, msg, strlen(msg), 0);  // Notify server about disconnection
            break;
        }

        // Prepend "CMD " to regular user commands
        char cmd[BUFSIZE + 4];  // Extra space for "CMD " prefix
        snprintf(cmd, sizeof(cmd), "CMD %s", buf);  // Prepend "CMD " to the input

        // Send the command with "CMD " prepended
        if (send(sd, cmd, strlen(cmd), 0) < 0) {
            perror("sending command");
        }
    }
    printf("EOF... exit\n");
    close(sd);
    kill(getppid(), SIGTERM);  // Gracefully terminate the parent process
    exit(0);
}
