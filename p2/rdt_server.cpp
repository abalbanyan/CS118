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
    int receivestatus;

    // Listen for TCP connection by waiting for SYN.
    do {
        receivestatus = this->receivePacket(rcv_packet);
    } while (receivestatus <= 0 || (rcv_packet->header).flags != SYN);

    // Send SYNACK with random initial seqno, and wait for ACK.
    srand(time(NULL));
    this->nextseqno = rand() % MAX_SEQNO; // Set initial sequence number randomly.
    snd_packet = Packet(SYNACK, this->nextseqno, rcv_packet->header.seqno + 1);
    this->nextseqno = (this->nextseqno + 1) % MAX_SEQNO;

    delete rcv_packet;

    do {
        this->sendPacket(snd_packet);
        receivestatus = this->receivePacket(rcv_packet, true, TIMEOUT);
    } while (receivestatus <= 0);

    if ((rcv_packet->header).flags != ACK || rcv_packet->payload == NULL) {
        fprintf(stderr, "Error receiving ACK. No filename included or not ACK.\n");
        exit(1);
    }
    this->filename_ackno = snd_packet.header.seqno;

    fprintf(stdout, "Connected to client!\n");

    // Connection has been established. Send requested file to client.
    if (this->sendFile((char*) rcv_packet->payload) <= 0) {
        fprintf(stderr, "Error sending file to client.\n");
        exit(1);
    }
    delete rcv_packet;

    // TODO: Not sure if we're supposed to send a FIN here or in sendFileChunk when the last chunk is sent.
    snd_packet = Packet(FIN, this->nextseqno);
    this->sendPacket(snd_packet);
    // Finished sending file. Wait for FINACK then close connection (we close anyway if it takes too long).
    bool fin_ackreceived = false;
    bool fin_finreceived = false;
    do {
        receivestatus = this->receivePacket(rcv_packet, true, TIMEOUT);
        if (receivestatus > 0 && rcv_packet->header.flags & FIN)
            fin_finreceived = true;
        if (receivestatus > 0 && rcv_packet->header.flags & ACK)
            fin_ackreceived = true;
        delete rcv_packet;
    } while (receivestatus > 0 && ( !fin_ackreceived || !fin_finreceived ));

    // Send acknowledgement of FIN (if we didn't just timeout).
    if (receivestatus > 0) {
        snd_packet = Packet(ACK);
        this->sendPacket(snd_packet);
    }
    // Close connection.
    fprintf(stdout, "Closing connection. Goodbye.\n");
    close(this->sockfd);
    exit(0);
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

    if (bytessent <= 0) {
        fprintf(stderr, "Error sending packet with seqno %d. Exiting.\n", packet.header.seqno);
        exit(1);
    }

    // Reset timeout on packet.
    gettimeofday(&(packet.timeout), NULL);
    timeradd(&TIMEOUT, &(packet.timeout), &(packet.timeout));

    // Print status message.
    const char* type = (retransmission)? "Retransmission" : (packet.header.flags & SYN)? "SYN" : (packet.header.flags & FIN)? "FIN" : "";
    fprintf(stdout, "Sending packet %d %d %s\n", packet.header.seqno, this->cwnd * MAX_PKT_SIZE, type);

    delete packet_buffer;
    return (bytessent > 0) ? bytessent : 0;
}

