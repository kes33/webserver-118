//
//  CS 118 Project 1 Webserver
//
//  Created by Kim Swennen and Ben Tzou.
//

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
#include <cstring>
#include <cctype>

#define true 1
#define false 0

#define BACKLOG 20
#define BUFSIZE 256


// SERVER PORT NUMBER
#define MYPORT 2020


// struct for storing HTTP information
struct headerInfo {
    const char* statusCode;
    const char* server;
    const char* date;
    const char* contentType;
    off_t contentLength;
};


// function declarations
void generateResponse(int socketfd);
const int isValidHttpRequest(const char* response);
const char* getContentType (const char* fileName);
char* createHeader(struct headerInfo* replyHeader);
char* appendDataToHeader(char * header, const int requestedFileDescriptor, const off_t fileSize);
char* writeSocketContentsToBuf(const int socketfd);
void sigChildHandler (int s) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}



int main (int argc, const char * argv[]) {
    
    // variable declarations
    int sockfd;     //socket file descriptor
    int childSockfd;    //socket descriptor for forked child
    socklen_t clientLength = sizeof(struct sockaddr_in);   //length of client address
    short portno = MYPORT;
    int backlog = BACKLOG;
    struct sockaddr_in serverAddress, clientAddress;
    pid_t pid;
    struct sigaction signalAction;
    
    // set up signal handling for zombie child processes
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
    
    // set up socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);   //creating a ipv4 socket to use TCP
    if (sockfd < 0) {
        perror("error opening socket");
        exit(1);
    }
    
    // set up address struct
    bzero((char *) &serverAddress, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(portno);     //in network byte order
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    
    // bind socket
    if (bind(sockfd, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr_in)) < 0) {
        perror("error on binding");
        exit(1);
    }
    
    // listen
    if (listen(sockfd, backlog) < 0) {
        perror("error on listening");
        exit(1);
    }
    
    while (1) {
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
            generateResponse(childSockfd);  // handle request
            
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


void generateResponse(int socketfd) {
    
    // variable declarations
    struct headerInfo replyHeader;
    replyHeader.contentLength = 0;
    replyHeader.contentType = NULL;
    
    int requestedFileDescriptor;
    struct stat fileStats;
    
    char* response = NULL;
    long int bytesWritten;
    
    
    // read request into buffer
    char * buf = writeSocketContentsToBuf(socketfd);
    
    // print request to standard out
    printf("%s\n", buf);
    
    // validate request and collect relevant HTTP information
    if (isValidHttpRequest(buf) == false) {
        // invalid HTTP request
        replyHeader.statusCode = "400 Bad Request";
    }
    else {
        // valid HTTP request
        
        // get filename
        char buffer[strlen(buf)+1];
        strcpy(buffer, buf);
        strtok(buffer, " ");
        char* fileName = strtok(NULL, " ");
        if (fileName[0] == '/')         // accept both /webpage.html or webpage.html
            fileName = fileName + 1;
        
        // attempt to open requested file
        requestedFileDescriptor = open(fileName, O_RDONLY);
        if (requestedFileDescriptor < 0) {
            // file was not found
            replyHeader.statusCode = "404 Not Found";
        }
        else
            // file was found
            if (fstat(requestedFileDescriptor, &fileStats) < 0) {
                perror("error reading file stats");
                exit(1);
            }
            else {
                replyHeader.statusCode = "200 OK";
                replyHeader.contentLength = fileStats.st_size;
                replyHeader.contentType = getContentType(fileName);
            }
    }
    
    // prepare HTTP response: 1) create the header 2) if necessary, append data
    response = createHeader(&replyHeader);
    size_t headerLength = strlen(response);
    if (replyHeader.contentLength > 0)
        response = appendDataToHeader(response, requestedFileDescriptor, replyHeader.contentLength);
    
    // write HTTP response to socket and standard out
    bytesWritten = write(socketfd, response, headerLength + replyHeader.contentLength);
    if (bytesWritten < 0) {
        perror("error writing file to socket");
        exit(1);
    }
    else if (bytesWritten < headerLength + replyHeader.contentLength)
        perror("did not write all of the content to the socket");
    
    
    //clean up
    close(requestedFileDescriptor);
    free(response);
    free(buf);
}



// Input: socketfd, containing the file descriptor for the socket
// Output: C string, containing socket contents
// Assumptions:
char* writeSocketContentsToBuf(const int socketfd) {
    // allocate memory for buffer
    char * buffer = (char*)malloc(BUFSIZE);
    if (buffer == NULL) {
        perror("failed to allocate buffer buf");
        exit(1);
    }
    bzero(buffer, BUFSIZE);
    
    // declarations
    char* readPoint = buffer;
    long int amountRead = 0;
    long int totalRead = 0;
    size_t buffsize = BUFSIZE;
    
    // loop to capture socket contents
    bool loopback = true;
    while (loopback) {
        
        // track bytes read
        amountRead = read(socketfd, readPoint, buffsize);
        totalRead = totalRead + amountRead;
        
        // error with read
        if (amountRead < 0) {
            perror("error on read");
            exit(1);
        }
        
        // socket was closed
        else if (amountRead == 0) {
            loopback = false;
            fprintf(stderr, "client socket closed"
                    "- no complete request messages received\n");
            exit(1);
        }
        
        // buffer is too small
        else if (amountRead >= buffsize) {
            buffsize *= 2;
            buffer = (char*) realloc(buffer, ((size_t)amountRead + buffsize));
            if (buffer == NULL) {
                perror("error on realloc");
                exit(1);
            }
            // on the next read, add new data to the end of the buffer
            readPoint = buffer + totalRead;
        }
        
        // buffer is too big (i.e. amountRead < buffsize)
        else {
            loopback = false;
            buffer = (char*)realloc(buffer, totalRead+1);
            if (buffer == NULL) {
                perror("error on realloc");
                exit(1);
            }
            buffer[totalRead]= '\0';   // add null termination to properly format cstring
        }
    }
    
    return buffer;
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
            strtok(NULL, " ");  // skip filename for now
            char *httpVersion = strtok(NULL, " ");
            char *endOfGet = strtok(NULL, " ");
            
            // check for HTTP version 1.x with no other characters following
            if (httpVersion != NULL && endOfGet == NULL &&
                strncmp(httpVersion, "HTTP/1.", 7) == 0)
                
                // everything checks out, return filename
                return true;
        }
    }
    // if any problem with GET format, send
    return false;
}



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
    else if (strcmp(extn, "ico") == 0)
        return "image/x-icon";
    else
        return "text/html";
}



