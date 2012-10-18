//
//  webserver
//
//  Created by Kim Swennen and Ben Tzou.
//
//

/*QUESTIONS:
    nothing new here; just checking that the commit works
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <cctype>

#define true 1
#define false 0

#define MYPORT 2020  //server port number
#define BACKLOG 20
#define BUFSIZE 256

struct headerInfo {
    const char* statusCode;         //initialized to 0
    const char* server;
    const char* date;
    const char* contentType;
    off_t contentLength;        //initialized to 0
};

//function declarations
void respondWithHTML(int socketfd);
const int isValidHttpRequest(const char* response);
const char* getRequestedFilename(const char* buffer);
const char* getContentType (const char* fileName);
int createHeader(struct headerInfo* replyHeader, char * header);


void sigChildHandler (int s) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main (int argc, const char * argv[]) {

    /*---------variable declarations------------*/
    
    int sockfd;     //socket file descriptor
    int childSockfd;    //socket descriptor for forked child
    socklen_t clientLength = sizeof(struct sockaddr_in);   //length of client address
    short portno = MYPORT;
    int backlog = BACKLOG;
    struct sockaddr_in serverAddress, clientAddress;
    pid_t pid;
    struct sigaction signalAction;
    
    /*----------code to set up signal handling for zombie child processes---------------*/
    signalAction.sa_handler = sigChildHandler;
    if (sigemptyset(&signalAction.sa_mask) < 0) {
        perror("sigemptyset error");
        exit(1);
    }
    signalAction.sa_flags=SA_RESTART;
    if (sigaction(SIGCHLD, &signalAction, NULL) < 0){
        perror("error on sigaction");
        exit(1);
    }
    
    /*---------socket setup-----------*/
    sockfd = socket(AF_INET, SOCK_STREAM, 0);   //creating a ipv4 socket to use TCP
    if (sockfd < 0) {
        perror("error opening socket");
        exit(1);
    }
    
    /*---------set up address structure------------*/
    bzero((char *) &serverAddress, sizeof(serverAddress));     //zero out the contents of the serverAddress
    //set server address details:
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(portno);     //in network byte order
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    
    
    /*---------bind-----------*/
    if (bind(sockfd, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr_in)) < 0) {
        perror("error on binding");
        exit(1);
    }
    
    
    /*---------listen------------*/
    if (listen(sockfd, backlog) < 0) {
        perror("error on listening");
        exit(1);
    }
    
    while (1) {             //will run forever
        //create new socket for incoming client
        childSockfd = accept(sockfd, (struct sockaddr *)&clientAddress, &clientLength);
        
        if (childSockfd < 1)  {
            perror("error on accept");
            exit(1);
        }

        pid = fork();
        if (pid < 0) {
            perror("error on fork");
            exit(1);
        }
        
        if (pid == 0) {
            if (close(sockfd) < 0){
                perror("error on close in child");
                exit(1);
            }
            respondWithHTML(childSockfd);
            
            if (close(childSockfd) < 0){
                perror("error on close after response in child");
                exit(1);
            }
            exit(0);
        }
        else {
            if (close(childSockfd)<0){
                perror("error on close in server");
                exit (1);
            }
        }
    }
    return 0;
}

void respondWithHTML(int socketfd) {
//    printf("child socket number %zd going through respondWithHTML\n", getpid());
    
    /*-------------variable declarations-----------------*/
    size_t buffsize = BUFSIZE;
    size_t totalHeaderBufSize;
    char * buf = NULL;
    bool loopback = true;
    buf = (char*)malloc(buffsize);
    if (buf == NULL) {
        perror("failed to allocate buffer buf");
        exit(1);
    }
    bzero(buf, buffsize);
    char* readPoint = buf;
    long int amountRead = 0;
    long int totalRead = 0;
    struct headerInfo *replyHeader;
    replyHeader = (struct headerInfo*)malloc(sizeof(struct headerInfo));
    if (replyHeader == NULL) {
        perror("failed to allocate buffer for replyHeader");
        exit(1);
    }
    const char* fileName;
    int requestedFileDescriptor;
    struct stat* fileStats = (struct stat*) malloc(sizeof(struct stat));
    if (fileStats == NULL) {
        perror("failed to allocate buffer for fileStats");
        exit(1);
    }
    char* messageHeader = NULL;
    char* writePoint;
    off_t curHeaderSize;
    long int test;
    
    replyHeader->contentLength = 0;
    replyHeader->contentType = NULL;
    
    
    /*--------------read in a loop to make sure buffer is large enough for request message-------------------*/
    while (loopback) {
        
        amountRead = read(socketfd, readPoint, buffsize);
        totalRead = totalRead + amountRead;         //track total number of bytes read
        
        if (amountRead < 0) {
            perror("error on read");
            exit(1);
        }
        else if (amountRead == 0) {  //socket closed
            loopback=false;
            fprintf(stderr, "client socket closed - no complete request messages received\n");
            exit(1);
        }
        
        else if (amountRead >= buffsize) {        //buffer is too small
            buffsize *= 2;
            buf = (char*) realloc(buf, ((size_t)amountRead + buffsize));
            if (buf == NULL) {
                perror("error on realloc");
                exit(1);
            }
            readPoint = buf + totalRead;         //start adding new data to the end of the buffer on the next read
        }
        
        else {          //  amountRead < buffsize
            loopback = false;
            buf = (char*)realloc(buf, totalRead+1);
            if (buf == NULL) {
                perror("error on realloc");
                exit(1);
            }
            buf[totalRead]= '\0';            //buf is now a properly formatted cstring
            printf("%s", buf);  //print request
        }
    }
    
    /*---------------parse information from request message--------------*/

    if (isValidHttpRequest(buf) == false){
        replyHeader->statusCode = "400 Bad Request";
//        printf("got bad request");
    }
    
    else {                  //was a valid http request
        fileName = getRequestedFilename(buf);
        printf("filename is %s", fileName);
        fileName = fileName + 1;    //to get rid of leading '/'
        
        requestedFileDescriptor = open("index.html", O_RDONLY);
        if (requestedFileDescriptor < 0) {
            perror("error opening requested file");
            replyHeader->statusCode = "404 Not Found";
        }
        else {
            if (fstat(requestedFileDescriptor, fileStats) < 0) {
                perror("error reading file stats");
                exit(1);
            }
            else {
                replyHeader->contentLength = fileStats->st_size;
                replyHeader->statusCode = "200 OK";
                replyHeader->contentType = (const char*)malloc(BUFSIZE);
                if (replyHeader->contentType == NULL) {
                    perror("error allocating memory for the content type");
                    exit(1);
                }
                replyHeader->contentType = getContentType(fileName);
            }
        }
    }
    
    time_t timer = time(NULL);
    replyHeader->date = ctime(&timer);
    messageHeader = (char*)malloc(BUFSIZE+strlen(fileName));
    createHeader(replyHeader, messageHeader);
    
    //reallocate a buffer that will fit all of the reply message
    curHeaderSize = strlen(messageHeader);
    totalHeaderBufSize = curHeaderSize + replyHeader->contentLength;
    messageHeader = (char*)realloc(messageHeader, totalHeaderBufSize);  //reallocate the messageHeader so there is room for the data portion
    
    //write the data to the end of the header
    writePoint = messageHeader + curHeaderSize;
    if (requestedFileDescriptor >= 0) {
        if (read(requestedFileDescriptor, (void*)writePoint, replyHeader->contentLength) < replyHeader->contentLength) {
            perror("did not read all of content into reply message");
        }
    }
    
    //write the reply message to the socket
    printf("reply message is %s\n", messageHeader);
    test = write(socketfd, messageHeader, sizeof(messageHeader));
                 
    if (test< 0) {
        perror("error writing file to socket");
        exit(1);
    }
    
    if (test < sizeof(messageHeader))
        perror("did not write all of the content to the socket");
    
    close(requestedFileDescriptor);
    free(messageHeader);
    free(replyHeader);
    free(buf);
    free(fileStats);
    free((void*)replyHeader->contentType);
}


