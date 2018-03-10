#include "rdt.h"

// Creates and binds a socket to the server and port.
Client::Client(char* serverhostname, char* port) {
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
    Packet p = Packet(SYN);
    p.payload = (uint8_t*) "hello\0";
    p.packet_size = HEADER_SIZE + 6; 
    if (this->sendPacket(p) <= 0) {
        fprintf(stderr, "Error sending SYN. Error: %d\n", errno);
        exit(1);
    }

    uint8_t* buffer = new uint8_t[MAX_PKT_SIZE];
    if (this->receivePacket(buffer, true) <= 0) {
        fprintf(stderr, "Error receiving ACK.\n");
        exit(1);
    }
    
    PacketHeader header;
    memcpy(&header, buffer, sizeof(header));
    if (!(header.flags ^ ACK)) {
        fprintf(stderr, "ACK Received.\n");
    }
}

// Prepares a packet to be sent.
int Client::sendPacket(Packet packet) {
    // Copy packet into a buffer.
    uint8_t* packet_buffer = new uint8_t[HEADER_SIZE + packet.packet_size];
    memcpy(packet_buffer, &packet.header, HEADER_SIZE); // Copy header into buffer.
    if (packet.payload != NULL && packet.packet_size > HEADER_SIZE) {
        memcpy(&packet_buffer[HEADER_SIZE], packet.payload, packet.packet_size - HEADER_SIZE); // Copy payload into buffer if it exists.
    }

    // Send the packet to the server.
    int bytessent = sendto(this->sockfd, packet_buffer, packet.packet_size, 0, (struct sockaddr*) &(this->serverinfo), sizeof(this->serverinfo));

    return (bytessent > 0) ? bytessent : 0;
}

// Blocking operation!
// Wait for a packet and store in buffer. Returns number of bytes read on success, 0 otherwise.
int Client::receivePacket(uint8_t* buffer, bool blocking = true) {
    socklen_t serverinfolen = sizeof(struct sockaddr);
    
    int bytesreceived = recvfrom(this->sockfd, buffer, MAX_PKT_SIZE, blocking? 0 : MSG_DONTWAIT, (struct sockaddr*) &(this->serverinfo), &serverinfolen);

    fprintf(stderr, "Received %d bytes: %s\n", bytesreceived, (char*) buffer);

    return (bytesreceived > 0) ? bytesreceived : 0;
}