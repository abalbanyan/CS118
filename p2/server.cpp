#include "rdt.h"

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Must provide port number. Usage: %s <server_portnumber>\n", argv[0]);
        exit(1);
    }
    new Server(argv[1]);
}