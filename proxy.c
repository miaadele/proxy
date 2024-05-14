#include <stdio.h> 
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <assert.h>
#include <signal.h>

/* Mia Lassiter
CSEN 162: Project 1, web proxy */

#define HEADER_MAX 8192
#define BUFF 1024
#define N 30 //number of simultaneous connections that can be served

//global variables
int serverfd;
int threadCt = 0;
pthread_t connections[N];

//extract server address
char *extractServerAddress(const char *request) {
    static char extractedAddr[256];
    char *start = strstr(request, "Host: ");
    if(start != NULL) {
        start += strlen("Host: "); //move pointer to start of host address
        char *end = strchr(start, '\r'); //find end of host address
        if(end != NULL) {
            size_t length = end-start;
            if(length < sizeof(extractedAddr)) {
                strncpy(extractedAddr, start, length);
                extractedAddr[length] = '\0';
                printf("Line 40 \n");
                return extractedAddr;
            }
        }
    }
    else {
        perror("HTTP address not found \n");
        return NULL; //server address not retrieved
    } 
    return NULL;
}

//Thread function for servicing client requests for file transfer
void *connectionHandler(void *sock) {
    int connfd = *((int *)sock);
    int n;
    char csbuf[BUFF]; //client-side receiving buffer
    char srbuf[BUFF]; //buffer holding destination address

    printf("Connection established\n");
    if((n = read(connfd, csbuf, sizeof(csbuf)-1)) < 0) {
        perror("Failure reading client request \n");
        close(connfd);
        return NULL;
    }

    csbuf[n] = '\0'; //null terminator
    strcpy(srbuf, csbuf);
    char *extractAddr = extractServerAddress(csbuf);

    if(extractAddr != NULL) {
        printf("Server address: %s\n", extractAddr);
    }
    else {
        close(connfd);
        return NULL;
    }

    //DNS resolution
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    int status = getaddrinfo(extractAddr, "http", &hints, &res);
    if(status != 0) {
        fprintf(stderr, "getaddrinfo: %s \n", gai_strerror(status));
        close(connfd);
        return NULL;
    }
    
    //Connect to the server
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if(clientfd < 0) {
        perror("Failure to connect to server\n");
        close(connfd);
        return NULL;
    }

    struct sockaddr_in addr;
    struct sockaddr_in *resolvedAddr = (struct sockaddr_in *)res->ai_addr;
    memcpy(&addr.sin_addr, &resolvedAddr->sin_addr, sizeof(struct in_addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    freeaddrinfo(res);

    if(connect(clientfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
        perror("connection to client failed \n");
        close(clientfd);
        close(connfd);
        return NULL;
    }

    send(clientfd, srbuf, strlen(srbuf), 0);
    char response[1024];
    int bytes_recv = 0;

    while((n = read(clientfd, response, sizeof(response))) > 0) {
        int bytes_sent = send(connfd, response, n, 0);
        assert(bytes_sent != -1);
        memset(response, 0, sizeof(response));
        if(n < 0) {
            perror("Client read failure \n");
            return NULL;
        }
    }//end while

    close(connfd);
    close(clientfd);
    return NULL;
}

//signal handler to close socket
void sigintHandler(int sig) {
    close(serverfd);
    exit(0);
}

int main(int argc, char *argv[]) {
    //Get port number from the command line 
    if (argc != 2){
	    printf ("Usage: %s <port #> \n",argv[0]);
	    exit(1);
    }  

    struct sockaddr_in servAddr;
    int addrlen = sizeof(servAddr);
    memset(&servAddr, 0, sizeof(servAddr));
    
    if((serverfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Failure to set up server-side endpoint socket \n");
        close(serverfd);
        exit(1);
    }

    //set the server address to send using socket addressing structure
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(atoi(argv[1]));
    servAddr.sin_addr.s_addr = INADDR_ANY;

    if(bind(serverfd, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
        perror("Failure to bind to the endpoint socket\n");
        close(serverfd);
        exit(1);
    }

    signal(SIGINT, sigintHandler); //register signal handler

    if(listen(serverfd, SOMAXCONN) < 0) {
        perror("Server failure \n");
        close(serverfd);
        exit(1);
    }

    while(1) {
        pthread_t thread;
        int connfd = accept(serverfd, (struct sockaddr *)&servAddr, (socklen_t *)&addrlen);
        
        if(connfd < 0) {
            perror("Server acceptance failure \n");
            close(serverfd);
            exit(1);
        }
        if(pthread_create(&connections[threadCt], NULL, connectionHandler, (void *)&connfd) < 0) {
            perror("Unable to create a thread \n");
            close(connfd);
            exit(1);
        }
        for(int i = 0; i < N; i++) {
            if(connections[i] == 0) {
                connections[i] = thread;
                break;
            }
        }//end for
    } //end while
   close(serverfd);
   return 0;
}
