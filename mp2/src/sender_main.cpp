#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
using namespace std;
// To Do: Synchronize MSS
#define MaxSegmentSize 10000
// #define DefaultSsthreshByte 65535
#define RTO 40000
enum PacketType { DATA,
    ACK,
    FIN,
    FINACK };

enum TcpState { SLOW_START,
    CONGESTION_AVOIDANCE,
    FAST_RECOVERY };

typedef unsigned long long int tcp_int;

typedef uint64_t time_dur;

typedef struct DataSegment {
    unsigned long long int seqNum;
    int length;
    int type;
    char data[MaxSegmentSize];
    time_dur sentTime;
} DataSegment;

typedef vector<DataSegment*> PacketData;

uint64_t getCurrTime() {
  using namespace std::chrono;
  return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

typedef struct TcpInfo {
    tcp_int currSeqNum = 0;
    tcp_int sendBase = 0;
    double cwnd = 1.0;
    double ssthresh = 64;
    // double ssthresh = 64;
    int dupAckCount = 0;
    TcpState currTcpState = SLOW_START;
    PacketData packets;
    int sockfd;
    time_dur timer = 0; 
    tcp_int byteToRead = 0;
    FILE* file;
} TcpInfo;

// PacketData readFile(string fileName, tcp_int byteToRead)
// {
//     PacketData packets;
//     ifstream inputFile(fileName, ios::in | ios::binary);
//     tcp_int countByteRead = 0;
//     tcp_int countSeqNum = 0;
//     while (countByteRead < byteToRead) {
//         DataSegment* segment = new DataSegment();
//         inputFile.read(segment->data, min((tcp_int)MaxSegmentSize, byteToRead - countByteRead));
//         int actualRead = inputFile.gcount();
//         // Reached end of the file
//         if (actualRead == 0) {
//             break;
//         }
//         segment->seqNum = countSeqNum;
//         segment->length = actualRead;
//         segment->type = DATA;
//         countSeqNum++;
//         countByteRead += actualRead;
//         packets.push_back(segment);
//     }
//     return packets;
// }
void readByteFromFile(TcpInfo* info, tcp_int seq_num) {
    if (seq_num < info->packets.size()) {
        return;
    }
    if (seq_num > info->packets.size()) {
        cout << "read file mistake" << endl;
        exit(0);
    }
    cout << "Read Seq Num " << seq_num << endl;
    DataSegment* segment = new DataSegment();
    tcp_int startByte = seq_num*MaxSegmentSize;
    int actualRead = fread(segment->data, 1, min((tcp_int)MaxSegmentSize, info->byteToRead - startByte), info->file);
    // Reached end of the file
    if (actualRead == 0) {
        cout << "Read to the end of file" << endl;
        return;
    }
    segment->seqNum = seq_num;
    segment->length = actualRead;
    segment->type = DATA;
    info->packets.push_back(segment);
}
// Connect to the destination hostname and port, returning the socket fd.
int connect(char* hostname, unsigned short int hostUDPport)
{
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
    for (struct addrinfo* p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                 p->ai_protocol))
            == -1) {
            continue;
        }

        // // Set timeout value for the socket
        // struct timeval rtt;
        // rtt.tv_sec = 0;
        // rtt.tv_usec = 0;
        // if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &rtt, sizeof(rtt)) < 0) {
        //     continue;
        // }
        
        // Set the socket to be non-blocking
        int flags = fcntl(sockfd,F_GETFL,0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

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
int sendPacketInRange(TcpInfo* info, tcp_int start_seq, tcp_int end_seq)
{
    PacketData packets = info->packets;
    char buffer[sizeof(DataSegment)];
    for (tcp_int i = start_seq; i < end_seq; i++) {
        readByteFromFile(info, i);
        DataSegment* segment = info->packets[i];
        segment->sentTime = getCurrTime();
        memcpy(buffer, segment, sizeof(DataSegment));
        if (send(info->sockfd, buffer, sizeof(DataSegment), 0) == -1) {
            return -1;
        }
    }
    cout <<  "sent packet from " << start_seq << " to " << end_seq << endl;
    // info->currSeqNum = max(end_seq, info->currSeqNum);
    return 0;
}

// If timeout from the other end, back to the SLOW_START phase and set window size back to 1.
bool checkAndHandleTimeout(TcpInfo* info)
{
    if (getCurrTime() - info->timer > RTO) {
        cout << "Found Time out" << endl;
        info->ssthresh = info->cwnd / 2.0;
        info->cwnd = 1;
        info->dupAckCount = 0;
        info->currTcpState = SLOW_START;
        sendPacketInRange(info, info->sendBase, info->sendBase+1);
        info->currSeqNum = info->sendBase+1;
        info->timer = getCurrTime();
        return true;
    }
    return false;
}

// Delete all acked packets, and set sendBase to the ack_num
void handleAckedPacket(TcpInfo* info, tcp_int ack_num)
{
    PacketData packets = info->packets;
    for (tcp_int curr_seq = info->sendBase; curr_seq < ack_num; curr_seq++) {
        delete packets[curr_seq];
    }
    info->sendBase = ack_num;
    // Since we may move currSeqNum to left in the case of timeout, 
    // it's possible that ack_num > currSeqNum 
    if (info->currSeqNum < info->sendBase) {
        info->currSeqNum = info->sendBase;
    }
}

// Update cwnd, ssthresh, currTcpState based on the ack_num, and handle the case when the ack_num = 3
void controlCongretion(TcpInfo* info, tcp_int ack_num)
{
    tcp_int num_pkt_acked = ack_num - info->sendBase;
    if (num_pkt_acked > 0) {
        info->timer = getCurrTime();
        info->dupAckCount = 0;
    } else {
        info->dupAckCount++;
    }
    if (info->currTcpState == SLOW_START && num_pkt_acked > 0) {
        info->cwnd += 1;
        if (info->cwnd > info->ssthresh) {
            info->currTcpState = CONGESTION_AVOIDANCE;
        }
    } else if (info->currTcpState == CONGESTION_AVOIDANCE && num_pkt_acked > 0) {
        info->cwnd += (1 / info->cwnd);
    } else if (info->currTcpState == FAST_RECOVERY) {
        if (num_pkt_acked > 0) {
            info->currTcpState = CONGESTION_AVOIDANCE;
        } else {
            info->cwnd += 1;
        }
    }
    if (info->dupAckCount == 3) {
        info->ssthresh = info->cwnd / 2.0;
        info->cwnd = info->ssthresh + 3;
        info->currTcpState = FAST_RECOVERY;
        sendPacketInRange(info, info->sendBase, info->sendBase + 1);
    }
    // DataSegment* frontPacket = info->packets[info->sendBase];
    // if (num_pkt_acked == 0 && getCurrTime() - frontPacket->sentTime > TimeOut) {
    //     cout << "Timeout" << endl;
    //     handleTimeout(info);
    // }
}

void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, tcp_int bytesToTransfer)
{
    TcpInfo* info = new TcpInfo();
    info->sockfd = connect(hostname, hostUDPport);
    info->file = fopen(filename, "r");
    info->byteToRead = bytesToTransfer;
    cout << "byte to Read: " << info->byteToRead << endl;
    if (info->sockfd == -1) {
        perror("Create Socket Failed");
        exit(1);
    }
    tcp_int num_packet = ceil((double) bytesToTransfer / MaxSegmentSize);
    int pktSent = min((tcp_int) info->cwnd, num_packet);
    info->timer = getCurrTime();
    if (sendPacketInRange(info, 0, pktSent) == -1) {
        perror("Send Packet Failed");
        exit(1);
    }
    info->currSeqNum = pktSent;
    char buffer[sizeof(DataSegment)];
    while (info->sendBase != num_packet) {
        if (info->cwnd < 1) {
            cout << "cwnd < 1" << endl;
            exit(0);
        }
        cout << "current iteration " << info->sendBase << " " << info->currSeqNum << endl;
        if (recv(info->sockfd, buffer, sizeof(DataSegment), 0) == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                checkAndHandleTimeout(info);
            } else {
                cout << "Socket Time out Error" << endl;
            }
        } else {
            if (checkAndHandleTimeout(info)) {
                cout << "Time out when received packets" << endl;
                continue;
            }
            DataSegment segment;
            memcpy(&segment, buffer, sizeof(DataSegment));
            tcp_int ack_num = segment.seqNum;
            cout << "ack_num " << ack_num << endl;
            if (ack_num < info->sendBase) {
                cout << "ack number smaller than sendbase" << endl;
            }
            controlCongretion(info, ack_num);
            handleAckedPacket(info, ack_num);
            // Send more segments if allowed by cwnd
            info->sendBase = ack_num;
            if (floor(info->cwnd) > (info->currSeqNum - info->sendBase)) {
                tcp_int numSegmentToSend = floor(info->cwnd) - (info->currSeqNum - info->sendBase);
                tcp_int endingSeqNum = info->currSeqNum + numSegmentToSend;
                endingSeqNum = min(endingSeqNum, num_packet);
                sendPacketInRange(info, info->currSeqNum, endingSeqNum);
                info->currSeqNum = endingSeqNum;
            }
        }
        cout << "cwnd = " << info->cwnd << endl;
        cout << "current state and dupackcount: " << info->currTcpState << " " << info->dupAckCount << endl;
    }
    fclose(info->file);
    // Send Fin to the other side and wait for FinAck after sending all segments.
    DataSegment segment;
    segment.type = FIN;
    segment.length = 0;
    memcpy(buffer, &segment, sizeof(DataSegment));
    if (send(info->sockfd, buffer, sizeof(DataSegment), 0) == -1) {
        perror("Failed to send FIN packet");
        exit(1);
    }
    while (true) {
        if (recv(info->sockfd, buffer, sizeof(DataSegment), 0) != -1) {
            cout << "received packet for Fin";
            DataSegment ack_segment;
            memcpy(&ack_segment, buffer, sizeof(DataSegment));
            if (ack_segment.type == FINACK) {
                cout << "Finished TCP Connection" << endl;
                return;
            }
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                break;
            }
        }
    }
    perror("Failed to receive FINACK packet");
    exit(1);
}

int main(int argc, char** argv)
{

    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int)atoi(argv[2]);
    numBytes = atoll(argv[4]);

    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);

    return (EXIT_SUCCESS);
}