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
#include <queue>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
using namespace std;
// To Do: Synchronize MSS
#define MaxSegmentSize 2000
#define NUM_THREADS 3
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
    time_dur sendTime;
    int length;
    int type;
    char data[MaxSegmentSize];
} DataSegment;

typedef vector<DataSegment*> PacketData;

uint64_t getCurrTime() {
  using namespace std::chrono;
  return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

typedef struct TcpInfo {
    tcp_int sendBase = 0;
    tcp_int maxSentSeqNum = 0;
    double cwnd = 1.0;
    double ssthresh = 64;
    // double ssthresh = 64;
    int dupAckCount = 0;
    TcpState currTcpState = SLOW_START;
    PacketData packets;
    int sockfd;
    tcp_int byteToRead = 0;
    char* fileName;
    bool finished;
    queue<DataSegment*> senderQueue;
    pthread_mutex_t readMutex;
    pthread_mutex_t sendMutex;
    vector<pthread_t> threads;
} TcpInfo;

void* readFile(void* info)
{
    TcpInfo* tcpInfo = (TcpInfo*) info;
    ifstream inputFile(tcpInfo->fileName, ios::in | ios::binary);
    tcp_int countByteRead = 0;
    tcp_int countSeqNum = 0;
    while (countByteRead < tcpInfo->byteToRead && !tcpInfo->finished) {
        DataSegment* segment = new DataSegment();
        inputFile.read(segment->data, min((tcp_int)MaxSegmentSize, tcpInfo->byteToRead - countByteRead));
        int actualRead = inputFile.gcount();
        // Reached end of the file
        if (actualRead == 0) {
            break;
        }
        segment->seqNum = countSeqNum;
        segment->length = actualRead;
        segment->type = DATA;
        countSeqNum++;
        countByteRead += actualRead;
        pthread_mutex_lock(&tcpInfo->readMutex);
        tcpInfo->packets.push_back(segment);
        pthread_mutex_unlock(&tcpInfo->readMutex);
    }
}

void* sendPackets(void* info) {
    TcpInfo* tcpInfo = (TcpInfo*) info;
    char buffer[sizeof(DataSegment)];
    while (!tcpInfo->finished) {
        if (tcpInfo->senderQueue.size() != 0) {
            pthread_mutex_lock(&tcpInfo->sendMutex);
            DataSegment* packetToSend = tcpInfo->senderQueue.front();
            tcpInfo->senderQueue.pop();
            pthread_mutex_unlock(&tcpInfo->sendMutex);
            packetToSend->sendTime = getCurrTime();
            memcpy(buffer, packetToSend, sizeof(DataSegment));
            if (send(tcpInfo->sockfd, buffer, sizeof(DataSegment), 0) == -1) {
                cout << "Send Packet " << packetToSend->seqNum << " Failed" << endl;
            }
        }
    }
}



void initializeThreads(TcpInfo* info) {
    pthread_mutex_t readMutex;
    info->readMutex = readMutex;
    pthread_mutex_t sendMutex;
    info->sendMutex = sendMutex;
    pthread_t  sendThread, readThread;
    info->threads.push_back(sendThread);
    info->threads.push_back(readThread);
    pthread_create(&readThread, NULL, readFile, info);
    sleep(5);
    pthread_create(&sendThread, NULL, sendPackets, info);
}
// void readByteFromFile(TcpInfo* info, tcp_int seq_num) {
//     if (seq_num < info->packets.size()) {
//         return;
//     }
//     if (seq_num > info->packets.size()) {
//         cout << "read file mistake" << endl;
//         exit(0);
//     }
//     cout << "Read Seq Num " << seq_num << endl;
//     DataSegment* segment = new DataSegment();
//     FILE* file = fopen(info->fileName, "r");
//     tcp_int startByte = seq_num*MaxSegmentSize;
//     fseek(file, startByte, SEEK_SET);
//     int actualRead = fread(segment->data, 1, min((tcp_int)MaxSegmentSize, info->byteToRead - startByte), file);
//     // Reached end of the file
//     if (actualRead == 0) {
//         cout << "Read to the end of file" << endl;
//         return;
//     }
//     segment->seqNum = seq_num;
//     segment->length = actualRead;
//     segment->type = DATA;
//     info->packets.push_back(segment);
//     fclose(file);
// }
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
    while (end_seq >= info->packets.size()) {
        sleep(1);
    }
    PacketData packets = info->packets;
    pthread_mutex_lock(&info->sendMutex);
    pthread_mutex_lock(&info->readMutex);
    for (tcp_int i = start_seq; i < end_seq; i++) {
        DataSegment* segment = info->packets[i];
        info->senderQueue.push(segment);
    }
    pthread_mutex_unlock(&info->sendMutex);
    pthread_mutex_unlock(&info->readMutex);
    cout <<  "sent packet from " << start_seq << " to " << end_seq << endl;
    return 0;
}

