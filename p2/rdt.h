#pragma once

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <ctime>
#include <signal.h>

using namespace std;

// Define constants (bytes).
const int MAX_PKT_SIZE = 1024; // Including headers.
const int MAX_SEQNO = 30720;
const int INITIAL_WINDOW = 5120; // Just the default.

const int FAST_RETRANSMIT_THRESH = 3;
const struct timeval TIMEOUT = {
    0,     /* tv_sec  */
    500000 /* tv_usec */
};
const struct timeval NOTIMEOUT = {0,0};

// TODO: Do we include header size when incrementing sequence number? Switch this to true if so.
const bool INCHEADER = false;

// TCP Header Flags
const int FIN = 1;   // 0b00000001;
const int SYN = 2;   // 0b00000010;
const int ACK = 16;  // 0b00010000;
const int CWR = 128; // 0b10000000;
const int SYNACK = ACK | SYN;

struct PacketHeader {
    // uint16_t src_port = 0;
    // uint16_t dst_port = 0;
    uint32_t seqno = 0;
    uint32_t ackno = 0;
    uint16_t flags = 0; // |= with flag defined above to set flag (e.g. flags |= ACK).
    // uint16_t window = 0;
    // uint16_t checksum = 0;
    // uint16_t urgentptr = 0;
    PacketHeader(uint32_t seqno, uint32_t ackno, uint16_t flags) {
        this->seqno = seqno;
        this->ackno = ackno;
        this->flags |= flags;
    }
    PacketHeader() {}
};

const int HEADER_SIZE = sizeof(PacketHeader);
const int MAX_PKT_SIZE_SANS_HEADER = MAX_PKT_SIZE - HEADER_SIZE;

// TCP Packet. Created with an optional payload (shallow copy!).
class Packet {
public:
    uint8_t* payload = NULL;
    PacketHeader header;
    int packet_size; // header + payload, in bytes
    bool acked = false;
    struct timeval timeout_time; // Timestamp of when a packet will timeout. Updated whenever a packet is sent/resent.

    // For reading.
    Packet(PacketHeader header, uint8_t* payload = NULL, int payload_size = 0) {
        if (payload) {        
            this->payload = new uint8_t[payload_size]; 
            memcpy(this->payload, payload, payload_size);
        }
        this->header = header;
        this->packet_size = HEADER_SIZE + payload_size;
        // gettimeofday(&(this->timeout), NULL);
        // timeradd(&TIMEOUT, &(this->timeout), &(this->timeout));
    }
    // For sending.
    Packet(int flag, int seqno = 0, int ackno = 0, uint8_t* payload = NULL, int payload_size = 0) {
        this->header.ackno = ackno;
        this->header.seqno = seqno;
        this->header.flags |= flag;

        this->payload = new uint8_t[payload_size]; 
        memcpy(this->payload, payload, payload_size);
        
        this->packet_size = payload_size + HEADER_SIZE;
        // gettimeofday(&(this->timeout), NULL);
        // timeradd(&TIMEOUT, &(this->timeout), &(this->timeout));
    }
    // Deep copy.
    Packet(Packet* &packet) {
        this->packet_size = packet->packet_size;
        if (packet->payload != NULL) {
            this->payload = new uint8_t[packet->packet_size - HEADER_SIZE];
            memcpy(this->payload, payload, this->packet_size - HEADER_SIZE);
        }
        this->header = packet->header;
        this->acked = packet->acked;
        this->timeout_time = packet->timeout_time;

        // gettimeofday(&(this->timeout), NULL);
        // timeradd(&TIMEOUT, &(this->timeout), &(this->timeout));
    }

    Packet() {}

    // Packet() {
    //     if (this->payload != NULL) {
    //         delete this->payload;
    //     }
    // }

//    ~Packet() { if (payload != NULL) delete this->payload; }
};

class Client {
public:
    int sockfd;
    struct sockaddr_in serverinfo;

    vector<uint16_t> last_seqnos; // Stores the sequence numbers of the last MAX_SEQNO/MAX_PKT_SIZE_SANS_HEADER packets we have seen. Used to check for duplicate packets.
    vector<Packet*> rcv_window; // Store received packets in a buffer.
    uint16_t rcv_base; // Base seqno in packet_buffer.
    
    uint16_t nextackno; // The next expected seqno. Used to determine whether received a packet is missing or out of order.

    // Creates and binds a socket to the server at port port.
    Client(char* serverhostname, char* port, char* filename);

    // Send a packet to the server. Returns bytes sent on success, 0 otherwise.
    int sendPacket(Packet &packet, bool retransmission = false);

    // Wait for a packet from the server and store in buffer. Returns number of bytes read on success, 0 otherwise.
    int receivePacket(Packet* &packet, bool blocking = true, struct timeval timeout = NOTIMEOUT);

    void writePacketToFile(ofstream &file, Packet* &packet);

    // Returns true if the packet's sequence number has been seen in the last MAX_SEQNO/MAX_PKT_SIZE_SANS_HEADER packets.
    bool isDuplicatePacket(Packet* &packet);
};

class Server {
public:
    int sockfd; // Server's UDP socket.
    uint16_t src_port; // Server port.
    struct sockaddr_in clientinfo, serverinfo; // Client initiates connection, so need to store clientinfo.

    ifstream file; // File we are sending.
    ssize_t filesize;

    vector<Packet*> window; // Packets ready to be sent (limited to size of window).
    uint16_t cwnd = INITIAL_WINDOW/MAX_PKT_SIZE; // Number of packets allowed in current window.
    uint16_t baseseqno; // The seqno of the oldest packet which has not been ACKed (bytes).
    uint16_t nextseqno; // The seqno of the next sendable packet (bytes).
    uint16_t lastackno; // The last ackno we have received. Remember SR uses cumulative ACKing, so baseseq should be set equal to lastack.
    int dupacks; // Counter for number of duplicate ACKs we have received (retransmit all unACKed packets on 3).

    bool filename_acked = false; // Has the filename been ACKed yet?
    int filename_ackno; // ackno of filename ACK.

    // Creates and binds a socket at port src_port.
    Server(char* src_port);

    // Send a packet to the connected client. Returns bytes sent on success, 0 otherwise.
    int sendPacket(Packet &packet, bool retransmission = false);

    // Wait for a packet from a client and store in buffer. Returns number of bytes read on success, 0 otherwise.
    int receivePacket(Packet* &packet, bool blocking = true, struct timeval timeout = NOTIMEOUT);

    // Send file <filename> to connected client.
    int sendFile(char* filename);

    // Reads the next PACKET_SIZE_SAN_HEADER bytes from the currently open file into buffer. Returns the number of bytes read, or 0 on error.
    bool sendFileChunk();
};
