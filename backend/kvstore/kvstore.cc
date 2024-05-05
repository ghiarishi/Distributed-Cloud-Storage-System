#include "helper.h"
#include "constants.h"
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <errno.h>
#include <stdlib.h> 
#include <pthread.h>
#include <thread>
#include <vector>
#include <signal.h>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <iterator>
#include <sstream>
#include <string>
#include <mutex>
#include <ctime>

using namespace std;

ServerInfo masterInfo;
ServerInfo primaryInfo;

bool enabled = true; // enabled by default, disabled by signal from admin console
bool currentlyInitializing = false;
bool currentlyReadingFromCheckpointFile = false;

int currentNumberOfWritesForReplicaAndServer = 0;
string diskFilePath;

// thread struct
struct thread_data {
    int conFD;
    pthread_t threadID;
    struct sockaddr_in clientaddr;
    socklen_t clientaddrlen;
};

// signal handler for ctrl c
void sigHandler(int signum) {
    for (int conFD : openConnections) { // iterate through all currently open connections and shut them down
        string msg = "-ERR Server shutting down\r\n";
        if (write(conFD, msg.c_str(), msg.length()) < 0){
            fprintf(stderr, "Failed to write: %s\n", strerror(errno));
            exit(1);
        }

        printDebug(msg);

        if (close(conFD) < 0){
            fprintf(stderr, "Failed to close connection: %s\n", strerror(errno));
            exit(1);
        }
    }
    cout<<""<<endl;
    exit(1); 
}

// function to split the command received delimited by commas
vector<string> splitKVStoreCommand(const string& command_str) {
    vector<string> parameters;
    string temp = command_str.substr(0, command_str.find(' ') + 1);
    transform(temp.begin(), temp.end(), temp.begin(), ::toupper); 
    parameters.push_back(temp);
    const string& command_parameters = command_str.substr(command_str.find(' ') + 1);
    stringstream ss(command_parameters); // create a stringstream from the command
    string parameter;
    while (getline(ss, parameter, ',')) { // read parameters separated by ','
        parameters.push_back(parameter);
    }
    return parameters;
}

// function to split a string based a on delimiter
vector<string> split(const string& str, char delimiter) {
    vector<string> tokens;
    stringstream ss(str);

    string token;
    while (getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }

    return tokens;
}

// append the write command with parameters at the end of the checkpoint log file (along with parameters - PUT, CPUT and DELETE)
void handleAppend(string command) {
    if (command.size() > 0 && !currentlyReadingFromCheckpointFile) {
        appendToFile(diskFilePath + "-checkpoint", command);
        currentNumberOfWritesForReplicaAndServer++;
    }
}

long long sendStringOverSocket(int sock, const string& command) {
    size_t bufferSize = BUFFER_SIZE;
    size_t commandSize = command.size();
    long long count = 0;

    // Loop through the string, sending bufferSize characters at a time
    for (size_t i = 0; i < commandSize; i += bufferSize) {
        // Get a substring of bufferSize characters from the command string
        string buffer = command.substr(i, bufferSize);

        // Send the buffer over the socket
        if (write(sock, buffer.c_str(), buffer.size()) != static_cast<ssize_t>(buffer.size())) {
            cerr << "Send failed\n";
            break;
        }

        count += buffer.size();
    }

    return count;
}

string readFromSocket(int sock, int expectNumberOfBytesToRead) {
    const int bufferSize = BUFFER_SIZE;
    char buffer[BUFFER_SIZE];
    string response;

    ssize_t bytesRead = 0;

    printDebug("Reading from socket");

    // read data from the socket until no more data is available
    while (bytesRead < expectNumberOfBytesToRead) {
        bytesRead += read(sock, buffer, bufferSize);
        // append the read data to the response string
        printDebug("Bytes read from socket : " + to_string(bytesRead));
        response.append(buffer, bytesRead);
    }

    printDebug("Response from socket: " + response);
    
    return response;
}

string readAndWriteFromSocket(int sock, const string &command) {
    int expectNumberOfBytesToRead = EXPECTED_BYTES_TO_READ_WHEN_CONNECTING_TO_SERVER;
    string initialRead = readFromSocket(sock, expectNumberOfBytesToRead);
    
    printDebug("Read when making connection to server: " + initialRead);

    long long count  = sendStringOverSocket(sock, command);
    
    printDebug("Sent " + to_string(count) + " bytes from primary to secondary.");

    // Expect to read only +OK from the server
    expectNumberOfBytesToRead = 5;
    string response = readFromSocket(sock,expectNumberOfBytesToRead);

    return response;
}

