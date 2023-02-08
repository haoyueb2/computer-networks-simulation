#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

string topofile, messagefile, changesfile;
unordered_map<int, unordered_map<int, int>> network;
unordered_map<int, unordered_map<int, pair<int, int>>> forwardingTables;
set<int> nodes;
typedef struct {
    int src;
    int dst;
    string message;
} MessageStruct;
vector<MessageStruct> messageStructs;
typedef struct {
    int src;
    int dst;
    int cost;
} Change;
vector<Change> changes;
ofstream output("output.txt");
void iniTopology()
{
    ifstream myfile;
    myfile.open(topofile);
    string line;
    if (myfile.is_open()) {
        while (getline(myfile, line)) {
            stringstream s(line);
            int node1, node2, cost;
            s >> node1 >> node2 >> cost;
            nodes.insert(node1);
            nodes.insert(node2);
            network[node1][node2] = cost;
            network[node2][node1] = cost;
            network[node1][node1] = 0;
            network[node2][node2] = 0;
        }
        myfile.close();
    }
}
void iniForwardingTables()
{
    for (auto it = nodes.begin(); it != nodes.end(); it++) {
        for (auto it2 = nodes.begin(); it2 != nodes.end(); it2++) {
            int src = *it, dst = *it2;
            if (network[src].find(dst) != network[src].end()) {
                forwardingTables[src][dst] = make_pair(dst, network[src][dst]);
            } else {
                forwardingTables[src][dst] = make_pair(-1, -1);
            }
        }
    }
}
bool updateForwardingTables()
{
    bool isChange = false;
    for (auto it = nodes.begin(); it != nodes.end(); it++) {
        for (auto it2 = nodes.begin(); it2 != nodes.end(); it2++) {
            int src = *it, dst = *it2;
            int nowDist = forwardingTables[src][dst].second;
            int minHop = dst, minDist = nowDist;
            for (auto neighbor = network[src].begin(); neighbor != network[src].end(); neighbor++) {
                int neighborNode = neighbor->first;
                int toNeighborCost = neighbor->second;
                if (neighborNode == src) {
                    continue;
                }
                if (forwardingTables[neighborNode][dst].second == -1) {
                    continue;
                }
                int tempDist = toNeighborCost + forwardingTables[neighborNode][dst].second;
                if (minDist == -1 || tempDist < minDist) {
                    minDist = tempDist;
                    minHop = neighborNode;
                    forwardingTables[src][dst] = make_pair(minHop, minDist);
                    isChange = true;
                }
            }
        }
    }
    return isChange;
}
void outputForwardingTables()
{
    for (auto it = nodes.begin(); it != nodes.end(); it++) {
        for (auto it2 = nodes.begin(); it2 != nodes.end(); it2++) {
            int src = *it, dst = *it2;
            if(forwardingTables[src][dst].second == -1) {
                continue;
            }
            output << 
            // src << " " << 
            dst << " " << forwardingTables[src][dst].first << " " << forwardingTables[src][dst].second << endl;
        }
    }
}
void convergeForwardingTables()
{
    bool isContinue = true;
    while (isContinue) {
        // outputForwardingTables();
        // output << endl;
        isContinue = updateForwardingTables();
    }
    outputForwardingTables();
}
void sendMessage(int src, int dst, string message) {
    int cost = forwardingTables[src][dst].second;
    if(cost == -1) {
        output << "from " << src << " to "<< dst << " cost infinite hops unreachable message "
        << message << endl;
        return;
    }
    vector<int> hops;
    int hop = src;
    while(hop != dst) {
        hops.push_back(hop);
        hop = forwardingTables[hop][dst].first;        
    }
    string hopsStr = "";
    for(int i = 0; i < hops.size(); i++) {
        hopsStr += to_string(hops[i]) + " ";
    }
    output << "from " << src << " to "<< dst << " cost " << cost
    << " hops " << hopsStr << "message" << message << endl;
}
void sendAllMessages() {
    for(auto m : messageStructs) {
        sendMessage(m.src, m.dst, m.message);
    }
}
void readMessageFile() {
    ifstream myfile;
    myfile.open(messagefile);
    string line;
    if (myfile.is_open()) {
        while (getline(myfile, line)) {
            stringstream s(line);
            int node1, node2;
            string message;
            s >> node1 >> node2;
            getline(s, message);
            MessageStruct m = {node1, node2, message};
            messageStructs.push_back(m);
        }
        myfile.close();
    }
}
void readchangesfile() {
    ifstream myfile;
    myfile.open(changesfile);
    string line;
    if (myfile.is_open()) {
        while (getline(myfile, line)) {
            stringstream s(line);
            int node1, node2, cost;
            string message;
            s >> node1 >> node2 >> cost;
            Change c = {node1, node2, cost};
            changes.push_back(c);
            nodes.insert(node1);
            nodes.insert(node2);
        }
        myfile.close();
    }
}
int main(int argc, char** argv) {
    //printf("Number of arguments: %d", argc);
    if (argc != 4) {
        printf("Usage: ./distvec topofile messagefile changesfile\n");
        return -1;
    }
    topofile = argv[1];
    messagefile = argv[2];
    changesfile = argv[3];
    iniTopology();
    iniForwardingTables();
    convergeForwardingTables();
    readMessageFile();
    sendAllMessages();
    readchangesfile();
    for(auto c : changes) {
        if(c.cost == -999) {
            network[c.src].erase(c.dst);
            network[c.dst].erase(c.src);
        } else {
            network[c.src][c.dst] = c.cost;
            network[c.dst][c.src] = c.cost;
        }
        forwardingTables.clear();
        iniForwardingTables();
        convergeForwardingTables();
        sendAllMessages();
    }
    output.close();
    return 0;
}

