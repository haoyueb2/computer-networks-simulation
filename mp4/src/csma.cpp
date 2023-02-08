#include<stdio.h>
#include<string.h>
#include<stdlib.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
using namespace std;

int nodesNumber, packetSize, maxTxAttempt, totalTime;
vector<int> randomNumRanges;
typedef struct {
    int backOff;
    int remainingPacketSize;
    int collisionCnt;
} Node;
vector<Node> nodes;
int ticks = 0;
int currentTxNode = -1;
int txSlotNum = 0;
void readFile(string file)
{
    ifstream myfile;
    myfile.open(file);
    string line;
    if (myfile.is_open()) {
        while (getline(myfile, line)) {
            stringstream s(line);
            char var;
            s >> var;
            switch (var) {
            case 'N':
                s >> nodesNumber;
                break;
            case 'L':
                s >> packetSize;
                break;
            case 'M':
                s >> maxTxAttempt;
                break;
            case 'T':
                s >> totalTime;
                break;
            default:
                // for (int i = 0; i < maxTxAttempt; i++) {
                //     int randomNumRange;
                //     s >> randomNumRange;
                //     randomNumRanges.push_back(randomNumRange);
                // }
                int randomNumRange;
                while (s >> randomNumRange) {
                    randomNumRanges.push_back(randomNumRange);
                }
            }
        }
        myfile.close();
    }
    while (randomNumRanges.size() < maxTxAttempt) {
        randomNumRanges.push_back(randomNumRanges.back() * 2);
    }
}

void initNodes()
{
    for (int i = 0; i < nodesNumber; i++) {
        Node n = { i%randomNumRanges[0], packetSize, 0 };
        nodes.push_back(n);
    }
}
int countReadyNodes()
{
    int num = 0;
    for (int i = 0; i < nodesNumber; i++) {
        if (nodes[i].backOff == 0) {
            currentTxNode = i;
            num++;
        }
    }
    return num;
}
void decreaseBackoff()
{
    for (int i = 0; i < nodesNumber; i++) {
        nodes[i].backOff--;
    }
}
void setBackOff(int id)
{
    nodes[id].backOff = (id + ticks + 1) % randomNumRanges[nodes[id].collisionCnt];
}
void sendCurrentTxNode()
{
    nodes[currentTxNode].remainingPacketSize--;
    txSlotNum++;
    if (nodes[currentTxNode].remainingPacketSize == 0) {
        nodes[currentTxNode].collisionCnt = 0;
        setBackOff(currentTxNode);
        nodes[currentTxNode].remainingPacketSize = packetSize;
        currentTxNode = -1;
    }
}
void handleCollision()
{
    for (int i = 0; i < nodesNumber; i++) {
        if (nodes[i].backOff == 0) {
            nodes[i].collisionCnt++;
            if (nodes[i].collisionCnt < maxTxAttempt) {
                setBackOff(i);
            } else {
                nodes[i].collisionCnt = 0;
                // nodes[i].backOff = 0;
                setBackOff(i);
            }
        }
    }
}
void debugBackoff()
{
    for (int i = 0; i < nodesNumber; i++) {
        cout << nodes[i].backOff << ' ';
    }
    cout << endl;
}
void simulate()
{
    while (ticks < totalTime) {
        debugBackoff();
        // channel is busy
        if (currentTxNode != -1) {
            sendCurrentTxNode();

        } else {
            // channel is not busy
            int readyNodes = countReadyNodes();
            if (readyNodes == 0) {
                decreaseBackoff();
            } else if (readyNodes == 1) {
                sendCurrentTxNode();
            } else {
                // collsion happens
                currentTxNode = -1;
                handleCollision();
            }
        }
        ticks++;
    }
}
int main(int argc, char** argv) {
    //printf("Number of arguments: %d", argc);
    if (argc != 2) {
        printf("Usage: ./csma input.txt\n");
        return -1;
    }
    string file = argv[1];
    readFile(file);
    initNodes();
    simulate();
    // ofstream output("output.txt");
    // output << (float)txSlotNum / totalTime;
    // output.close();
    FILE *fpOut;
    fpOut = fopen("output.txt", "w");
    float rate = (float)txSlotNum / totalTime;
    fprintf(fpOut, "%.2f", rate);
    fclose(fpOut);
    return 0;
}