// Function to forward request to all secondary servers (only alive servers)
bool forwardToAllSecondaryServers(string command) {
    printDebug("Forwarding a command of size: " + to_string(command.size()) + " :to all secondary servers");
    string temp = command.size() > 1000 ? command.substr(0,1000) : command;
    printDebug("Forwarding a command 1000 : " + temp + " : to all secondary servers");
    auto& serverList = servers[myInfo.replicaGroup]; 
    
    for (auto it = serverList.begin(); it != serverList.end(); ++it) {
        printDebug("For port: " + to_string(it->tcpPort) + " is it Primary ? : " + to_string(it->isPrimary) + " is it dead ? : " + to_string(it->isDead));
        
        if (it->isPrimary == false && it->isDead == false) {
            printDebug("Forwarding to " + it->ip + ":" + to_string(it->tcpPort));
            
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock == -1) {
                cerr << "Socket creation failed\n";
                return 1;
            }

            // Server address
            struct sockaddr_in server;
            server.sin_family = AF_INET;
            server.sin_port = htons(it->tcpPort);
            server.sin_addr.s_addr = inet_addr(it->ip.c_str());

            // Connect to the server
            if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
                cerr << "Connection failed\n";
                return false;
            }
            
            string response = readAndWriteFromSocket(sock, command);
            // Check if the response contains "OK"
            if (response.find("+OK") != string::npos) {
                continue;
            } else {
                return false;
            }
        } else if (it->isPrimary == false && it->isDead == true) {
            /*
                TODO : 
              - FileName = "./storage/RP" + to_string(replicaGroup) + "-" + to_string(it->tcpPort) + "-deadServerLog"
                In the variable command find the last instance of ",PRIMARY" and extract everything before it and call it original command
                Call appendToFile with the fileName and the originalCommand as parameters
            */

            // Construct the file name
            string fileName = "./storage/RP" + to_string(replicaGroup) + "-" + to_string(it->tcpPort) + "-deadServerLog";

            // Find the last instance of ",PRIMARY" in the command string
            size_t pos = command.rfind(",PRIMARY");
            if (pos != string::npos) {
                string originalCommand = command.substr(0, pos);

                // append the original command to the dead server log file
                appendToFile(fileName, originalCommand);
            }
        }
    }
    return true;
}

// function to forward initial request made to secondary to primary server
bool forwardToPrimary(string command) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        cerr << "Socket creation failed\n";
        return 1;
    }

    // Server address
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(primaryInfo.tcpPort);
    server.sin_addr.s_addr = inet_addr(primaryInfo.ip.c_str());

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        cerr << "Connection failed\n";
        return 1;
    }

    string response = readAndWriteFromSocket(sock,command);

    // Check if the response contains "OK"
    if (response.find("+OK") != string::npos) {
        return true;
    }
    return false;
}

// Function to check if this server is primary server
bool currentIsPrimary() {
    return myInfo.isPrimary;
}

