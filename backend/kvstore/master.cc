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

map<int, ServerInfo> primaryServers; // replica group numbers mapped to the primary server info for that group
unordered_map<int, int> nextServerRoundRobin; // which server to send to frontend next for each repica group
unordered_map<string, chrono::steady_clock::time_point> last_heartbeat; // most recent time of heartbeat for each repID:tcpPort 
mutex heartbeat_mutex;

// Function to handle incoming TCP connections from frontend servers
void handleIncomingRequests() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        cerr << "Error opening socket: " << strerror(errno) << endl;
        return;
    }

    // ensure socket closes, and new socket can be opened
    int option = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &option, sizeof(option)) < 0){
        fprintf(stderr, "Failed to setsockopt: %s\n", strerror(errno));
        exit(1);
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(myInfo.tcpPort);

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        cerr << "Bind failed: " << strerror(errno) << endl;
        close(sockfd);
        return;
    }

    listen(sockfd, 5);

    while (true) {  // main accept() loop
        int conFD = accept(sockfd, NULL, NULL);
        if (conFD < 0) {
            cerr << "Error on accept: " << strerror(errno) << endl;
            continue;
        }

        char buffer[BUFFER_SIZE];
        string command;
        int bytesRead;

        bytesRead = read(conFD, buffer, sizeof(buffer));
        if (bytesRead < 0) {
            cerr << "Failed to read: " << strerror(errno) << endl;
        } else if (bytesRead == 0) {
            // connection closed by client
            printDebug("Connection closed by client");
        } else {
            for (int i = 0; i < bytesRead; ++i) {
                char ch = buffer[i];
                    
                // command is complete
                if (ch == '\r' && i + 1 < bytesRead && buffer[i + 1] == '\n') {
                    printDebug("[handleIncomingRequests] Command received from frontend: " + command);

                    if (command.substr(0,11) == "GET_SERVER:") {
                        string username = command.substr(11);
                        
                        int replicaGroup;

                        // assign the replica group based on first character of the username
                        char firstChar = std::tolower(username[0]);

                        if (firstChar >= 'a' && firstChar <= 'i') {
                            replicaGroup = 1;
                        } else if (firstChar >= 'j' && firstChar <= 'r') {
                            replicaGroup = 2;
                        }  else if ((firstChar >= 's' && firstChar <= 'z') || (firstChar >= '0' && firstChar <= '9')) {
                            replicaGroup = 3;
                        } 
                      
                        printDebug("[handleIncomingRequests] Replica group assigned: " + replicaGroup);

                        // initialize a flag to indicate if a live server was found
                        bool liveServerFound = false;
                        string response;

                        // get the starting index for round-robin to ensure all servers are checked

                        int startServerIndex = nextServerRoundRobin[replicaGroup];
                        int currentServerIndex = startServerIndex;
                        int numServers = servers[replicaGroup].size();

                        // loop through all the servers in the replica group, until a live server is founds
                        do {
                            ServerInfo selectedServer = servers[replicaGroup][currentServerIndex];

                            // check if the current server is alive
                            if (!selectedServer.isDead) {
                                response = "SERVER_INFO:" + selectedServer.ip + ":" + to_string(selectedServer.tcpPort) + "\r\n";
                                
                                // increment index to the next server for future requests
                                nextServerRoundRobin[replicaGroup] = (currentServerIndex + 1) % numServers;
                                liveServerFound = true;
                                break; 
                            }

                            // move to the next server in the round-robin
                            currentServerIndex = (currentServerIndex + 1) % numServers;
                        } while (currentServerIndex != startServerIndex);

                        // if no live servers were found

                        if (!liveServerFound) {
                            response = "-ERR NO_SERVERS_ALIVE\r\n";
                        }

                        printDebug("[handleIncomingRequests] Replied to frontend: " + response);

                        // send the response to the frontend
                        write(conFD, response.c_str(), response.length());
                    }
                    command.clear();
                    
                    // skip \n
                    i++;
                } else {
                    command += ch;
                }
            }
        }

        // close the connection to the frontend server
        close(conFD);  
    }

    close(sockfd);
}

// function to send the primary/secondary status to the replica servers
// also share information about a server that has died to those in its replica group
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
    
    printDebug("[sendIsPrimary] Status message: " + message + " | Dead Server: " + to_string(deadInfo.isDead));

    int send_status = sendto(sockfd, message.c_str(), message.length(), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (send_status < 0) {
        cerr << "Error sending server status" << endl;
    }

    close(sockfd);
}

