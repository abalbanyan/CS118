#include "rdt.h"

int main(int argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Must provide hostname, port number, and filename. Usage: %s <server_hostname> <server_portnumber> <filename>\n", argv[0]);
        exit(1);
    }

    new Client(argv[1], argv[2], argv[3]);
}