void handleCommand(vector<string> parameters, string &msg, string command = "", bool receivedFromPrimary = false, bool receivedFromSecondary = false, bool receivedFromFrontend = true) {
    
    printDebug("Received from primary, secondary, frontend: " + to_string(receivedFromPrimary) + ", " + to_string(receivedFromSecondary) + ", " + to_string(receivedFromFrontend));

    if (parameters[0] == "PUT ") {
        if (parameters.size() >= 4 ) {
            if(currentlyInitializing) {
                string row = parameters[1];
                string col = parameters[2];
                string value = parameters[3]; 

                table[row][col] = value;
                msg = "+OK\r\n";
                string appendCommand = parameters[0] + parameters[1] + "," + parameters[2] + "," + parameters[3];
                handleAppend(appendCommand);
                return;
            }
            /*
                TODO : 
                - Add check if I am primary then I write to myself and send request to all alive secondary, 
                    Can do write to secondary servers sequentially and wait for an OK from all of them. 
                - If I am secondary - I send request to primary, and primary sends back to all secondary 
            */ 
            // First if block is for communication when primary forward to secondary
            if(currentIsPrimary()) {
                // Write to in-memory map , set msg as OK and do handleAppend(command) - setting msg as OK will send OK back to the sender
                string tempCommand = parameters[0] + parameters[1] + "," + parameters[2] + "," + parameters[3] + ",PRIMARY\r\n";
                if(forwardToAllSecondaryServers(tempCommand)) {
                    printDebug("Succeeded forwarding to all secondary servers and received +OK.");
                    string row = parameters[1];
                    string col = parameters[2];
                    string value = parameters[3];  // Here, value is directly used as a string
                    table[row][col] = value;
                    msg = "+OK\r\n";
                    string appendCommand = parameters[0] + parameters[1] + "," + parameters[2] + "," + parameters[3];
                    handleAppend(appendCommand);
                }
                else {
                    msg = "-ERR Writing PUT values to secondary\r\n";
                }        
            } 
            // This else if block is for when current is primary and received from secondary
            else if (!currentIsPrimary() && receivedFromFrontend) {
                string tempCommand = parameters[0] + parameters[1] + "," + parameters[2] + "," + parameters[3] + ",SECONDARY\r\n";
                // First forward to all secondaries, wait for OK response from all of them, then write to in-memory and call handleAppend and assign to msg
                if(forwardToPrimary(tempCommand)) {
                    msg = "+OK\r\n";
                } else {
                    printDebug("Primary returned False while performing this PUT");
                    msg = "-ERR Returned by Server while writing PUT Values\r\n";
                }
            } else if(!currentIsPrimary() && receivedFromPrimary) {
                string row = parameters[1];
                string col = parameters[2];
                string value = parameters[3];  // Here, value is directly used as a string
                table[row][col] = value;
                msg = "+OK\r\n";
                string appendCommand = parameters[0] + parameters[1] + "," + parameters[2] + "," + parameters[3];
                handleAppend(appendCommand);
            }
        } else {
            msg = "-ERR Invalid PUT parameters\r\n";
        }
    } else if (parameters[0] == "GET ") {
        if (parameters.size() == 3) {
            string row = parameters[1];
            string col = parameters[2];
            if (table.find(row) != table.end() && table[row].find(col) != table[row].end()) {
                msg = "+OK " + table[row][col] + "\r\n";
            } else {
                msg = "-ERR Not Found\r\n";
            }
        } else {
            msg = "-ERR Invalid GET parameters\r\n";
        }
    } else if (parameters[0] == "CPUT ") {
        if (parameters.size() >= 5) {
            if(currentlyInitializing) {
                string row = parameters[1];
                string col = parameters[2];
                string currentValue = parameters[3];
                string newValue = parameters[4];
                if (table[row][col] == currentValue) {
                    table[row][col] = newValue;
                    msg = "+OK\r\n";
                    string appendCommand = parameters[0] + parameters[1] + "," + parameters[2] + "," + parameters[3] + "," + parameters[4];
                    handleAppend(appendCommand);
                } else {
                    msg = "-ERR Conditional Write Failed\r\n";
                }
                return ;
            }
            /*
                TODO : 
                - Add check if I am primary then I write to myself and send request to all alive secondary, 
                    Can do write to secondary servers sequentially and wait for an OK from all of them. 
                - If I am secondary - I send request to primary, and primary sends back to all secondary 
            */ 
            // First if block is for communication when primary forward to secondary
            if(currentIsPrimary()) {
                // Write to in-memory map , set msg as OK and do handleAppend(command) - setting msg as OK will send OK back to the sender
                string tempCommand = parameters[0] + parameters[1] + "," + parameters[2] + "," + parameters[3] + ","+ parameters[4] + ",PRIMARY\r\n";
                if(forwardToAllSecondaryServers(tempCommand)) {
                    printDebug("Succeeded forwarding to all secondary servers and received +OK");

                    string row = parameters[1];
                    string col = parameters[2];
                    string currentValue = parameters[3];
                    string newValue = parameters[4];
                    if (table[row][col] == currentValue) {
                        table[row][col] = newValue;
                        msg = "+OK\r\n";
                        string appendCommand = parameters[0] + parameters[1] + "," + parameters[2] + "," + parameters[3] + "," + parameters[4];
                        handleAppend(appendCommand);
                    } else {
                        msg = "-ERR Conditional Write Failed\r\n";
                    }
                } else {
                    msg = "-ERR Writing CPUT values to secondary\r\n";
                }
            }  
            // This else if block is for when current is primary and received from secondary
            else if (!currentIsPrimary() && receivedFromFrontend) {
                string tempCommand = parameters[0] + parameters[1] + "," + parameters[2] + "," + parameters[3] + "," + parameters[4] + ",SECONDARY\r\n";
                // First forward to all secondaries, wait for OK response from all of them, then write to in-memory and call handleAppend and assign to msg
                if(forwardToPrimary(tempCommand)) {
                    msg = "+OK\r\n";
                } else {
                    printDebug("Error writing CPUT Values when forwarded to Primary");
                    msg = "-ERR Error Editing the values\r\n";
                }
            } else if(!currentIsPrimary() && receivedFromPrimary) {
                string row = parameters[1];
                string col = parameters[2];
                string currentValue = parameters[3];
                string newValue = parameters[4];
                if (table[row][col] == currentValue) {
                    table[row][col] = newValue;
                    msg = "+OK\r\n";
                    string appendCommand = parameters[0] + parameters[1] + "," + parameters[2] + "," + parameters[3]+","+parameters[4];
                    handleAppend(appendCommand);
                } else {
                    msg = "-ERR Conditional Write Failed\r\n";
                }
            }
        } else {
            msg = "-ERR Invalid CPUT parameters\r\n";
        }
    } else if (parameters[0] == "DELETE ") {
        if (parameters.size() >= 3) {
            if(currentlyInitializing) {
                string row = parameters[1];
                string col = parameters[2];
                table[row].erase(col);
                msg = "+OK\r\n";
                string appendCommand = parameters[0] + parameters[1] + "," + parameters[2];
                handleAppend(appendCommand);
                return;
            }
            if(currentIsPrimary()) {
                // Write to in-memory map , set msg as OK and do handleAppend(command) - setting msg as OK will send OK back to the sender
                string tempCommand = parameters[0] + parameters[1] + "," + parameters[2] + ",PRIMARY\r\n";
                if(forwardToAllSecondaryServers(tempCommand)) {
                    printDebug("Succeeded forwarding to all secondary servers and received +OK");

                    string row = parameters[1];
                    string col = parameters[2];
                    table[row].erase(col);
                    msg = "+OK\r\n";
                    string appendCommand = parameters[0] + parameters[1] + "," + parameters[2];
                    handleAppend(appendCommand);
                }
                else {
                    msg = "-ERR DELETING values from all secondary\r\n";
                }        
            } 
            // This else if block is for when current is primary and received from secondary
            else if (!currentIsPrimary() && receivedFromFrontend) {
                string tempCommand = parameters[0] + parameters[1] + "," + parameters[2] + ",SECONDARY\r\n";
                // First forward to all secondaries, wait for OK response from all of them, then write to in-memory and call handleAppend and assign to msg
                if(forwardToPrimary(tempCommand)) {
                    msg = "+OK\r\n";
                } else {
                    printDebug("Error Deleting Values when command was forwarded to Primary");
                    msg = "-ERR Error Deleting Values\r\n";
                }
            } else if(!currentIsPrimary() && receivedFromPrimary) {
                string row = parameters[1];
                string col = parameters[2];
                table[row].erase(col);
                msg = "+OK\r\n";
                string appendCommand = parameters[0] + parameters[1] + "," + parameters[2];
                handleAppend(appendCommand);
            }
        } else {
            msg = "-ERR Invalid DELETE parameters\r\n";
        }
    } else if (parameters[0] == "LIST ") {
        string  keys;
        for (const auto& entry : table) {
            if (entry.first == parameters[1]) {
                const auto& inner_map = entry.second;
                for (const auto& inner_entry : inner_map) {
                    if (inner_entry.first.find(parameters[2]) == 0) {
                        keys+= inner_entry.first + "\n";
                    }
                }
            }
        }
        msg = keys.size() > 0 ? keys + "\r\n" : "-ERR Nothing Found\r\n";
    } else {
        msg = "-ERR Unknown command\r\n";
    }
}

