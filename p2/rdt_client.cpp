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

    // Send SYN with initial seqno.
    srand(time(NULL));
    nextseqno = rand() % MAX_SEQNO; // Set initial sequence number randomly.
    snd_packet = Packet(SYN, nextseqno, 0);

    do {
        delete rcv_packet; rcv_packet = NULL;
        this->sendPacket(snd_packet); // Send SYN.
        receivestatus = this->receivePacket(rcv_packet, true, TIMEOUT); // Wait for SYNACK.
    } while (receivestatus <= 0 || (rcv_packet->header).flags != SYNACK || rcv_packet->header.ackno != snd_packet.header.seqno);

    uint16_t filenameseqno = (nextseqno + 1) % MAX_SEQNO;

    // Send ACK for SYNACK, include filename.
    snd_packet = Packet(ACK, filenameseqno, rcv_packet->header.seqno, (uint8_t*) filename, strlen(filename) + 1);
    this->rcv_base = (rcv_packet->header.seqno + 1) % MAX_SEQNO;
    do {
        delete rcv_packet; rcv_packet = NULL;
        this->sendPacket(snd_packet); // Send the ACK with filename.
        receivestatus = this->receivePacket(rcv_packet, true, TIMEOUT); // Wait for first ACK of filename.
    } while (receivestatus <= 0 || rcv_packet->header.flags != ACK || rcv_packet->header.ackno != filenameseqno);

    // Begin accepting requested file.
    ofstream outputfile("received.data", ios::trunc | ios::out | ios::binary);
    // Accept the rest of the file.
    while(1) {
        if (rcv_packet == NULL) {
            receivestatus = this->receivePacket(rcv_packet, true, TIMEOUT);
        } else if (!(rcv_packet->header.flags & FIN)) {
            // Normal packet.
            if (!this->isDuplicatePacket(rcv_packet)) {
                // This block of code is here to write out-of-order packets in the correct order to file.
                if (rcv_packet->header.seqno != this->rcv_base) {
                    this->rcv_window.push_back(new Packet(rcv_packet)); // Add to buffer.
                } else {
                    // Write packet to file immediately and update rcv_base.
                    writePacketToFile(outputfile, rcv_packet);
                    this->rcv_base = (this->rcv_base + rcv_packet->packet_size - (INCHEADER? 0 : HEADER_SIZE)) % MAX_SEQNO;

                    // Write all previously buffered and consecutively numbered (beginning with rcv_base) packets.
                    bool removed_one;
                    do {
                        removed_one = false;
                        for (vector<Packet*>::iterator it = this->rcv_window.begin(); it != this->rcv_window.end();) {
                            if ((*it)->header.seqno == this->rcv_base) {
                                writePacketToFile(outputfile, *it);
                                this->rcv_base = (this->rcv_base + (*it)->packet_size - (INCHEADER? 0 : HEADER_SIZE)) % MAX_SEQNO;
                                delete *it;
                                this->rcv_window.erase(it);
                                removed_one = true;
                            } else {
                                ++it;
                            }
                        }
                    } while (removed_one);
                }
            }
            // ACK the received packet. 
            Packet ack = Packet(ACK, 0, rcv_packet->header.seqno);
            this->sendPacket(ack);

            delete rcv_packet; rcv_packet = NULL;

        } else {
            // Server is trying to close connection. Respond with FINACK.
            uint16_t finackseqno = (filenameseqno + strlen(filename) + 1) % MAX_SEQNO;
            snd_packet = Packet(FINACK, finackseqno, rcv_packet->header.seqno);
            this->sendPacket(snd_packet);

            // Wait for ACK.
            struct timeval timedwaittimeout;
            timeradd(&TIMEOUT, &TIMEOUT, &timedwaittimeout);
            while (this->receivePacket(rcv_packet, true, timedwaittimeout) > 0) {
                if (rcv_packet->header.flags != ACK)
                    break;
            }
            if (rcv_packet != NULL) {
                delete rcv_packet; rcv_packet = NULL;
            }

            outputfile.close();
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
            // TODO: Set packet to NULL?
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

void Client::writePacketToFile(ofstream &file, Packet* &packet) {
    for (int i = 0; i < (packet->packet_size - HEADER_SIZE); i++) {
        file << packet->payload[i];
    }
}

bool Client::isDuplicatePacket(Packet* &packet) {
    for (uint16_t seqno : this->last_seqnos) {
        if (packet->header.seqno == seqno) {
            return true;
        }
    }
    // Not a duplicate. Add to last_seqnos.
    this->last_seqnos.push_back(packet->header.seqno);
    if (this->last_seqnos.size() > MAX_SEQNO/MAX_PKT_SIZE_SANS_HEADER) {
        this->last_seqnos.erase(this->last_seqnos.begin());
    }
    return false;
}