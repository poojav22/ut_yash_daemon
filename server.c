#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define PORT 3826  // Hardcoded port
#define MAXHOSTNAME 80
#define BUFSIZE 1024

void reusePort(int sock);
void *EchoServe(void *input);
struct ClInfo{
    int sock;
    struct sockaddr_in from;
};

FILE *logFile; 

int main(int argc, char **argv) {
    int sd, psd;
    struct sockaddr_in server;
    struct sockaddr_in from;
    struct ClInfo client;
    socklen_t fromlen;
    char ThisHost[MAXHOSTNAME];
    char buf[BUFSIZE];
    int NUM_CLIENTS = 10;
    pthread_t th[NUM_CLIENTS]; 
    int i = 0;

    /* Open log file for appending communications */
    logFile = fopen("/tmp/server_log.txt", "a");
    if (!logFile) {
        perror("Failed to open log file");
        exit(-1);
    }

    /* get TCPServer Host information, NAME and INET ADDRESS */
    gethostname(ThisHost, MAXHOSTNAME);
    printf("----TCP/Server running at host NAME: %s\n", ThisHost);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY); 
    server.sin_port = htons(PORT); 


     /** Create socket on which to send and receive */
    sd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP); 
    if (sd < 0) {
        perror("binding name to stream socket");
        exit(-1);
    }

    /** this allows the server to re-start quickly instead of waiting
    for TIME_WAIT which can be as large as 2 minutes */
    reusePort(sd);

    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("binding name to stream socket");
        close(sd);
        exit(-1);
    }

    /** accept TCP connections from clients and fork a process to serve each */
    listen(sd, 4);
    fromlen = sizeof(from);

    for (;;) {
        psd = accept(sd, (struct sockaddr *)&from, &fromlen);
        if (psd < 0){
            perror("accept failure");
            continue;
        }

        client.sock = psd;
        client.from = from;

       /* Log connection info */
        fprintf(logFile, "New connection from %s:%d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
        fflush(logFile);  // Flush log to ensure it's written

        struct ClInfo thread_client = client; // Store client info for the thread
        // new thread for every client
        if (pthread_create(&th[i++], NULL, EchoServe, (void *) &thread_client) != 0) {
            perror("Thread creation failed");
            close(psd);
            continue;
        }

        printf("Thread created for client %s:%d (Thread ID: %lu)\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port), (unsigned long)th[i]);
        fprintf(logFile, "Thread created for client %s:%d (Thread ID: %lu)\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port), (unsigned long)th[i]);
        fflush(logFile);

        if (i >= NUM_CLIENTS) {
            i = 0;  // Recycle thread array
        }
    }

    /* Close log file and socket */
    fclose(logFile);
    close(sd);
    return 0;  // Add the missing return and closing brace for main()
}

void *EchoServe(void *input) {
    struct ClInfo client =  *(struct ClInfo *)input;
    char buf[BUFSIZE];
    int rc;

    fprintf(logFile, "Serving client %s:%d\n", inet_ntoa(client.from.sin_addr),
            ntohs(client.from.sin_port));
    fflush(logFile);

    for (;;) {
        printf("\n...server is waiting...\n");
        if ((rc = recv(client.sock, buf, BUFSIZE, 0)) < 0) {
            perror("receiving stream message");
            pthread_exit(0);
        }

        if (rc > 0) {
            buf[rc] = '\0';
            printf("Received: %s\n", buf);

            fprintf(logFile, "[%s:%d] Received: %s\n", inet_ntoa(client.from.sin_addr),
                    ntohs(client.from.sin_port), buf);
            fflush(logFile);

            if (send(client.sock, buf, rc, 0) < 0) {
                perror("sending stream message");
            }
        }

        if (rc == 0) {
            printf("Client %s:%d disconnected\n", inet_ntoa(client.from.sin_addr),
                   ntohs(client.from.sin_port));

            /* Log disconnection */
            fprintf(logFile, "Client %s:%d disconnected\n", inet_ntoa(client.from.sin_addr),
                    ntohs(client.from.sin_port));
            fflush(logFile);

            close(client.sock);
            pthread_exit(0);
        }
    }

    close(client.sock);
    pthread_exit(0);
}

void reusePort(int s) {
    int one = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) == -1) {
        perror("setsockopt SO_REUSEADDR");
        exit(-1);
    }
}