bool crashRecoveryFunction(string path) {
    // Create the file if it doesn't exist and open it for appending
    fstream file(path + "-deadServerLog", ios::out | ios::app);
    if (!file.is_open()) {
        cerr << "Error opening data file" << endl;
        return false;
    }
    // If the file exists, close it and reopen it for reading
    file.close();

    ifstream crashFilePath(path + "-deadServerLog");  //  Adjust filename if needed
    if (crashFilePath.is_open()) {
        string line;
        string msg;
        while (getline(crashFilePath, line)) {
            vector<string> parameters = splitKVStoreCommand(line);
            handleCommand(parameters, msg);
        }
        crashFilePath.close();
        // Truncate the file after reading
        ofstream truncateFile(path + "-deadServerLog", ofstream::trunc);
        truncateFile.close();
        cout << "Crash recovery (true) from " << path << "-deadServerLog" << endl;
        return true;
    }
    cout << "Crash recovery (true) from " << path << "-deadServerLog" << endl;
    return false;
}

// function to initialize in-memory map - which reads diskfile and replays the checkpointing file
// contact primary server and see if it is out of sync with them and update accordingly
void initialize(string path) {

    // create the file if it doesn't exist, open it for appending
    fstream file(path, ios::out | ios::app);

    if (!file.is_open()) {
        cerr << "Error opening data file" << endl;
        return;
    }

    // If the file exists, close it and reopen it for reading
    file.close();
    
    ifstream dataFile(path);
    if (dataFile.is_open()) {
        string line;
        string msg;
        while (getline(dataFile, line)) {
            vector<string> tokens = split(line, ',');
            if (tokens.size() == 3) {
                table[tokens[0]][tokens[1]] = tokens[2];
            } else {
                // TODO : <How to handle invalid file writes>
            }
        }

        printDebug("Disk file -> " + path + " loaded in memory.");
        
        dataFile.close();
    } else {
        cerr << "Error opening data file" << endl;
    }
    // Replay Checkpoint

    // Create the file if it doesn't exist and open it for appending
    fstream logFileTemp(path + "-checkpoint", ios::out | ios::app);

    if (!logFileTemp.is_open()) {
        cerr << "Error opening data file" << endl;
        return;
    }

    // If the file exists, close it and reopen it for reading
    logFileTemp.close();

    ifstream logFile(path + "-checkpoint");  //  Adjust filename if needed

    if (logFile.is_open()) {
        string line;
        string msg;
        currentlyReadingFromCheckpointFile = true;
        while (getline(logFile, line)) {
            currentNumberOfWritesForReplicaAndServer++;
            vector<string> parameters = splitKVStoreCommand(line);
            handleCommand(parameters, msg);
        }
        currentlyReadingFromCheckpointFile = false;
        logFile.close();
    } else {
        cerr << "Error opening checkpoint log file" << endl;
    }


    // SIMPLIFY THIS
    if (currentNumberOfWritesForReplicaAndServer >= CHECKPOINTING_THRESHOLD) {
        checkpoint_table(path);
        currentNumberOfWritesForReplicaAndServer = 0;
        printDebug("Checkpointing done! currentNumberOfWrites = " + to_string(currentNumberOfWritesForReplicaAndServer));
    }

    // Should we perform crash recovery ?
    if (crashRecoveryFunction(path)) {
        printDebug("Crash Recovery Done!");
        if (currentNumberOfWritesForReplicaAndServer >= CHECKPOINTING_THRESHOLD) {
            checkpoint_table(path);
            currentNumberOfWritesForReplicaAndServer = 0;
            printDebug("Checkpointing done! currentNumberOfWrites = " + to_string(currentNumberOfWritesForReplicaAndServer));
        }
    }
}