// If timeout from the other end, back to the SLOW_START phase and set window size back to 1.
bool checkAndHandleTimeout(TcpInfo* info)
{
    if (getCurrTime() - (info->packets[info->sendBase])->sendTime > RTO) {
        cout << "Found Time out" << endl;
        info->ssthresh = info->cwnd / 2.0;
        info->cwnd = 1;
        info->dupAckCount = 0;
        info->currTcpState = SLOW_START;
        sendPacketInRange(info, info->sendBase, info->sendBase+1);
        info->packets[info->sendBase]->sendTime = getCurrTime();
        info->maxSentSeqNum = info->sendBase;
        return true;
    }
    return false;
}

// Delete all acked packets, and set sendBase to the ack_num
void handleAckedPacket(TcpInfo* info, tcp_int ack_num)
{
    // If no new packet is acked by the new ack num, do nothing.
    if (ack_num - info->sendBase <= 0) {
        return;
    }
    PacketData packets = info->packets;
    for (tcp_int curr_seq = info->sendBase; curr_seq < ack_num; curr_seq++) {
        delete packets[curr_seq];
    }
    info->sendBase = ack_num;
}

// Update cwnd, ssthresh, currTcpState based on the ack_num, and handle the case when the ack_num = 3
void controlCongretion(TcpInfo* info, tcp_int ack_num)
{
    tcp_int num_pkt_acked = ack_num - info->sendBase;
    if (num_pkt_acked > 0) {
        info->timer = getCurrTime();
        info->dupAckCount = 0;
    } else if (num_pkt_acked == 0) {
        info->dupAckCount++;
    } else {
        return;
    }
    if (info->currTcpState == SLOW_START && num_pkt_acked > 0) {
        info->cwnd += num_pkt_acked;
        if (info->cwnd > info->ssthresh) {
            info->currTcpState = CONGESTION_AVOIDANCE;
        }
    } else if (info->currTcpState == CONGESTION_AVOIDANCE && num_pkt_acked > 0) {
        info->cwnd += (num_pkt_acked / floor(info->cwnd));
    } else if (info->currTcpState == FAST_RECOVERY) {
        if (num_pkt_acked > 0) {
            info->currTcpState = CONGESTION_AVOIDANCE;
            info->cwnd = info->ssthresh;
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
}

void waitAndCloseThreads(TcpInfo* info) {
    info->finished = true;
    pthread_join(info->threads[0], NULL);
    pthread_join(info->threads[1], NULL);
    pthread_mutex_destroy(&info->readMutex);
    pthread_mutex_destroy(&info->sendMutex);
}

void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, tcp_int bytesToTransfer)
{
    TcpInfo* info = new TcpInfo();
    initializeThreads(info);
    info->sockfd = connect(hostname, hostUDPport);
    info->fileName = filename;
    info->byteToRead = bytesToTransfer;
    cout << "byte to Read: " << info->byteToRead << endl;
    if (info->sockfd == -1) {
        perror("Create Socket Failed");
        exit(1);
    }
    tcp_int num_packet = ceil((double) bytesToTransfer / MaxSegmentSize);
    int pktSent = min((tcp_int) info->cwnd, num_packet);
    if (sendPacketInRange(info, 0, pktSent) == -1) {
        perror("Send Packet Failed");
        exit(1);
    }
    info->maxSentSeqNum = pktSent - 1;
    char buffer[sizeof(DataSegment)];
    while (info->sendBase != num_packet) {
        cout << "curr window: " << info->sendBase << " " << info->sendBase+info->cwnd << endl;
        cout << "window size: " << info->cwnd << endl;
        cout << "Max Sent Seq Num: "<< info->maxSentSeqNum << endl;
        cout << "current state and dupackcount: " << info->currTcpState << " " << info->dupAckCount << endl;
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
            controlCongretion(info, ack_num);
            handleAckedPacket(info, ack_num);
            // Send more segments if allowed by cwnd
            tcp_int numPacketToSend = info->sendBase + floor(info->cwnd) - 1 - info->maxSentSeqNum;
            if (numPacketToSend > 0) {
                tcp_int endingSeqNum = min(info->maxSentSeqNum + numPacketToSend + 1, num_packet);
                sendPacketInRange(info, info->maxSentSeqNum+1, endingSeqNum);
                for (tcp_int i = info->maxSentSeqNum+1; i < endingSeqNum; i++) {
                    info->packets[i]->sendTime = getCurrTime();
                }
                info->maxSentSeqNum = endingSeqNum - 1;
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
    while (true) {
        if (recv(info->sockfd, buffer, sizeof(DataSegment), 0) != -1) {
            cout << "received packet for Fin" << endl;
            DataSegment ack_segment;
            memcpy(&ack_segment, buffer, sizeof(DataSegment));
            if (ack_segment.type == FINACK) {
                cout << "Finished TCP Connection" << endl;
                waitAndCloseThreads(info);
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