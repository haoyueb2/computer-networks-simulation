#include <fstream>
#include <cstdio>
#include <string>
#include <iostream>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <cmath>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
using namespace std;
// To Do: Synchronize MSS
#define MaxSegmentSize 536
#define DefaultSsthreshByte 65535
enum PacketType {DATA, ACK, FIN, FINACK};

typedef struct DataSegment
{
    unsigned long long int seqNum;
    int length;
    int type;
    char data[MaxSegmentSize];
} DataSegment;

enum TcpState {SLOW_START, CONGESTION_AVOIDANCE, FAST_RECOVERY};

typedef vector<DataSegment*> PacketData;

typedef unsigned long long int tcp_int;

typedef struct TcpInfo
{
    tcp_int currSeqNum = 0;
    tcp_int sendBase = 0;
    int cwnd = 1;
    double ssthresh = (double) DefaultSsthreshByte / MaxSegmentSize;
    int dupAckCount = 0;
    TcpState currTcpState = SLOW_START;
    PacketData packets;
    int sockfd;
} TcpInfo;

PacketData readFile(string fileName, tcp_int byteToRead) {
    PacketData packets;
    ifstream inputFile (fileName, ios::in | ios::binary);
    tcp_int countByteRead = 0;
    tcp_int countSeqNum = 0;
    while (countByteRead < byteToRead) {
        DataSegment* segment = new DataSegment();
        inputFile.read(segment->data, min((tcp_int) MaxSegmentSize, byteToRead-countByteRead));
        int actualRead = inputFile.gcount();
        // Reached end of the file
        if (actualRead == 0) {
            break;
        }
        segment->seqNum = countSeqNum;
        segment->length = actualRead;
        segment->type = DATA;
        countSeqNum ++;
        countByteRead += actualRead;
        packets.push_back(segment);
    }
    return packets;
}

// Connect to the destination hostname and port, returning the socket fd.
int connect(char* hostname, unsigned short int hostUDPport) {
	struct addrinfo hints, *servinfo;
    char port[5];
    sprintf(port, "%d", hostUDPport);
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	int rv;
	if ((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
		return -1;
	}
	// loop through all the results and connect to the first we can
    int sockfd = 0;
	for(struct addrinfo* p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			continue;
		}

        // Set timeout value for the socket
        struct timeval rtt;
        rtt.tv_sec = 0;
        rtt.tv_usec = 40000;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &rtt, sizeof(rtt)) < 0) {
            continue;
        }

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			continue;
		}
        
        freeaddrinfo(servinfo);
        return sockfd;
	}
    return -1;
}

// Send all packets with seq_num in the range of [start_seq, end_seq)
int sendPacketInRange(TcpInfo* info, tcp_int start_seq, tcp_int end_seq) {
    PacketData packets = info->packets;
    char buffer[sizeof(DataSegment)];
    for (tcp_int i = start_seq; i < end_seq; i++) {
        int numbytes = 0;
        DataSegment* segment = info->packets[i];
        memcpy(buffer, segment, sizeof(DataSegment));
        if (send(info->sockfd, buffer, sizeof(DataSegment), 0) == -1) {
            return -1;
        }
    }
    return 0;
}

// If timeout from the other end, back to the SLOW_START phase and set window size back to 1. 
void handleTimeout(TcpInfo* info) {
    info->ssthresh = info->cwnd / 2.0;
    info->cwnd = 0;
    info->dupAckCount = 0;
    info->currTcpState = SLOW_START;
}

// Delete all acked packets, and set sendBase to the ack_num
void handleAckedPacket(TcpInfo* info, tcp_int ack_num) {
    PacketData packets = info->packets;
    for (tcp_int curr_seq = info->sendBase; curr_seq < ack_num; curr_seq++) {
        delete packets[curr_seq];
    }
    info->sendBase = ack_num;
}

