#include "rdt.h"

// Creates and binds a socket to the server and port.
Client::Client(char* serverhostname, char* port, char* filename) {
    this->sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  // Create UDP socket.
    if (this->sockfd < 0) {
        fprintf(stderr, "ERROR: Unable to open socket.\n");
        exit(1);
    }

    // Fill in address info.
    memset((char*) &(this->serverinfo), 0, sizeof(this->serverinfo));

    this->serverinfo.sin_family = AF_INET;
    this->serverinfo.sin_port = htons(atoi(port));

    if (inet_aton(serverhostname, &(this->serverinfo.sin_addr)) <= 0) {
        fprintf(stderr, "Error converting hostname.");
        exit(1);
    }

    // Attempt to connect to server (sending TCP handshake).
    Packet snd_packet; 
    Packet* rcv_packet = NULL;

    snd_packet = Packet(SYN);
    if (this->sendPacket(snd_packet) <= 0) {
        fprintf(stderr, "Error sending SYN. Error: %d\n", errno);
        exit(1);
    }
    fprintf(stderr, "SYN sent.\n");


    if (this->receivePacket(rcv_packet) <= 0 || (rcv_packet->header).flags != SYNACK) {
        fprintf(stderr, "Error receiving SYNACK.\n");
        exit(1);
    }
    fprintf(stderr, "SYNACK received.\n");
    delete rcv_packet; rcv_packet = NULL;

    snd_packet = Packet(ACK, 0, 0, (uint8_t*) filename, strlen(filename) + 1);
    if (this->sendPacket(snd_packet) <= 0) {
        fprintf(stderr, "Error sending ACK + filename.\n");
        exit(1);
    }
    fprintf(stderr, "ACK + filename sent.\n");
}

// Prepares a packet to be sent.
int Client::sendPacket(Packet &packet) {
    // Copy packet header into a buffer.
    uint8_t* packet_buffer = new uint8_t[HEADER_SIZE + packet.packet_size];
    memcpy(packet_buffer, &(packet.header), HEADER_SIZE);
    
    // Copy payload into buffer if it exists.
    if (packet.payload != NULL && packet.packet_size > HEADER_SIZE) {
        memcpy(&packet_buffer[HEADER_SIZE], packet.payload, packet.packet_size - HEADER_SIZE);
    }

    // Send the packet to the server.
    int bytessent = sendto(this->sockfd, packet_buffer, packet.packet_size, 0, (struct sockaddr*) &(this->serverinfo), sizeof(this->serverinfo));

    delete packet_buffer;
    return (bytessent > 0) ? bytessent : 0;
}

// Blocking operation!
// Wait for a packet and store in buffer. Returns number of bytes read on success, 0 otherwise.
int Client::receivePacket(Packet* &packet, bool blocking) {
    uint8_t* buffer = new uint8_t[MAX_PKT_SIZE];
    socklen_t serverinfolen = sizeof(struct sockaddr);
    
    int bytesreceived = recvfrom(this->sockfd, buffer, MAX_PKT_SIZE, blocking? 0 : MSG_DONTWAIT, (struct sockaddr*) &(this->serverinfo), &serverinfolen);

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