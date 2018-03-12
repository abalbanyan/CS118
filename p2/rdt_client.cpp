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
    int receivestatus;
    uint16_t nextseqno;
    uint16_t filenameackno;

    // Send SYN with initial seqno.
    srand(time(NULL));
    nextseqno = rand() % MAX_SEQNO; // Set initial sequence number randomly.
    snd_packet = Packet(SYN, nextseqno, 0);
    nextseqno = (nextseqno + 1) % MAX_SEQNO;
    do {
        this->sendPacket(snd_packet);
        // Wait for SYNACK.
        receivestatus = this->receivePacket(rcv_packet, true, TIMEOUT);
        if (receivestatus > 0 
                && ((rcv_packet->header).flags != SYNACK || rcv_packet->header.ackno != nextseqno)) {
            fprintf(stderr, "Error: Expected SYNACK, received something else.\n");
            exit(1);
        }
        if (receivestatus > 0)
            filenameackno = (rcv_packet->header.seqno + 1) % MAX_SEQNO;
        delete rcv_packet;
    } while (receivestatus <= 0);
    
    fprintf(stdout, "Connected to server!\n");
    // Begin accepting requested file.
    ofstream receivedfile("received.data");

    // Send ACK with filename.
    snd_packet = Packet(ACK, nextseqno, filenameackno, (uint8_t*) filename, strlen(filename) + 1);
    do {
        this->sendPacket(snd_packet);
        // Wait for first chunk (which also contains filename ACK).
        receivestatus = this->receivePacket(rcv_packet, true, TIMEOUT);
        if (receivestatus <= 0) {
            delete rcv_packet;
        } else if (rcv_packet->header.flags & ACK) {
            break; // Start accepting the rest of the file.
        } else {
            fprintf(stderr, "Error receiving ACK for filename.\n");
            exit(1);
        }
    } while (receivestatus <= 0);

    // Accept the rest of the file.
    while(1) {
        if (rcv_packet == NULL) {
            receivestatus = this->receivePacket(rcv_packet);
        }
        if (!(rcv_packet->header.flags & FIN)) {
            // Normal packet.
            if (this->received_packets.find(rcv_packet->header.seqno) == this->received_packets.end()) {
                // Write to file. TODO: Out of order packets?
                for (int i = 0; i < (rcv_packet->packet_size - HEADER_SIZE); i++) {
                    receivedfile << rcv_packet->payload[i];
                }
                this->received_packets.insert(rcv_packet->header.seqno);
            }
            // ACK packet. 
            Packet ack = Packet(ACK, 0, rcv_packet->header.seqno);
            this->sendPacket(ack);

            delete rcv_packet; rcv_packet = NULL;

        } else {            
            // Server is trying to close connection. Acknowledge the FIN.
            // TODO: Fix this if we need to include payload in the FIN.

            snd_packet = Packet(ACK);
            this->sendPacket(snd_packet);
            snd_packet = Packet(FIN);
            this->sendPacket(snd_packet);
            
            this->receivePacket(rcv_packet, true, TIMEOUT); // Can just time out.

            if (rcv_packet != NULL) delete rcv_packet;
            receivedfile.close();
            close(sockfd);
            exit(0);
        }
    }
}

// Prepares a packet to be sent.
int Client::sendPacket(Packet &packet, bool retransmission) {
    // Copy packet header into a buffer.
    uint8_t* packet_buffer = new uint8_t[HEADER_SIZE + packet.packet_size];
    memcpy(packet_buffer, &(packet.header), HEADER_SIZE);
    
    // Copy payload into buffer if it exists.
    if (packet.payload != NULL && packet.packet_size > HEADER_SIZE) {
        memcpy(&packet_buffer[HEADER_SIZE], packet.payload, packet.packet_size - HEADER_SIZE);
    }

    // Send the packet to the server.
    int bytessent = sendto(this->sockfd, packet_buffer, packet.packet_size, 0, (struct sockaddr*) &(this->serverinfo), sizeof(this->serverinfo));

    if (bytessent <= 0) {
        fprintf(stderr, "Error sending packet with ackno %d. Exiting.\n", packet.header.ackno);
        exit(1);
    }

    // Print status message.
    if (packet.header.flags & SYN) {
        fprintf(stdout, "Sending packet SYN\n");
    } else {
        const char* type = (retransmission)? "Retransmission" : (packet.header.flags & FIN)? "FIN" : "";
        fprintf(stdout, "Sending packet %d %s\n", packet.header.ackno, type);
    }

    delete packet_buffer;
    return (bytessent > 0) ? bytessent : 0;
}

// Set blocking to false to make this a non-blocking operation.
// Wait for a packet and store in buffer. Returns number of bytes read on success, 0 otherwise.
int Client::receivePacket(Packet* &packet, bool blocking, struct timeval timeout) {
    uint8_t* buffer = new uint8_t[MAX_PKT_SIZE];
    socklen_t serverinfolen = sizeof(struct sockaddr);
    
    setsockopt(this->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    int bytesreceived = recvfrom(this->sockfd, buffer, MAX_PKT_SIZE, blocking? 0 : MSG_DONTWAIT,
                            (struct sockaddr*) &(this->serverinfo), &serverinfolen);

    // Error occured (possibly a timeout).
    if (bytesreceived <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1; // Timeout occured.
        } else {
            // Some other error occured.
            fprintf(stderr, "Error sending packet with seqno %d. Exiting.\n", packet->header.seqno);
            exit(1);
        }
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
    fprintf(stdout, "Receiving packet %d\n", packet->header.seqno);

    delete buffer;
    return bytesreceived;
}