//Input: a pointer to the replyHeader structure, filled out with the necessary info, and a
//pointer to the header, with memory allocated (and later freed) by the calling function
//Output: the header c-string, properly formatted with all necessary content and with terminating crlf
int createHeader(struct headerInfo* replyHeader, char* header){
    printf("entering createMessageHeader\n");
    
    char terminatingString [] = "\n\0";
    snprintf(header, BUFSIZE, "HTTP/1.1 %s\n"
             "Connection: close\n"
             "Date: %s"
             "Server: Apache\n", replyHeader->statusCode, replyHeader->date);
    
    if ((strcmp(replyHeader->statusCode, "404 Not Found") == 0) || (strcmp(replyHeader->statusCode, "400 Bad Request")) == 0){
        strcat(header, terminatingString);
        return 0;
    }
    
    else {     //need to add the content-length and type to the header
        size_t currentHeaderLen = strlen(header);
        snprintf(header+currentHeaderLen, (BUFSIZE-currentHeaderLen), "Content-Length: %lld\n"
                 "Content-Type: %s\n\n", replyHeader->contentLength, replyHeader->contentType);
        return 0;
    }
}


// Input: response, containing the HTTP GET request
// Output: true if valid HTTP GET request, false otherwise
const int isValidHttpRequest(const char* response) {
    char buffer[strlen(response)];
    strcpy(buffer, response);
    
    char *line = strtok(buffer, "\n");
    
    // check if first line of HTTP request is null
    if (line != NULL) {
        char *word = strtok(line, " ");
        
        // check if first word is GET
        if (strcmp(word, "GET") == 0) {
            /* char *filename = */ strtok(NULL, " ");   // avoiding warning for unused variable
            char *httpVersion = strtok(NULL, " ");
            char *endOfGet = strtok(NULL, " ");
            
            // check if HTTP version is 1.x and no other characters follow HTTP version
            if (httpVersion != NULL && endOfGet == NULL &&
                    strncmp(httpVersion, "HTTP/1.", 7) == 0)
                
                // everything checks out, return filename
                return true;
        }
    }
    
    // if any problem with GET format, send
    return false;
}

// Input: response, containing the HTTP GET request
// Output: requested filename
// Assumptions: HTTP GET request has been validated by isValidHttpRequest
const char* getRequestedFilename(const char* response) {
    char buffer[strlen(response)];
    strcpy(buffer, response);
    
    strtok(buffer, " ");
    return strtok(NULL, " ");
}

// stub for getContentType function
//const char* getContentType (const char* fileName) {
 //   return "text/html";
//}

// Input: filename, as a C string
// Output: MIME content type, defaulting to HTML if type not recognized
// Assumptions: assumes correctly formatted filename with no whitespace
const char* getContentType(const char* filename) {
    // make lowercase
    char buffer[strlen(filename)];
    strcpy(buffer, filename);
    
    int i;
    for (i = 0; buffer[i]; i++)
        buffer[i] = tolower(buffer[i]);
    
    // get extension
    char* extn = strrchr(buffer, '.') + 1;
    
    // return appropriate content type
    if (strcmp(extn, "jpeg") == 0 || strcmp(extn, "jpg") == 0)
        return "image/jpeg";
    else if (strcmp(extn, "gif") == 0)
        return "image/gif";
    else
        return "text/html";
}