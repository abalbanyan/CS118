#include "rdt.h"

// Creates and binds a socket to the server port.
Server::Server(char* src_port) {
    this->sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  // Create UDP socket.
    if (this->sockfd < 0) {
        fprintf(stderr, "ERROR: Unable to open socket.");
        exit(1);
    }

    // Fill in address info.
    struct sockaddr_in server_addr;
    memset((char*) &(this->serverinfo), 0, sizeof(this->serverinfo));

    this->serverinfo.sin_family = AF_INET;
    this->serverinfo.sin_port = htons(atoi(src_port));

    // Convert the hostname.
    if (inet_pton(AF_INET, "0.0.0.0", &(this->serverinfo.sin_addr)) <= 0) {
        fprintf(stderr, "Error converting hostname.");
        exit(1);
    }

    // Since this is the server, we need to bind the socket.
    if (bind(this->sockfd, (struct sockaddr *) &(this->serverinfo), sizeof(this->serverinfo)) < 0) {
        fprintf(stderr, "Unable to bind socket.\n");
        exit(1);
    }

    Packet snd_packet;
    Packet* rcv_packet = NULL;

    // Listen for TCP connection.
    if (this->receivePacket(rcv_packet) <= 0 || (rcv_packet->header).flags != SYN) {
        fprintf(stderr, "Error connecting to client.");
        exit(1);
    }
    fprintf(stdout, "SYN received.\n");
    delete rcv_packet; rcv_packet = NULL;

    snd_packet = Packet(SYNACK);
    if (this->sendPacket(snd_packet) <= 0) {
        fprintf(stderr, "Error sending SYNACK to client.\n");
        exit(1);
    }
    fprintf(stderr, "SYNACK sent.\n");

    if (this->receivePacket(rcv_packet) <= 0 || (rcv_packet->header).flags != ACK) {
        fprintf(stderr, "Error receiving ACK.\n");
        exit(1);
    }
    fprintf(stdout, "ACK received. Filename: %s\n", rcv_packet->payload);
    delete rcv_packet; rcv_packet = NULL;
}

// Send a packet to the connected client. Returns bytes sent on success, 0 otherwise.
int Server::sendPacket(Packet &packet) {
    // Copy packet header into a buffer.
    uint8_t* packet_buffer = new uint8_t[HEADER_SIZE + packet.packet_size];
    memcpy(packet_buffer, &packet.header, HEADER_SIZE);

    // Copy payload into buffer if it exists.
    if (packet.payload != NULL && packet.packet_size > HEADER_SIZE) {
        memcpy(&packet_buffer[HEADER_SIZE], packet.payload, packet.packet_size);
    }

    // Send the packet to the client.
    int bytessent = sendto(this->sockfd, packet_buffer, packet.packet_size, 0, (struct sockaddr*) &(this->clientinfo), sizeof(this->clientinfo));

    delete packet_buffer;
    return (bytessent > 0) ? bytessent : 0;
}

// Blocking operation!
// Wait for a packet from a client and store in buffer. Returns number of bytes read on success, 0 otherwise.
int Server::receivePacket(Packet* &packet, bool blocking) {
    uint8_t* buffer = new uint8_t[MAX_PKT_SIZE];
    socklen_t clientinfolen = sizeof(this->clientinfo);
    
    int bytesreceived = recvfrom(this->sockfd, buffer, MAX_PKT_SIZE, 0, (struct sockaddr*) &(this->clientinfo), &clientinfolen);

    // Error occured.
    if (bytesreceived <= 0) {
        return 0;
    }

    // Copy header.
    PacketHeader header;
    memcpy(&header, buffer, HEADER_SIZE);
    
    // Copy payload (it if exists) and set packet that was passed in.
    int payload_size = bytesreceived - HEADER_SIZE;
    if (payload_size > 0) {
        packet = new Packet(header, &buffer[HEADER_SIZE], payload_size);
    } else {
        packet = new Packet(header);
    }

    delete buffer;
    return bytesreceived;
}


