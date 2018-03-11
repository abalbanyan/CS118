#include "rdt.h"

// Creates and binds a socket to the server port.
Server::Server(char* src_port) {
    this->sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  // Create UDP socket.
    if (this->sockfd < 0) {
        fprintf(stderr, "ERROR: Unable to open socket.");
        exit(1);
    }

    // Fill in address info.
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
    delete rcv_packet; rcv_packet = NULL;

    snd_packet = Packet(SYNACK);
    if (this->sendPacket(snd_packet) <= 0) {
        fprintf(stderr, "Error sending SYNACK to client.\n");
        exit(1);
    }

    if (this->receivePacket(rcv_packet) <= 0 || (rcv_packet->header).flags != ACK || rcv_packet->payload == NULL) {
        fprintf(stderr, "Error receiving ACK.\n");
        exit(1);
    }

    // Connection has been established. Send requested file to client.
    if (this->sendFile((char*) rcv_packet->payload) <= 0) {
        fprintf(stderr, "Error sending file to client.\n");
    }
    delete rcv_packet;

    // Finished sending file. Close connection by sending FIN.
    snd_packet = Packet(FIN);
    if (this->sendPacket(snd_packet) <= 0) {
        fprintf(stderr, "Error sending FIN to client.\n");
        exit(1);
    }

    // Wait for FIN, ACK.
    bool fin_ackreceived = false;
    bool fin_finreceived = false;
    do {
        if (this->receivePacket(rcv_packet) <= 0) {
            fprintf(stderr, "Error closing connection. (receiving FIN or ACK from client).\n");
            exit(1);
        }
        if (rcv_packet->header.flags & FIN) fin_finreceived = true;
        if (rcv_packet->header.flags & ACK) fin_ackreceived = true;
        delete rcv_packet;
    } while (!fin_ackreceived || !fin_finreceived);

    // Send acknowledgement of FIN. Close connection (TODO: After timed wait?).
    snd_packet = Packet(ACK);
    this->sendPacket(snd_packet);
    fprintf(stdout, "Closing connection. Goodbye.\n");
    close(sockfd);
}

// Send a packet to the connected client. Returns bytes sent on success, 0 otherwise.
int Server::sendPacket(Packet &packet, bool retransmission) {
    // Copy packet header into a buffer.
    uint8_t* packet_buffer = new uint8_t[HEADER_SIZE + packet.packet_size];
    memcpy(packet_buffer, &packet.header, HEADER_SIZE);

    // Copy payload into buffer if it exists.
    if (packet.payload != NULL && packet.packet_size > HEADER_SIZE) {
        memcpy(&packet_buffer[HEADER_SIZE], packet.payload, packet.packet_size);
    }

    // Send the packet to the client.
    int bytessent = sendto(this->sockfd, packet_buffer, packet.packet_size, 0, (struct sockaddr*) &(this->clientinfo), sizeof(this->clientinfo));

    // Print status message.
    const char* type = (retransmission)? "Retransmission" : (packet.header.flags == SYN)? "SYN" : (packet.header.flags == FIN)? "FIN" : "";
    fprintf(stdout, "Sending packet %d %d %s\n", packet.header.seqno, this->cwnd, type);

    delete packet_buffer;
    return (bytessent > 0) ? bytessent : 0;
}

// Set blocking to false to make this a non-blocking operation.
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

    // Print status message.
    fprintf(stdout, "Receiving packet %d\n", packet->header.ackno);

    delete buffer;
    return bytesreceived;
}

// Reads the next MAX_PKT_SIZE_SANS_HEADER bytes from the open file, then creates a packet with the data read from the file.
int Server::readFileChunk(Packet* &packet) {
    // Allocate space for buffer.
    uint8_t* buffer = new uint8_t[MAX_PKT_SIZE_SANS_HEADER];
    memset(buffer, '\0', MAX_PKT_SIZE_SANS_HEADER);

    ssize_t bytestoread = this->filesize - this->file.tellg();
    bytestoread = (bytestoread >= MAX_PKT_SIZE_SANS_HEADER)? MAX_PKT_SIZE_SANS_HEADER : bytestoread;

    if (bytestoread > 0) {
        this->file.read((char*) buffer, bytestoread);
        packet = new Packet(0, this->nextseq, 0, buffer, bytestoread);
        this->nextseq += bytestoread;
        return bytestoread;
    } else {
        return 0;
    }
}

int Server::sendFile(char* filename) {
    // Open specified file.
    this->file.open(filename, ios::in | ios::binary);
    if (!this->file || !this->file.is_open()) {
        fprintf(stderr, "Error opening file. Error: %d\n", errno);
        exit(1);
    }

    // Determine filesize;
    this->file.seekg(0, this->file.end);
    this->filesize = this->file.tellg();
    this->file.seekg(0, this->file.beg);

    Packet* snd_packet;
    while (this->readFileChunk(snd_packet) > 0) {
        this->sendPacket(*snd_packet);
        delete snd_packet;
    }
    return 1;
}