void receiveStatus() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "Error opening socket" << endl;
        return;
    }

    struct sockaddr_in servaddr, cliaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(myInfo.udpPort2);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        cerr << "Bind failed" << endl;
        close(sockfd);
        return;
    }

    
    while(true){
        if (!enabled) { 
            continue;
        }

        char buffer[BUFFER_SIZE];
        socklen_t len;
        int n;

        len = sizeof(cliaddr);
        n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&cliaddr, &len);
        if (n > 0) {
            buffer[n] = '\0';
            string message(buffer);

            printDebug("Status message received: " + message);

            size_t posColon = message.find(":");
            if (message.substr(0, posColon) == "PRIMARY"){ // primary
                printDebug("Set to Primary in replica group " + to_string(replicaGroup));
                
                myInfo.isPrimary = true;
                myInfo.isDead = false;  

                auto& serverList = servers[myInfo.replicaGroup]; 
                for (auto it = serverList.begin(); it != serverList.end(); ++it) {
                    if (it->tcpPort == myInfo.tcpPort && it->ip == myInfo.ip){
                        *it = myInfo;
                    }
                }

                size_t posAt = message.find("@");

                // if secondary server info provided, then mark alive
                if (posAt != string::npos) {
                    string ip_port = message.substr(posColon+1, posAt - posColon - 1);

                    string ipTemp = ip_port.substr(0, ip_port.find(":"));

                    int tcpTemp = stoi(ip_port.substr(ip_port.find(":") + 1));

                    printDebug("Primary marks secondary alive in group " + to_string(replicaGroup));

                    auto& serverList = servers[myInfo.replicaGroup]; 
                    for (auto it = serverList.begin(); it != serverList.end(); ++it) {
                        if (it->tcpPort == tcpTemp && it->ip == ipTemp){
                            it->isDead = false;
                            it->isPrimary = false;
                        }
                    }
                }
            } 
            else{ // secondary
                size_t posAt = message.find("@");
                string ip_port = message.substr(posColon+1, posAt - posColon - 1);

                string ipTemp = ip_port.substr(0, ip_port.find(":"));
                int tcpTemp = stoi(ip_port.substr(ip_port.find(":") + 1));

                printDebug("Set to Secondary in replica group " + to_string(replicaGroup));

                myInfo.isPrimary = false;
                myInfo.isDead = false;  

                auto& serverList = servers[myInfo.replicaGroup]; 
                for (auto it = serverList.begin(); it != serverList.end(); ++it) {
                    if (it->tcpPort == myInfo.tcpPort && it->ip == myInfo.ip){
                        *it = myInfo;
                    }

                    if (it->tcpPort == tcpTemp && it->ip == ipTemp){
                        it->isPrimary = true;
                        primaryInfo = *it;
                    }
                }
            }

            // SERVER IS DEAD
            size_t posSemi = message.find(";");
            if (posSemi != string::npos) {
                string ipAndTCP = message.substr(posSemi + 1);
                string tempIp = ipAndTCP.substr(0, ipAndTCP.find(":"));
                int deadTCP = stoi(ipAndTCP.substr(ipAndTCP.find(":")+1));
                printDebug("Server " + ipAndTCP + " is dead");
              
                auto& serverList = servers[replicaGroup]; // Reference to the vector of servers in the specified group
                for (auto it = serverList.begin(); it != serverList.end(); ++it) {
                    if (it->ip == tempIp && it->tcpPort == deadTCP){
                        it->isDead = true;
                        it->isPrimary = false;
                        printDebug("Server " + ipAndTCP + " marked dead");
                    } 
                }
            }
        
        }

    }
    
    close(sockfd);
}

