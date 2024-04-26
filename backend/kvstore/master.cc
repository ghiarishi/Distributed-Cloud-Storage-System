#include "helper.h"
#include "constants.h"
#include <iostream>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <ctime>

using namespace std;

map<int, ServerInfo> primaryServers;
unordered_map<string, chrono::steady_clock::time_point> last_heartbeat; // repID:tcpPort mapped to time
mutex heartbeat_mutex;

void sendIsPrimary(bool isPrimary, ServerInfo recvInfo, ServerInfo primaryInfo, ServerInfo deadInfo) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "Error opening socket" << endl;
        return;
    }

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(recvInfo.udpPort2);
    servaddr.sin_addr.s_addr = inet_addr(recvInfo.ip.c_str());

    string message;
    
    if (isPrimary) {
        message = "PRIMARY:";
    } else { 
        // new primary server information
        message = "SECONDARY:" + primaryInfo.ip + ":" + to_string(primaryInfo.tcpPort) + "@";
    }
    
    // information about which server is dead
    if (deadInfo.isDead) {
        message += ";" + deadInfo.ip + ":" + to_string(deadInfo.tcpPort);
    }
    message.push_back('\0');

    int send_status = sendto(sockfd, message.c_str(), message.length(), 0,
                             (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (send_status < 0) {
        cerr << "Error sending server status" << endl;
    }

    close(sockfd);
}

void sendNewSecondaryInfoToPrimary(ServerInfo primaryInfo, ServerInfo secondaryInfo) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "Error opening socket" << endl;
        return;
    }

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(primaryInfo.udpPort2);
    servaddr.sin_addr.s_addr = inet_addr(primaryInfo.ip.c_str());

    string message;
    
    message = "PRIMARY:" + secondaryInfo.ip + ":" + to_string(secondaryInfo.tcpPort) + "@";

    message.push_back('\0');

    int send_status = sendto(sockfd, message.c_str(), message.length(), 0,
                             (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (send_status < 0) {
        cerr << "Error sending server status" << endl;
    }

    close(sockfd);
}

void check_heartbeats() {
    while (true) {
        this_thread::sleep_for(chrono::seconds(1)); // Check every second
        auto now = chrono::steady_clock::now();

        lock_guard<mutex> lock(heartbeat_mutex);
        for (auto it = last_heartbeat.begin(); it != last_heartbeat.end(); ) {
            // server is dead
            if (chrono::duration_cast<chrono::seconds>(now - it->second).count() > DEAD_THRESHOLD) {
                string key = it->first;
                
                // repID;ip:port
                size_t posSemi = key.find(";");
                size_t posColon = key.find(":");

                int deadRepID = stoi(key.substr(0, posSemi));
                string deadIP = key.substr(posSemi + 1, posColon - posSemi - 1);
                int deadTCP = stoi(key.substr(posColon+1));

                auto& serverList = servers[deadRepID]; // Reference to the vector of servers in the specified group

                ServerInfo deadServerInfo;

                cout << "Server " << to_string(deadTCP) << " is dead." << endl;

                // iterate through all servers of the group, and remove the dead server    
                for (auto it = serverList.begin(); it != serverList.end(); ++it) {
                    if (it->tcpPort == deadTCP && it->ip == deadIP){
                        it->isDead = true;
                        deadServerInfo = *it;

                        if(it->isPrimary){
                            primaryServers.erase(it->replicaGroup);
                        }

                        it->isPrimary = false;
                        cout<<"Server marked dead"<<endl;
                    } 
                }

                if (deadServerInfo.isPrimary){ // if the server that died was a primary server
                    // assign a new primary
                    cout<<"Primary Server Dead"<<endl;

                    bool primaryAssigned = false;
                    // iterate through all the severs in the group               
                    for (auto it = serverList.begin(); it != serverList.end(); ++it) {
                        // if primary assigned already, just inform this server about it
                        if (primaryAssigned){
                            // inform of new primary
                            cout << "Informing sec of primary death" << endl;
                            sendIsPrimary(false, *it, primaryServers[it->replicaGroup], deadServerInfo);
                        } else if (!it->isDead) {
                            // if no primary, assign it, and inform it
                            cout << "Server " << it->tcpPort << " is the new primary in group "<< to_string(it->replicaGroup) <<endl;
                            it->isPrimary = true;
                            primaryServers[deadServerInfo.replicaGroup] = *it;
                            primaryAssigned = true;
                            sendIsPrimary(true, *it, primaryServers[it->replicaGroup], deadServerInfo);
                        }
                    }
                } else { // if secondary server dies
                    cout<<"Secondary Server Dead"<<endl;

                    // inform all other servers in this replica group about the death
                    for (auto it = serverList.begin(); it != serverList.end(); ++it) {
                        if (!it->isDead) {
                            sendIsPrimary(it->isPrimary, *it, primaryServers[it->replicaGroup], deadServerInfo);
                        }
                    }
                }

                it = last_heartbeat.erase(it);
            } else {
                ++it;   
            }
        }
    }
}

void recvHeartbeat() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "Error opening socket" << endl;
        return;
    }

    struct sockaddr_in servaddr, cliaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(myInfo.udpPort);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        cerr << "Bind failed" << endl;
        close(sockfd);
        return;
    }

    thread heartbeat_checker(check_heartbeats);
    heartbeat_checker.detach();
    
    while (true) {
        
        char buffer[BUFFER_SIZE];
        socklen_t len;
        int n;

        len = sizeof(cliaddr);
        n = recvfrom(sockfd, (char *)buffer, BUFFER_SIZE, 0, (struct sockaddr *) &cliaddr, &len);

        if (n > 0) {
            buffer[n] = '\0';
            string ipAddress = inet_ntoa(cliaddr.sin_addr);
            string sender = ipAddress + ":" + to_string(ntohs(cliaddr.sin_port));

            string message(buffer);

            size_t first_comma_pos = message.find(',');
            size_t second_comma_pos = message.find(',', first_comma_pos + 1);

            int replicaGroup = stoi(message.substr(0, first_comma_pos));
            int senderTCP = stoi(message.substr(first_comma_pos+1, second_comma_pos-1));

            // vector<ServerInfo> temp = ;
            ServerInfo tempInfo;
            // if no existing primary server, then 
            for (auto& info : servers[replicaGroup]) {
                if (info.tcpPort == senderTCP) {
                    tempInfo = info;
                    
                    // if server already alive, don't bother
                    if (!info.isDead){
                        continue;
                    }

                    // if server newly alive, or revived, then do this

                    info.isDead = false;

                    // assign the first connecting node of rep grp as primary
                    if (primaryServers.find(replicaGroup) == primaryServers.end()){
                        info.isPrimary = true;
                        primaryServers[replicaGroup] = info;
                        cout<<to_string(senderTCP)<<" assigned as PRIMARY in group "<<to_string(replicaGroup)<<endl;
                    } 

                    ServerInfo deadInfo;
                    deadInfo.isDead = false;
                    sendIsPrimary(info.isPrimary, info, primaryServers[info.replicaGroup], deadInfo);

                    if(!info.isPrimary){
                        // send to primary saying I AM SECONDARY NEW
                        sendNewSecondaryInfoToPrimary(primaryServers[info.replicaGroup], info);
                    }
                }
            }
        
            lock_guard<mutex> lock(heartbeat_mutex);
            string key = to_string(tempInfo.replicaGroup) + ";" + tempInfo.ip + ":" + to_string(tempInfo.tcpPort);
            last_heartbeat[key] = chrono::steady_clock::now();

            time_t currentTime = time(nullptr); // for printing purposes
            // cout << message << " at " << ctime(&currentTime);
        }
    }

    close(sockfd);
}

int main(){
    parseServers("config.txt", servers);

    // Displaying the parsed data
    for (const auto& server : servers) {
        for (const auto& info : server.second) {
            if (server.first == 0) {
                myInfo = info;
            }
            // cout << "  IP: " << info.ip << ", TCP Port: " << info.tcpPort << ", UDP Port: " << info.udpPort << endl;
        }
    }

    // listen for heartbeats
    recvHeartbeat();
 
    return 0;
}