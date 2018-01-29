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
        // Accept connections.
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

        if (newsockfd < 0)
        error("ERROR on accept");

        int n;
        const int maxreqlen = 8192;
        char request[maxreqlen]; // Maximum size of an HTTP GET request.
        memset(request, 0, maxreqlen);  // reset memory

        // Read client's message.
        n = read(newsockfd, request, maxreqlen);
        if (n < 0) 
            error("ERROR reading from socket");
        fprintf(stdout, "%s", request);

        // Process HTTP request.
        char filename[4096]; // Maximum pathname length in Linux.
        memset(filename, 0, 4096);
        int filename_index = 0;

        char filetype[5]; // .html, .htm, .jpeg, .gif, or .jpg
        memset(filetype, 0, 5);
        int filetype_index = 0;
        int filetype_flag = 0; // Are we currently processing the file type?

        // Generate filename + filetype.
        int i = 5;
        while (i < 4096) {
            if (request[i] == ' ') {
                break;
            }

            if (request[i] == '.') {
                filetype_flag = 1;
            } else if (filetype_flag) {
                filetype[filetype_index++] = request[i];
            }
            
            if (strncmp(&request[i], "%20", 3) == 0) { // Check for whitespace code in pathname (represented as %20).
                filename[filename_index++] = ' ';
                i += 2;
            } else {
                filename[filename_index++] = request[i];
            }

            i++;
        }
        filename[filename_index++] = '\0';
        filetype[filetype_index++] = '\0';
 
        // Open file.
        FILE* file = fopen(filename, "r");
        if (!file) {
            // Send HTTP 404 response.
            write(newsockfd, "HTTP/1.1 200 OK\n", 14);
            write(newsockfd, "Content-length: 13\n", 19);
            write(newsockfd, "Content-Type: text/html\n\n", 25);

            write(newsockfd, "<b>404 Not Found</b>", 20);

            close(newsockfd);
            continue;
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

        // Send correct filetype.
        if (!strcmp(filetype, "html") || !strcmp(filetype, "htm")) {
            write(newsockfd, "Content-Type: text/html\n\n", 25);
        } else if (!strcmp(filetype, "jpg") || (!strcmp(filetype, "jpeg"))) {
            write(newsockfd, "Content-Type: image/jpeg\n\n", 26);
        } else if (!strcmp(filetype, "gif")) {
            write(newsockfd, "Content-Type: image/gif\n\n", 25);
        } else { // Just fallback to HTML.
            write(newsockfd, "Content-Type: text/html\n\n", 25);            
        }

        int count;
        int filefd = fileno(file);
        char filebuffer[8192];
        while ((count = read(filefd, filebuffer, sizeof filebuffer)) > 0) {
            send(newsockfd, filebuffer, count, 0);
        }
        if (count < 0) {
            error("ERROR sending file");
        }

        fprintf(stderr, "Successfully sent file %s.", "test");
        // if (sendfile(newsockfd, fileno(file), NULL, 0) < 0) {
        //     error("ERROR sending file");
        // }

        close(newsockfd);  // close connection
    }

    close(sockfd);

    return 0;
}