// Set blocking to false to make this a non-blocking operation.
// Wait for a packet from a client and store in buffer. Returns number of bytes read on success, 0 otherwise.
int Server::receivePacket(Packet* &packet, bool blocking, struct timeval timeout) {
    uint8_t* buffer = new uint8_t[MAX_PKT_SIZE];
    socklen_t clientinfolen = sizeof(this->clientinfo);
    
    setsockopt(this->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    int bytesreceived = recvfrom(this->sockfd, buffer, MAX_PKT_SIZE, blocking? 0 : MSG_DONTWAIT,
                            (struct sockaddr*) &(this->clientinfo), &clientinfolen);

    // Error occured (possibly a timeout).
    if (bytesreceived <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1; // Timeout occured.
        } else {
            // Some other error occured.
            fprintf(stderr, "Error sending packet with ackno %d. Exiting.\n", packet->header.ackno);
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
    fprintf(stdout, "Receiving packet %d\n", packet->header.ackno);

    delete buffer;
    return bytesreceived;
}

// Reads the next MAX_PKT_SIZE_SANS_HEADER bytes from the open file, then
// creates a packet with the data read from the file, and sends it.
// Returns true when done reading file.
bool Server::sendFileChunk() {
    // Allocate space for buffer.
    uint8_t* buffer = new uint8_t[MAX_PKT_SIZE_SANS_HEADER];
    memset(buffer, '\0', MAX_PKT_SIZE_SANS_HEADER);

    bool done_reading = false;
    ssize_t bytestoread = this->filesize - this->file.tellg();
    int flag = (this->filename_acked)? 0 : ACK;
    int ackno = (this->filename_acked)? 0 : this->filename_ackno;

    if (bytestoread > MAX_PKT_SIZE_SANS_HEADER) {
        bytestoread = MAX_PKT_SIZE_SANS_HEADER;
    } else {
        done_reading = true;
        // TODO: Not sure if we're supposed to send FIN with final packet of file or not. Ask TA.
        // flag = FIN; // We've come to the last chunk of the file. Send a FIN.
    }

    this->file.read((char*) buffer, bytestoread);
    Packet* packet = new Packet(flag, this->nextseqno, ackno, buffer, bytestoread);
    this->sendPacket(*packet); // Send as soon as it is made available.
    if (this->window.empty()) {
        this->baseseqno = packet->header.seqno;
    }
    this->window.push_back(packet);

    this->nextseqno = (this->nextseqno + bytestoread) % MAX_SEQNO;

    return done_reading;
}

// Reliable data transfer. Contains event loop.
int Server::sendFile(char* filename) {
    // Open specified file.
    this->file.open(filename, ios::in | ios::binary);
    if (!this->file || !this->file.is_open()) {
        fprintf(stderr, "Error opening file. Error: %d\n", errno);
        exit(1);
    }

    // Determine filesize.
    this->file.seekg(0, this->file.end);
    this->filesize = this->file.tellg();
    this->file.seekg(0, this->file.beg);

    // Begin sending file packet by packet. Send FIN when done.
    bool done_reading = false;
    struct timeval current_time;
    struct timeval closest_timeout;
    struct timeval packet_timeout;
    struct timeval double_timeout;
    timeradd(&TIMEOUT, &TIMEOUT, &double_timeout);
    Packet* closest_packet;
    Packet* rcv_packet = NULL;
    // Event loop.
    // Each loop, window is filled, and either one packet is received, or a timeout occurs.
    while (1) {
        // Fill cwnd.
        while (!done_reading && this->window.size() < this->cwnd) {
            done_reading = (done_reading)? done_reading : this->sendFileChunk();
        }

        // Find closest timeout time.
        gettimeofday(&current_time, NULL);
        closest_timeout = double_timeout;
        for (Packet* packet : this->window) {
            timersub(&(packet->timeout), &current_time, &packet_timeout);
            if (timercmp(&packet_timeout, &closest_timeout, <)) {
                closest_timeout = packet_timeout;
                closest_packet = packet;
            }
        }

        // Wait for an ACK, or a timeout. Retransmit on timeout.
        if (!this->window.empty()) {
            if (this->receivePacket(rcv_packet, true, closest_timeout) == -1) {
                // Timeout occured. Retransmit the packet.
                this->sendPacket(*closest_packet, true);
            } else {
                // ACK received.
                // Mark appropriate packet as ACKed.
                for (Packet* packet : this->window) {
                    if (packet->header.seqno == rcv_packet->header.ackno) {
                        packet->acked = true;
                    }
                }
                // If ACKno == baseseqno, delete all ACKed packets from beginning of window to first unACKed packet, and update baseseqno.
                if (this->baseseqno == rcv_packet->header.ackno) {
                    for (vector<Packet*>::iterator it = this->window.begin(); it != this->window.end();) {
                        if ((*it)->acked) {
                            delete *it;
                            this->window.erase(it);
                        } else {
                            this->baseseqno = (*it)->header.seqno;
                            break;
                        }
                    }
                }
                this->filename_acked = true;
            }
        } else if (done_reading) {
            break; // Done transmitting file.
        } else {
            fprintf(stderr, "Shouldn't be here.");
        }
    }
    return 1;
}