// function to send information about new secondary server to the primary server
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

    printDebug("[sendNewSecondaryInfoToPrimary] Status message: " + message + " | Dead Server: ");

    int send_status = sendto(sockfd, message.c_str(), message.length(), 0,
                             (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (send_status < 0) {
        cerr << "Error sending server status" << endl;
    }

    close(sockfd);
}

void checkHeartbeats() {
    while (true) {
        this_thread::sleep_for(chrono::milliseconds(HEARTBEAT_INTERVAL)); // Check every second
        auto now = chrono::steady_clock::now();

        lock_guard<mutex> lock(heartbeat_mutex);
        for (auto it = last_heartbeat.begin(); it != last_heartbeat.end(); ) {
            // server is dead
            if (chrono::duration_cast<chrono::milliseconds>(now - it->second).count() > DEAD_THRESHOLD) {
                string key = it->first;
                
                // repID;ip:port
                size_t posSemi = key.find(";");
                size_t posColon = key.find(":");

                int deadRepID = stoi(key.substr(0, posSemi));
                string deadIP = key.substr(posSemi + 1, posColon - posSemi - 1);
                int deadTCP = stoi(key.substr(posColon+1));

                auto& serverList = servers[deadRepID]; // Reference to the vector of servers in the specified group

                ServerInfo deadServerInfo;
                
                printDebug("[checkHeartbeats] Server: " + deadIP + ":" + to_string(deadTCP) + " is dead. Primary: " + to_string(deadServerInfo.isPrimary));

                // iterate through all servers of the group, and remove the dead server    
                for (auto it = serverList.begin(); it != serverList.end(); ++it) {
                    if (it->tcpPort == deadTCP && it->ip == deadIP){
                        it->isDead = true;
                        deadServerInfo = *it;

                        if(it->isPrimary){
                            primaryServers.erase(it->replicaGroup);
                        }

                        it->isPrimary = false;
                        
                        printDebug("[checkHeartbeats] Server: " + deadIP + ":" + to_string(deadTCP) + " marked as dead.");
                    } 
                }

                if (deadServerInfo.isPrimary){ // if the server that died was a primary server
                    // assign a new primary
                    bool primaryAssigned = false;
                    // iterate through all the severs in the group               
                    for (auto it = serverList.begin(); it != serverList.end(); ++it) {
                        // if primary assigned already, just inform this server about it
                        if (primaryAssigned){
                            // if a primary has been assigned, inform the secondary server about it
                            printDebug("[checkHeartbeats] Inform " + it->ip + ":" + to_string(it->tcpPort) + " that primary is dead.");
                            sendIsPrimary(false, *it, primaryServers[it->replicaGroup], deadServerInfo);
                        } else if (!it->isDead) {
                            // if no primary server has been assigned so far, assign the current one to it
                            printDebug("[checkHeartbeats] Server: " + it->ip + ":" + to_string(it->tcpPort) + " assigned as Primary Server in group " + to_string(it->replicaGroup));

                            it->isPrimary = true;
                            primaryServers[deadServerInfo.replicaGroup] = *it;
                            primaryAssigned = true;

                            sendIsPrimary(true, *it, primaryServers[it->replicaGroup], deadServerInfo);
                        }
                    }
                } else { // if secondary server dies
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

// function to receive heartbeats from servers to ensure they are alive
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

    // start the thread to check for heartbeats
    thread heartbeat_checker(checkHeartbeats);
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

            ServerInfo tempInfo;

            for (auto& info : servers[replicaGroup]) {
                if (info.tcpPort == senderTCP) {
                    tempInfo = info;
                    
                    // if server already alive, don't go through the rest of the code
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

                    // inform the server of its new status
                    sendIsPrimary(info.isPrimary, info, primaryServers[info.replicaGroup], deadInfo);

                    if(!info.isPrimary){
                        // if secondary server, let the primary know its a new server alive
                        sendNewSecondaryInfoToPrimary(primaryServers[info.replicaGroup], info);
                    }
                }
            }
        
            lock_guard<mutex> lock(heartbeat_mutex);
            string key = to_string(tempInfo.replicaGroup) + ";" + tempInfo.ip + ":" + to_string(tempInfo.tcpPort);
            last_heartbeat[key] = chrono::steady_clock::now();

            // time_t currentTime = time(nullptr); 
            // cout << message << " at " << ctime(&currentTime);
        }
    }
    close(sockfd);
}

int main(int argc, char *argv[]){

    parseArguments(argc, argv);

    parseServers("config.txt", servers);
        
    // Initialize round-robin index to 0 for each replica group
    for (const auto& pair : servers) {
        nextServerRoundRobin[pair.first] = 0;  
    }

    // find the master servers info
    for (const auto& server : servers) {
        for (const auto& info : server.second) {
            if (server.first == 0) {
                myInfo = info;
            }
        }
    }

    // listen for frontend requests
    thread requestHandlerHelper(handleIncomingRequests);
    requestHandlerHelper.detach();

    // listen for heartbeats
    recvHeartbeat();

    return 0;
}