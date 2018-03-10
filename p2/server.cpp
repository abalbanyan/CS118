#include "rdt.h"

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Must provide port number. Usage: %s <server_portnumber>\n", argv[0]);
        exit(1);
    }
    Server* server = new Server(argv[1]);
}