// Input: a pointer to the replyHeader structure, filled with the relevant info
// Output: the header c-string, formatted with all necessary content and with
//   terminating crlf
char* createHeader(struct headerInfo* replyHeader) {
    char* header = (char*)malloc(BUFSIZE);
    bzero(header, BUFSIZE);
    
    // get time
    time_t timer = time(NULL);
    replyHeader->date = ctime(&timer);
    
    // create beginning part of header
    snprintf(header, BUFSIZE, "HTTP/1.1 %s\n"
             "Connection: close\n"
             "Date: %s"
             "Server: KSBT\n",
             replyHeader->statusCode,
             replyHeader->date);
    
    // if no error, add content-length and content-type fields
    if (strcmp(replyHeader->statusCode, "404 Not Found") == 0 ||
        strcmp(replyHeader->statusCode, "400 Bad Request") == 0)
        strcat(header, "\n");
    else
        snprintf(header + strlen(header), BUFSIZE - strlen(header),
                 "Content-Length: %ld\n"
                 "Content-Type: %s\n\n",
                 replyHeader->contentLength,
                 replyHeader->contentType);
    
    return header;
}



// Input: response, containing the header without data; requestedFileDescriptor,
//        containing the file descriptor for the requested file; fileSize,
//        containing the size of the requested file
// Output: response, with the requested file appended
char* appendDataToHeader(char* response, const int requestedFileDescriptor,
    const off_t fileSize) {
    
    //calculate buffer size needed to fit data
    off_t curHeaderSize = strlen(response);
    size_t totalHeaderBufSize = curHeaderSize + fileSize;
    
    //reallocate the messageHeader to make room for data
    response = (char*)realloc(response, totalHeaderBufSize);
    if (response == NULL) {
        perror("error reallocating memory for messageHeader");
        exit(1);
    }
    
    //write the data to the end of the header (overwrite the terminating '\0')
    char* writePoint = response + curHeaderSize;
    
    //copy file into response at point given by writePoint
    //  assume that requestedFileDescriptor is valid
    long int test = read(requestedFileDescriptor, (void*)writePoint, fileSize);
    if (test == -1) {
        perror("error reading from requested file");
        exit(1);
    }
    
    if (test < fileSize)
        perror("did not read all of content into reply message");
    
    return response;
}