// Update cwnd, ssthresh, currTcpState based on the ack_num, and handle the case when the ack_num = 3
void controlCongretion(TcpInfo* info, tcp_int ack_num) {
    tcp_int num_segment_acked = ack_num - info->sendBase;
    // dupAckCount count the consecutive ack on the same seq num. 
    // If other side starts to ack on different seq num, set dupAckCount = 0;
    if (num_segment_acked > 0) {
        info->dupAckCount = 0;
    } else {
        info->dupAckCount ++;
    }
    if (info->currTcpState == SLOW_START) {
        info->cwnd += num_segment_acked;
        if (info->cwnd > info->ssthresh) {
            info->cwnd = floor(info->ssthresh);
        }
    } else if (info->currTcpState == CONGESTION_AVOIDANCE) {
        info->cwnd += num_segment_acked / info->cwnd;
    } else {
        if (num_segment_acked > 0) {
            info->cwnd = info->ssthresh;
            info->currTcpState = CONGESTION_AVOIDANCE;
        } else {
            info->cwnd += 1;
        }
    }
    if (info->dupAckCount == 3) {
        info->ssthresh = info->cwnd / 2.0;
        info->cwnd = info->ssthresh + 3;
        info->currTcpState = FAST_RECOVERY;
    }
}

void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, tcp_int bytesToTransfer) {
    TcpInfo* info = new TcpInfo();
    info->packets = readFile(filename, bytesToTransfer);
    info->sockfd = connect(hostname, hostUDPport);
    if (info->sockfd == -1) {
        perror("Create Socket Failed");
        exit(1);
    }
    if (sendPacketInRange(info, 0, min(info->cwnd, (int) info->packets.size())) == -1) {
        perror("Send Packet Failed");
        exit(1);
    }
    char buffer[sizeof(DataSegment)];
    while (info->sendBase != info->packets.size()) {
        if (recv(info->sockfd, buffer, sizeof(DataSegment), 0) == -1) {
            if (errno != EAGAIN || errno != EWOULDBLOCK) {
                perror("Lost Connection");
                exit(1);
            }
            handleTimeout(info);
            sendPacketInRange(info, info->sendBase, info->sendBase+1);
        } else {
            DataSegment segment;
            memcpy(&segment, buffer, sizeof(DataSegment));
            tcp_int ack_num = segment.seqNum;
            handleAckedPacket(info, ack_num);
            if (ack_num < info->currSeqNum) {
                sendPacketInRange(info, ack_num, ack_num+1);
            }
            controlCongretion(info, ack_num);
            // Send more segments if allowed by cwnd
            info->sendBase = ack_num;
            tcp_int numSegmentToSend = info->cwnd - (info->currSeqNum - info->sendBase);
            tcp_int endingSeqNum = info->currSeqNum + numSegmentToSend;
            if (numSegmentToSend > 0 && endingSeqNum <= info->packets.size()) {
                sendPacketInRange(info, info->currSeqNum, endingSeqNum);
            }
        }
    }

    // Send Fin to the other side and wait for FinAck after sending all segments.
    DataSegment segment;
    segment.type = FIN;
    segment.length = 0;
    memcpy(buffer, &segment, sizeof(DataSegment));
    if (send(info->sockfd, buffer, sizeof(DataSegment), 0) == -1) {
        perror("Failed to send FIN packet");
        exit(1);
    }
    if (recv(info->sockfd, buffer, sizeof(DataSegment), 0) != -1) {
        DataSegment ack_segment;
        memcpy(&ack_segment, buffer, sizeof(DataSegment));
        if (ack_segment.type == FINACK) {
            cout << "Finished TCP Connection" << endl;
            return;
        }
    }
    perror("Failed to receive FINACK packet");
    exit(1);
}

int main(int argc, char** argv) {

    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int) atoi(argv[2]);
    numBytes = atoll(argv[4]);

    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);

    return (EXIT_SUCCESS);
}