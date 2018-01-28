#include <stdio.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>  /* signal name macros, and the kill() prototype */

#include <sys/sendfile.h>
#include <sys/stat.h>
#include <math.h>

void error(char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    if (argc < 2) {
        fprintf(stderr,"ERROR, no port provided\n");
        exit(1);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);  // create socket
    if (sockfd < 0)
        error("ERROR opening socket");
    memset((char *) &serv_addr, 0, sizeof(serv_addr));   // reset memory

    // fill in address info
    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    listen(sockfd, 5);  // 5 simultaneous connection at most

    while (1) {
        //accept connections
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

        if (newsockfd < 0)
        error("ERROR on accept");

        int n;
        char buffer[256];

        memset(buffer, 0, 256);  // reset memory

        // Read client's message.
        n = read(newsockfd, buffer, 255);
        if (n < 0) 
            error("ERROR reading from socket");
        
        char* filename = "test.html";
        FILE* file = fopen(filename, "r");
        if (!file) {
            fprintf(stderr, "ERROR Opening file: %s", buffer);
            error("");
        }

        // Determine file length and include it in the HTTP response.
        struct stat st;
        stat(filename, &st);
        int filesize = st.st_size;

        int contentstrlength = (int) (20 + ((ceil(log10(filesize)) + 1) * sizeof(char)));
        char* contentstr = malloc(contentstrlength * sizeof(char));
        sprintf(contentstr, "Content-length: %d\n", filesize);

        // Send HTTP response.
        write(newsockfd, "HTTP/1.1 200 OK\n", 16);
        write(newsockfd, contentstr, contentstrlength);
        fprintf(stderr, "%s", contentstr);
        write(newsockfd, "Content-Type: text/html\n\n", 25);

        int count;
        int filefd = fileno(file);
        char filebuffer[8192];
        while ((count = read(filefd, filebuffer, sizeof filebuffer)) > 0) {
            send(newsockfd, filebuffer, count, 0);
        }
        if (count < 0) {
            error("ERROR sending file");
        }


        printf("Successfully sent file %s.", "test");
        // if (sendfile(newsockfd, fileno(file), NULL, 0) < 0) {
        //     error("ERROR sending file");
        // }

        close(newsockfd);  // close connection
    }

    close(sockfd);

    return 0;
}
