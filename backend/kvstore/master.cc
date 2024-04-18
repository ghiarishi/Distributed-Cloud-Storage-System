#include "helper.h"
#include <iostream>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

ServerMap servers;

int main(){
    parseServers("config.txt", servers);

    // Displaying the parsed data
    for (const auto& server : servers) {
        cout << "Server ID: " << server.first << endl;
        for (const auto& info : server.second) {
            if (server.first == 0) {
                masterIP = info.ip;
                masterTCP = info.tcpPort;
                masterUDP = info.udpPort;
            }
            cout << "  IP: " << info.ip << ", TCP Port: " << info.tcpPort << ", UDP Port: " << info.udpPort << endl;
        }
    }

    /// listen for heartbeats
    recvHeartbeat(masterUDP);

    return 0;
}