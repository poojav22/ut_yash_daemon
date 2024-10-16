#include <stdio.h>
/* socket(), bind(), recv, send */
#include <sys/types.h>
#include <sys/socket.h> /* sockaddr_in */
#include <netinet/in.h> /* inet_addr() */
#include <arpa/inet.h> /* struct hostent */
#include <string.h> /* memset() */
#include <unistd.h> /* close() */
#include <stdlib.h> /* exit() */
#include <signal.h> /* Signal handling */

#define MAXHOSTNAME 80
#define BUFSIZE 1024
#define PORT 3826  // Hardcoded

char buf[BUFSIZE];  // Input buffer
int sd;  // Socket descriptor

void handle_signal(int sig);  // Signal handler function
void GetUserInput();  // Function to handle user input and communication with server

int main(int argc, char **argv) {
    struct sockaddr_in server;
    struct sockaddr_in from;
    struct hostent *hp;
    socklen_t fromlen;
    char ThisHost[MAXHOSTNAME];

    // Set up signal handlers for Ctrl+C and Ctrl+Z
    signal(SIGINT, handle_signal);  // Handle Ctrl+C (SIGINT)
    signal(SIGTSTP, handle_signal); // Handle Ctrl+Z (SIGTSTP)

    // for logging
    gethostname(ThisHost, MAXHOSTNAME); // <for logging>
    printf("----TCP/Client running at host NAME: %s\n", ThisHost);

    // No need to resolve host, we have the server IP from argv[1]
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) {
        perror("opening stream socket");
        exit(-1);
    }

    // Set up server connection
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr(argv[1]);  // IP provided by client

    if (server.sin_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "Invalid IP address: %s\n", argv[1]);
        exit(-1);
    }

    // Connect to the server
    if (connect(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connecting stream socket");
        close(sd);
        exit(-1);
    }

    fromlen = sizeof(from);
    if (getpeername(sd, (struct sockaddr *)&from, &fromlen) < 0) {
        perror("couldn't get peername");
        close(sd);
        exit(-1);
    }

    printf("Connected to TCPserver at IP: %s on port %d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));

    // Call GetUserInput to handle command processing and interaction with the server
    GetUserInput();

    close(sd);  // Close socket when done
    return 0;
}

// Function to handle signal (Ctrl+C or Ctrl+Z)
void handle_signal(int sig) {
    if (sig == SIGINT) {  // Ctrl+C
        printf("\nReceived Ctrl+C, closing connection and exiting...\n");
        close(sd);  // Close the socket
        exit(0);  // Exit the client
    } else if (sig == SIGTSTP) {  // Ctrl+Z
        printf("\nReceived Ctrl+Z, suspending client...\n");
        raise(SIGSTOP);  // Suspend the process
    }
}

void GetUserInput() {
    while (1) {
        printf("\nEnter a command (CMD <command>), a control signal (CTL <d>), or plain text: ");
        fgets(buf, BUFSIZE, stdin);  // Read user input

        // If input starts with CMD, send as a command to the server
        if (strncmp(buf, "CMD", 3) == 0) {
            if (send(sd, buf, strlen(buf), 0) < 0) {
                perror("sending command");
            }
        }
        // If input starts with CTL, handle it accordingly
        else if (strncmp(buf, "CTL", 3) == 0) {
            if (strncmp(buf + 4, "d", 1) == 0) {
                printf("Disconnecting from server...\n");
                close(sd);  // Close the connection
                exit(0);
            } else {
                printf("Unknown control signal. Only 'CTL d' to disconnect is supported.\n");
            }
        }
        // Handle plain text
        else {
            // Send plain text to the server
            if (send(sd, buf, strlen(buf), 0) < 0) {
                perror("sending plain text");
            }
        }
    }
}

