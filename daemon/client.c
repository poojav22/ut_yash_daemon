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
#define PORT 8080  // Hardcoded

char buf[BUFSIZE];  // Input buffer
int sd;  // Socket descriptor

void handle_signal(int sig);  // Signal handler function
void GetUserInput();  // Function to handle user input and communication with server

int main(int argc, char **argv) {
    struct sockaddr_in server;
    struct sockaddr_in from;
    socklen_t fromlen;
    char ThisHost[MAXHOSTNAME];

    // Set up signal handlers for Ctrl+C and Ctrl+Z
    signal(SIGINT, handle_signal);  // Handle Ctrl+C (SIGINT)
    signal(SIGTSTP, handle_signal); // Handle Ctrl+Z (SIGTSTP)

    // Get host name for logging
    gethostname(ThisHost, MAXHOSTNAME); 
    printf("----TCP/Client running at host NAME: %s\n", ThisHost);

    // Create socket
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

    // Call function to handle communication with the server
    GetUserInput();

    close(sd);  // Close socket when done
    return 0;
}

// Signal handler function
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
        // Step 1: Receive the prompt from the server first
        int rc = recv(sd, buf, BUFSIZE, 0);
        if (rc < 0) {
            perror("Error receiving prompt from server");
            break;
        } else if (rc == 0) {
            printf("Server closed connection.\n");
            close(sd);
            exit(0);
        } else {
            buf[rc] = '\0';  // Null-terminate the received prompt
            printf("%s", buf);  // Display the server's prompt
        }

        // Step 2: Ask the user for input after showing the prompt
        printf("Enter a command or plain text: ");
        fgets(buf, BUFSIZE, stdin);  // Get user input

        // Step 3: Send the input to the server
        if (send(sd, buf, strlen(buf), 0) < 0) {
            perror("Error sending input to server");
            break;
        }

        // Step 4: Receive the server's response
        rc = recv(sd, buf, BUFSIZE, 0);
        if (rc < 0) {
            perror("Error receiving response from server");
            break;
        } else if (rc == 0) {
            printf("Server closed connection.\n");
            close(sd);
            exit(0);
        } else {
            buf[rc] = '\0';  // Null-terminate the received string
            printf("Server response: %s\n", buf);  // Display the server's response
        }
    }
}


// void GetUserInput() {
//     while (1) {
//         // Step 1: Receive the prompt from the server
//         int rc = recv(sd, buf, BUFSIZE - 1, 0);  // Ensure enough space for null-terminator
//         if (rc < 0) {
//             perror("Error receiving prompt from server");
//             break;
//         } else if (rc == 0) {
//             printf("Server closed connection.\n");
//             close(sd);
//             exit(0);
//         } else {
//             buf[rc] = '\0';  // Null-terminate the received string
//             printf("%s", buf);  // Display the server's prompt
//         }

//         // Step 2: Ask the user for input
//         printf("Enter a command (CMD <command>) or plain text: ");
//         fgets(buf, BUFSIZE, stdin);  // Read user input

//         // Step 3: Send the input to the server
//         if (send(sd, buf, strlen(buf), 0) < 0) {
//             perror("Error sending message to server");
//             break;
//         }

//         // Step 4: Receive the server's response after sending the message
//         rc = recv(sd, buf, BUFSIZE - 1, 0);  // Ensure enough space for null-terminator
//         if (rc < 0) {
//             perror("Error receiving response from server");
//             break;
//         } else if (rc == 0) {
//             printf("Server closed connection.\n");
//             close(sd);
//             exit(0);
//         } else {
//             buf[rc] = '\0';  // Null-terminate the received string
//             printf("Server response: %s\n", buf);  // Display the response
//         }
//     }
// }

