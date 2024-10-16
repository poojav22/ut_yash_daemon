
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#define MAXHOSTNAME 80
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
int pfd[2];
struct ClInfo{
    int id;
    int sock;
    struct sockaddr_in from;
};

FILE *logFile;
void reusePort(int sock);
void EchoServe(void *input);
void process_cmd(char* buf);


int main(int argc, char **argv ) {
    int   sd, psd;
    struct   sockaddr_in server;
    struct  hostent *hp, *gethostbyname();
    struct sockaddr_in from;
    struct ClInfo client;
    int fromlen;
    int length;
    char ThisHost[80];
    int pn;
    int childpid;
    int NUM_CLIENTS = 50;
    pthread_t th[NUM_CLIENTS];
    int i = 0;

    logFile = fopen("server_log.txt", "a");
    if (!logFile) {
        perror("Failed to open log file");
        exit(-1);
    }

    /* get TCPServer1 Host information, NAME and INET ADDRESS */
    //gethostname(ThisHost, MAXHOSTNAME);
    // OR
    strcpy(ThisHost,"localhost"); 

    printf("----TCP/Server running at host NAME: %s\n", ThisHost);
    if  ( (hp = gethostbyname(ThisHost)) == NULL ) {
      fprintf(stderr, "Can't find host %s\n", argv[1]);
      exit(-1);
    }
    bcopy ( hp->h_addr, &(server.sin_addr), hp->h_length);
    printf("    (TCP/Server INET ADDRESS is: %s )\n", inet_ntoa(server.sin_addr));

    /** Construct name of socket */
    server.sin_family = AF_INET;
    /* OR server.sin_family = hp->h_addrtype; */

    server.sin_addr.s_addr = htonl(INADDR_ANY);
    if (argc == 1)
        server.sin_port = htons(0);
    else  {
        pn = htons(atoi(argv[1]));
        server.sin_port =  pn;
    }

    /** Create socket on which to send  and receive */

    sd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
 
    if (sd<0) {
	perror("opening stream socket");
	exit(-1);
    }

    reusePort(sd);
    if ( bind( sd, (struct sockaddr *) &server, sizeof(server) ) < 0 ) {
	close(sd);
	perror("binding name to stream socket");
	exit(-1);
    }

    length = sizeof(server);
    if ( getsockname (sd, (struct sockaddr *)&server,&length) ) {
	perror("getting socket name");
	exit(0);
    }
    printf("Server Port is: %d\n", ntohs(server.sin_port));

    listen(sd,40);
    fromlen = sizeof(from);
    for(;;){
	psd  = accept(sd, (struct sockaddr *)&from, &fromlen);
    if (psd < 0){
            perror("accept failure");
            continue;
        }

    client.sock = psd;
    client.from = from;
    client.id = i+1;
    struct ClInfo thread_client = client;

    if (pthread_create(&th[i++], NULL, EchoServe, (void *) &thread_client) != 0) {
            perror("Thread creation failed");
            close(psd);
            continue;
        }

    printf("Thread created for client %s:%d (Thread ID: %lu)\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port), (unsigned long)th[i]);
    if (i >= NUM_CLIENTS) {
        i = 0;
    }

    }
}

void run_yash(int psd, char *cmd) {
        int saved_stdout = dup(1);
        dup2(psd, STDOUT_FILENO);
        process_cmd(cmd);
        fflush(stdout);
        dup2(saved_stdout, STDOUT_FILENO);
}

void update_log_file(struct sockaddr_in from, char *cmd) {
    char time_now[64];
    printf("Updating log file here with cmd: %s", cmd);
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(time_now, sizeof(time_now), "%b %d %H:%M:%S", tm);
    pthread_mutex_lock(&lock);
    fprintf(logFile, "%s yashd[%s:%d]: %s\n", time_now, inet_ntoa(from.sin_addr), ntohs(from.sin_port), cmd);
    fflush(logFile);
    pthread_mutex_unlock(&lock);
}
void EchoServe(void *input) {
    struct ClInfo client =  *(struct ClInfo *)input;
    char buf[512], cbuf[512], cmd[512];
    int rc;
    struct  hostent *hp, *gethostbyname();

    printf("Serving %s:%d\n", inet_ntoa(client.from.sin_addr),
	   ntohs(client.from.sin_port));
    if ((hp = gethostbyaddr((char *)&client.from.sin_addr.s_addr,
			    sizeof(client.from.sin_addr.s_addr),AF_INET)) == NULL)
	fprintf(stderr, "Can't find host %s\n", inet_ntoa(client.from.sin_addr));
    else
	printf("(Name is : %s)\n", hp->h_name);


    for(;;){
     //   sleep(5);
	printf("\n...server is waiting...\n");
	if( (rc=recv(client.sock, buf, sizeof(buf), 0)) < 0){
	    perror("receiving stream  message");
	    exit(-1);
	}
    printf("rc here %d", rc);
    printf("buf here %s", buf);
	if (rc > 0){
	    buf[rc]='\0';
	    printf("Received: %s\n", buf);
        printf("Running yash: %s\n", buf);
        update_log_file(client.from, buf);

        run_yash(client.sock, buf);
	    printf("From TCP/Client: %s:%d\n", inet_ntoa(client.from.sin_addr),
		   ntohs(client.from.sin_port));
	    printf("(Name is : %s)\n", hp->h_name);

	}
	else {
	    printf("TCP/Client: %s:%d\n", inet_ntoa(client.from.sin_addr),
		   ntohs(client.from.sin_port));
	    printf("(Name is : %s)\n", hp->h_name);
	    printf("Disconnected..\n");
	    close (client.sock);
	    exit(0);
    }
    }
}
void reusePort(int s)
{
    int one=1;

    if ( setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(char *) &one,sizeof(one)) == -1 )
	{
	    printf("error in setsockopt,SO_REUSEPORT \n");
	    exit(-1);
	}
}
