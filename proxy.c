#include <stdio.h> 
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <assert.h>

/* Mia Lassiter
CSEN 162: Project 1, web proxy 
Instructions: change your browser configuration to use a proxy. All HTTP requests using port 80 will be sent to the proxy at the loopback address 127.0.0.1
*/

#define BUFF 1024
#define N 100 //number of simultaneous connections that can be served

//global variables
int serverfd;
int threadCt = 0;
pthread_t connections[N];
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);

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

    //printf("Connection established\n");
    if((n = read(connfd, csbuf, sizeof(csbuf)-1)) < 0) {
        perror("Failure reading client request \n");
        close(connfd);
        return NULL;
    }

    csbuf[n] = '\0'; //null terminator
    strcpy(srbuf, csbuf);
    char *extractAddr = extractServerAddress(csbuf);

    if(extractAddr != NULL)
        printf("Server address: %s\n", extractAddr);
    else {
        close(connfd);
        return NULL;
    }

    //DNS resolution
    struct addrinfo hints, *res = NULL, *p = NULL;
    int status;
    char ipstr[INET_ADDRSTRLEN];
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = IPPROTO_TCP;
    
    if((status = getaddrinfo(extractAddr, NULL, &hints, &res)) != 0)
        exit(1);
    for(p = res; p != NULL; p = p->ai_next) {
        void *a;
        char *ipver;
        //get the pointer to the address itself
        if (p->ai_family == AF_INET) { //IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *) p->ai_addr;
            a = & (ipv4->sin_addr);
            ipver = "IPv4";
        } else {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *) p->ai_addr;
            a = &(ipv6->sin6_addr);
            ipver = "IPv6";
        }
        //convert IP to string and print it
        inet_ntop(p->ai_family, a, ipstr, sizeof ipstr);
        printf(" %s: %s\n", ipver, ipstr);
    }
   
    //Connect to the server
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if(clientfd < 0) {
        perror("Failure to connect to server\n");
        close(connfd);
        return NULL;
    }
    printf("Connected to server \n");
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
        if(n < 0)
            return NULL;
    }
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
    int socket_option = 1;
    memset(&servAddr, 0, sizeof(servAddr));
    
    if((serverfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Failure to set up server-side endpoint socket \n");
        close(serverfd);
        exit(1);
    }
    
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&socket_option, sizeof(int)); //configure the socket to ignore bind reuse error
    struct timeval timeout = {5, 0}; //set a 5-second timeout when waiting to receive
    setsockopt(serverfd, SOL_SOCKET, SO_RCVTIMEO, (const void *)&timeout, sizeof(timeout));

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
