
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

char buf[BUFSIZE];
char rbuf[BUFSIZE];
void GetUserInput();
void cleanup(char *buf);


int rc, cc;
int   sd;

int main(int argc, char **argv ) {
    int childpid;
    struct sockaddr_in server;
    struct sockaddr_in client;
    struct hostent *hp, *gethostbyname();
    struct sockaddr_in from;
    struct sockaddr_in addr;
    int fromlen;
    int length;
    char ThisHost[80];

    strcpy(ThisHost,"localhost");

    printf("----TCP/Client running at host NAME: %s\n", ThisHost);
    if  ( (hp = gethostbyname(ThisHost)) == NULL ) {
	fprintf(stderr, "Can't find host %s\n", argv[1]);
	exit(-1);
    }
    bcopy ( hp->h_addr, &(server.sin_addr), hp->h_length);
    printf("    (TCP/Client INET ADDRESS is: %s )\n", inet_ntoa(server.sin_addr));

    if  ( (hp = gethostbyname(argv[1])) == NULL ) {
	addr.sin_addr.s_addr = inet_addr(argv[1]);
	if ((hp = gethostbyaddr((char *) &addr.sin_addr.s_addr,
				sizeof(addr.sin_addr.s_addr),AF_INET)) == NULL) {
	    fprintf(stderr, "Can't find host %s\n", argv[1]);
	    exit(-1);
	}
    }
    printf("----TCP/Server running at host NAME: %s\n", hp->h_name);
    bcopy ( hp->h_addr, &(server.sin_addr), hp->h_length);
    printf("    (TCP/Server INET ADDRESS is: %s )\n", inet_ntoa(server.sin_addr));

    server.sin_family = AF_INET; 
    server.sin_port = htons(atoi(argv[2]));
    sd = socket (AF_INET,SOCK_STREAM,0); 

    if (sd<0) {
	perror("opening stream socket");
	exit(-1);
    }

    if ( connect(sd, (struct sockaddr *) &server, sizeof(server)) < 0 ) {
	close(sd);
	perror("connecting stream socket");
	exit(0);
    }
    fromlen = sizeof(from);
    if (getpeername(sd,(struct sockaddr *)&from,&fromlen)<0){
	perror("could't get peername\n");
	exit(1);
    }
    printf("Connected to TCPServer1: ");
    printf("%s:%d\n", inet_ntoa(from.sin_addr),
	   ntohs(from.sin_port));
    if ((hp = gethostbyaddr((char *) &from.sin_addr.s_addr,
			    sizeof(from.sin_addr.s_addr),AF_INET)) == NULL)
	fprintf(stderr, "Can't find host %s\n", inet_ntoa(from.sin_addr));
    else
	printf("(Name is : %s)\n", hp->h_name);
    childpid = fork();
    if (childpid == 0) {
	GetUserInput();
    }

    for(;;) {
	cleanup(rbuf);
	if( (rc=recv(sd, rbuf, sizeof(buf), 0)) < 0){
	    perror("receiving stream  message");
	    exit(-1);
	}
	if (rc > 0){
	    rbuf[rc]='\0';
	    //printf("Received: %s\n", rbuf);
        printf("%s", rbuf);
	}else {
	    printf("Disconnected..\n");
	    close (sd);
	    exit(0);
	}
  }
}

void cleanup(char *buf)
{
    int i;
    for(i=0; i<BUFSIZE; i++) buf[i]='\0';
}

void GetUserInput()
{
    printf("# ");
    for(;;) {
	//printf("\nEnter command:\n");
	cleanup(buf);
	rc=read(0,buf, sizeof(buf));
	if (rc == 0) break;
	if (send(sd, buf, rc, 0) <0 )
	    perror("sending stream message");
    }
    printf ("EOF... exit\n");
    close(sd);
    kill(getppid(), 9);
    exit (0);
}
