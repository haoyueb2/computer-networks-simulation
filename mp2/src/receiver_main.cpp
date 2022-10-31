/* 
 * File:   receiver_main.c
 * Author: 
 *
 * Created on
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <unordered_map>

#define MSS 10000
enum PacketType {DATA, ACK, FIN, FINACK};
using namespace std;

struct sockaddr_in si_me, si_other;
int s, slen;

void diep(char *s) {
    perror(s);
    exit(1);
}
typedef uint64_t time_dur;
typedef struct {
    unsigned long long int num;
    int size;
    int type;
    char data[MSS];
    typedef uint64_t time_dur;
} packet;

unordered_map<int, packet*> buffer;

void writePacketToFile(int num, ofstream& write)
{
    packet* p = buffer[num];
    write.write(p->data, p->size);
}

void sendAck(int num, PacketType type)
{
    int numBytes;
    packet p;
    p.type = type;
    p.num = num;
    if ((numBytes = sendto(s, (void*)&p, sizeof p, 0, (struct sockaddr*)&si_other, sizeof(si_other))) == -1) {
        diep("sendto");
    }
    cout << "send Ack " << num << endl;
}

void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {
    
    slen = sizeof (si_other);


    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_me, 0, sizeof (si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");
    if (bind(s, (struct sockaddr*) &si_me, sizeof (si_me)) == -1)
        diep("bind");


	/* Now receive data and send acknowledgements */    
    int numBytes;
    int expectedNum = 0;
    ofstream write;
    write.open(destinationFile);
    while (true) {
        packet* p = new packet();
        if ((numBytes = recvfrom(s, p, sizeof(packet), 0, (struct sockaddr*)&si_other,
                 (socklen_t*)&slen))
            == -1) {
            diep("recvfrom");
        }
        if (p->type == FIN) {
            sendAck(0, FINACK);
            cout << "connection close" << endl;
            break;
        } else if (p->num == expectedNum) {
            cout << "receive expected packet: " << p->num << endl;
            int i = p->num;
            buffer[p->num] = p;
            while (buffer.count(i) > 0) {
                writePacketToFile(i, write);
                buffer.erase(i);   
                i++;
            }
            sendAck(i, ACK);
            expectedNum = i;
        } else {
            //to cooperate with sender, what to be sent is expectedNum
            cout << "receive unexpected packet: " << p->num << endl;
            sendAck(expectedNum, ACK);
            if (p->num > expectedNum) {
                buffer[p->num]=p;
            }
        }
    }
    close(s);
    write.close();
    printf("%s received.\n", destinationFile);
    return;
}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int) atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}