void adminConsoleSignalHandler() {
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
    servaddr.sin_port = htons(myInfo.tcpPort2);

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
            printDebug("Connection closed by admin");
        } else {
            for (int i = 0; i < bytesRead; ++i) {
                char ch = buffer[i];
                    
                // command is complete
                if (ch == '\r' && i + 1 < bytesRead && buffer[i + 1] == '\n') {
                    printDebug("[adminConsoleSignalHandler] Command received from admin: " + command);
                    vector<string> commands;
                    if(command.find(' ') != string::npos) {
                        commands = splitKVStoreCommand(command);
                    }
                    
                    string temp = commands.size() > 0 ? commands[0] : command;
                    for (char &c : temp) {
                        c = std::toupper(c);
                    }

                    string response;

                    if (temp == "ENABLE" && !enabled) {
                        enabled = true; // set flag to true

                        // perform crash recovery
                        currentlyInitializing = true;
                        initialize(diskFilePath);
                        currentlyInitializing = false;
                        
                        response = "+OK\r\n";

                    } else if (temp == "DISABLE" && enabled) {
                        enabled = false; // set flag to disabled
                        response = "+OK\r\n";
                    } else if (temp == "GETALL ") {
                        int skip = stoi(commands[1]);
                        response = "+OK\n";
                        int count = 0;
                        for (const auto& row : table) {
                            for (const auto& column : row.second) {
                                if (count >= skip && count < skip + 3) {
                                    response += row.first + "," + column.first + "\n";
                                }
                                count++;
                            }
                        }
                    } else if (temp == "GET ") {
                        if (commands.size() == 3) {
                            string row = commands[1];
                            string col = commands[2];
                            if (table.find(row) != table.end() && table[row].find(col) != table[row].end()) {
                                response = "+OK " + table[row][col] + "\r\n";
                            } else {
                                response = "-ERR Not Found\r\n";
                            }
                        } else {
                            response = "-ERR Invalid GET parameters\r\n";
                        }
                    } else {
                        response = "-ERR Invalid Command\r\n";
                    }

                    printDebug("[adminConsoleSignalHandler] Replied to admin console: " + response);

                    // send the response to the admin console
                    write(conFD, response.c_str(), response.length());

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

void sendHeartbeat() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "Error opening socket" << endl;
        return;
    }

    // Setup destination address structure
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(masterInfo.udpPort);
    servaddr.sin_addr.s_addr = inet_addr(masterInfo.ip.c_str());

    string heartbeat_message = to_string(replicaGroup) + "," + to_string(myInfo.tcpPort) + ",heartbeat";
    heartbeat_message.push_back('\0');

    while (true) {
        if (!enabled) {
            continue;
        }
        int send_status = sendto(sockfd, heartbeat_message.c_str(), heartbeat_message.size(), 0, (const struct sockaddr*)&servaddr, sizeof(servaddr));
        if (send_status < 0) {
            cerr << "Error sending heartbeat" << endl;
        }

        // time_t currentTime = time(nullptr); // for printing purposes
        // cout << message << " at " << ctime(&currentTime);
        // cout<<"heartbeat at " << currentTime <<endl;

        this_thread::sleep_for(chrono::milliseconds(HEARTBEAT_INTERVAL)); // Send every 2 seconds, adjusted from your comment
    }

    close(sockfd);
}

void* threadFunc(void* arg) {
    struct thread_data* data = (struct thread_data*)arg;

    int conFD = data->conFD;
    char buffer[BUFFER_SIZE];
    string command;
    string msg;
    int bytesRead;

    while (true) {
        bytesRead = read(conFD, buffer, BUFFER_SIZE);
        if (bytesRead < 0) {
            printDebug("Command at failure: " + command);
            fprintf(stderr, "Failed to read: %s\n", strerror(errno));
            exit(1);
        } else if (bytesRead == 0) {
            // Connection closed by client
            break;
        }

        if (!enabled) {
            msg = "-ERR Server Disabled\r\n";
            if (write(conFD, msg.c_str(), msg.length()) < 0) {
                fprintf(stderr, "Failed to write: %s\n", strerror(errno));
                
            }
            continue;
        }

        for (int i = 0; i < bytesRead; ++i) {
            char ch = buffer[i];
            if (ch == '\r') {
                // Assuming next char is '\n', check boundary
                if (i + 1 < bytesRead && buffer[i + 1] == '\n') {
                    printDebug("Command received: " + to_string(command.size()));
                    string temp = command.size() > 1000 ? command.substr(0,1000) : command;
                    printDebug("First 1000 characters in command : " + temp);

                    vector<string> parameters = splitKVStoreCommand(command);

                    bool receivedFromPrimary = false;
                    bool receivedFromSecondary = false;
                    bool receivedFromFrontend = false;

                    if (parameters.size() >= 4 && (parameters[0] == "PUT " || parameters[0] == "CPUT " || parameters[0] == "DELETE ")) {
                        if (parameters[parameters.size()-1] == "PRIMARY") {
                            receivedFromPrimary = true;
                        } else if (parameters[parameters.size()-1] == "SECONDARY") {
                            receivedFromSecondary = true;
                        } else {
                            receivedFromFrontend = true;
                        }
                    } else {
                        receivedFromFrontend = true;
                    }
                    handleCommand(parameters, msg, command, receivedFromPrimary, receivedFromSecondary, receivedFromFrontend);

                    // Checkpointing
                    if (currentNumberOfWritesForReplicaAndServer == CHECKPOINTING_THRESHOLD) {
                        checkpoint_table(diskFilePath);
                        currentNumberOfWritesForReplicaAndServer = 0;
                    }

                    if (write(conFD, msg.c_str(), msg.length()) < 0) {
                        fprintf(stderr, "Failed to write: %s\n", strerror(errno));
                        exit(1);
                    }

                    printDebug("[threadFunc] Final response message: " + msg);

                    // reset for the next command
                    command.clear();

                    // skip \n as it's already processed
                    i++; 
                } else {
                    // handle cases where \n might come in the next batch
                    // keep \r in command as we're unsure about \n
                    command += '\r'; 
                }
            } else {
                command += ch;
            }
        }
    }

    if (close(conFD) < 0) {
        fprintf(stderr, "Failed to close connection: %s\n", strerror(errno));
        exit(1);
    }

    printDebug("Connection closed.");

    // Remove the socket from the open connections vector
    auto it = find(openConnections.begin(), openConnections.end(), conFD);
    if (it != openConnections.end()) {
        openConnections.erase(it);
    }
    return NULL;
}

int main(int argc, char *argv[]){
    signal(SIGINT, sigHandler);
    parseArguments(argc, argv);

    diskFilePath = "./storage/RP" + to_string(replicaGroup) + "-" + to_string(myInfo.tcpPort);
    currentlyInitializing = true;
    initialize(diskFilePath);
    currentlyInitializing = false;

    if(aFlag) {
        cerr<<"SEAS LOGIN HERE"<<endl;
        return 0;
    }

    parseServers("config.txt", servers);

    // Displaying the parsed data
    for (auto& server : servers) {
        for (auto& info : server.second) {
            if (server.first == 0) {
                masterInfo = info;
            }
            // current server
            if (myInfo.tcpPort == info.tcpPort){ 
                myInfo = info;
            }
        }
    }

    // create a separate thread to send heartbeat to master node
    thread heartbeat_thread(sendHeartbeat);
    heartbeat_thread.detach();
    // receive status message (pri/secondary)
    thread status_thread(receiveStatus);
    status_thread.detach();
    // receive enable/disable signals from frontend
    thread adminConsoleThread(adminConsoleSignalHandler);
    adminConsoleThread.detach();

    // create new socket
    int listenFD = socket(PF_INET, SOCK_STREAM, 0);
    if (listenFD < 0){
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        exit(1);
    }

    // ensure socket closes, and new socket can be opened
    int option = 1;
    if (setsockopt(listenFD, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &option, sizeof(option)) < 0){
        fprintf(stderr, "Failed to setsockopt: %s\n", strerror(errno));
        exit(1);
    }

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    servaddr.sin_port = htons(myInfo.tcpPort);

    // bind socket to port
    if (bind(listenFD, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0){
        fprintf(stderr, "Failed to bind socket to port: %s\n", strerror(errno));
        exit(1);
    }

    // listen for new connections
    if (listen(listenFD, 100) < 0){
        fprintf(stderr, "Failed to listen for connections: %s\n", strerror(errno));
        exit(1);
    }
    
    // indefinitely create new connections for each client 
    while (true) { 
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen = sizeof(clientaddr);

        // allocate memory for struct for each thread
        struct thread_data* data = (struct thread_data*)malloc(sizeof(struct thread_data));
        data->conFD = accept(listenFD, (struct sockaddr*)&clientaddr, &clientaddrlen);
        if (data->conFD < 0) {
            fprintf(stderr, "Failed to accept new connection: %s\n", strerror(errno));
            free(data); 
            continue;
        }

        data->clientaddr = clientaddr;
        data->clientaddrlen = clientaddrlen;

        printDebug("New connection established: " + data->conFD);

        string greeting = "+OK Server ready (Authors: Janavi, Pranshu, Rishi, Zihao) \r\n";
        if (write(data->conFD, greeting.c_str(), greeting.length()) < 0){
            fprintf(stderr, "Failed to write: %s\n", strerror(errno));
            exit(1);
        }
        
        printDebug("Greeting message sent.");
        
        openConnections.push_back(data->conFD); // add the FD to open connections

        // spawn new thread for each connection
        if (pthread_create(&data->threadID, NULL, threadFunc, data) != 0) {
            fprintf(stderr, "Failed to create thread: %s\n", strerror(errno));
            if (data->conFD >= 0) {
                close(data->conFD); // close the socket if thread creation fails
            }
            free(data); // free memory if connection failed
            continue; 
        }
        if (pthread_detach(data->threadID) < 0){ // detach the thread (kills it)
            fprintf(stderr, "Failed to detach thread: %s\n", strerror(errno));
        };    
    }
}