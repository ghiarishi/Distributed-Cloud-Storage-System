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

// signal handler
void sigHandler(int signum) {
    for (int conFD : openConnections) { // iterate through all currently open connections and shut them down
        string msg = "-ERR Server shutting down\r\n";
        if (write(conFD, msg.c_str(), msg.length()) < 0){
            fprintf(stderr, "Failed to write: %s\n", strerror(errno));
            exit(1);
        }
        if(debug){
            fprintf(stderr, "[ %d ] S: %s\n", conFD, msg.c_str());
        }
        if (close(conFD) < 0){
            fprintf(stderr, "Failed to close connection: %s\n", strerror(errno));
            exit(1);
        }
    }
    cout<<""<<endl;
    exit(1); 
}

vector<string> splitKvstoreCommand(const string& command_str) {
    vector<string> parameters;
    string temp = command_str.substr(0, command_str.find(' ') + 1);
    transform(temp.begin(), temp.end(), temp.begin(), ::toupper); 
    parameters.push_back(temp);
    const string& command_parameters = command_str.substr(command_str.find(' ') + 1);
    stringstream ss(command_parameters); // Create a stringstream from the command
    string parameter;
    while (getline(ss, parameter, ',')) { // Read parameters separated by ','
    parameters.push_back(parameter);
    }
    return parameters;
}

// Function to split a string based a on delimiter
vector<string> split(const string& str, char delimiter) {
    vector<string> tokens;
    stringstream ss(str);

    string token;
    while (getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }

    return tokens;
}

// Append the write command with parameters at the end of the checkpoint log file (along with parameters - PUT, CPUT and DELETE)
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

        // Slightly longer sleep to avoid overloading network for large sends
        // usleep(10000);
    }

    return count;
}

string readFromSocket(int sock, int expectNumberOfBytesToRead) {
    const int bufferSize = 1024;
    char buffer[BUFFER_SIZE];
    string response;

    ssize_t bytesRead = 0;
    if (debug)
        printDebug("Reading from socket");
    // Read data from the socket until no more data is available
    while (bytesRead < expectNumberOfBytesToRead) {
        bytesRead += read(sock, buffer, bufferSize);
        // Append the read data to the response string
        if (debug)
            printDebug("Bytes read from socket : " + to_string(bytesRead));
        response.append(buffer, bytesRead);
    }

    if (debug) {
        printDebug("Response from socket : " + response);
    }
    return response;
}

string readAndWriteFromSocket(int sock, const string &command) {
    int expectNumberOfBytesToRead = EXPECTED_BYTES_TO_READ_WHEN_CONNECTING_TO_SERVER;
    string initialRead = readFromSocket(sock, expectNumberOfBytesToRead);
    if (debug) {
        printDebug("Read when making connection to server : " + initialRead);

    }
    long long count  = sendStringOverSocket(sock, command);
    
    if (debug) {
        printDebug("Sent " + to_string(count) + " bytes from primary to secondary");
    }

    // Expect to read only +OK from the server
    expectNumberOfBytesToRead = 5;
    string response = readFromSocket(sock,expectNumberOfBytesToRead);

    return response;

}

// Function to forward request to all secondary servers (only alive servers)
bool forwardToAllSecondaryServers(string command) {

    if (debug) {
        printDebug("Forwarding command of size " + to_string(command.size()) + " to all secondary Servers ");
    }

    auto& serverList = servers[myInfo.replicaGroup]; 
    if (debug) {
        printDebug("ServerList.size for this replicaGroup + " + to_string(serverList.size()));
    }
    
    for (auto it = serverList.begin(); it != serverList.end(); ++it) {
        if (debug) {
            printDebug("For port : " + to_string(it->tcpPort) + " is it Primary ? : " + to_string(it->isPrimary) + " is it dead ? : " + to_string(it->isDead));
        }
        if (it->isPrimary == false && it->isDead == false) {
            if (debug) {
                printDebug("Forwarding to -> " + it->ip + ":" + to_string(it->tcpPort));
            }
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
                // Extract the original command
                string originalCommand = command.substr(0, pos);

                // Append the original command to the dead server log file
                appendToFile(fileName, originalCommand);
            }
        }
    }
    return true;
}

// Function to forward initial request made to secondary to primary server
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
    // cout << "command -> " << command << endl;
    // cout << " receieved from primary -> " << receivedFromPrimary << endl;
    // cout << " receieved from frontend -> " << receivedFromFrontend << endl;
    // cout << " receieved from secondary -> " << receivedFromSecondary << endl;
    
    if (parameters[0] == "PUT ") {
        if (parameters.size() >= 4 ) {
            if(currentlyInitializing) {
                string row = parameters[1];
                string col = parameters[2];
                string value = parameters[3];  // Here, value is directly used as a string
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
                cout << "Received command size -> " << command.size() << " in primary" << endl;
                // Write to in-memory map , set msg as OK and do handleAppend(command) - setting msg as OK will send OK back to the sender
                string tempCommand = parameters[0] + parameters[1] + "," + parameters[2] + "," + parameters[3] + ",PRIMARY\r\n";
                if(forwardToAllSecondaryServers(tempCommand)) {
                    cout << "Succeeded forwarding to all secondary and received +OK" << endl;
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
                }
            } else if(!currentIsPrimary() && receivedFromPrimary) {
                cout << "Received command size " << command.size() << " from primary +OK" << endl;
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
                cout << "Received command size -> " << command.size() << " in primary" << endl;
                // Write to in-memory map , set msg as OK and do handleAppend(command) - setting msg as OK will send OK back to the sender
                string tempCommand = parameters[0] + parameters[1] + "," + parameters[2] + "," + parameters[3] + ","+ parameters[4] + ",PRIMARY\r\n";
                if(forwardToAllSecondaryServers(tempCommand)) {
                    cout << "Succeeded forwarding to all secondary and received +OK" << endl;
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
                }
            } else if(!currentIsPrimary() && receivedFromPrimary) {
                cout << "Received command size " << command.size() << " from primary +OK" << endl;
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
                cout << "Received command size -> " << command.size() << " in primary" << endl;
                // Write to in-memory map , set msg as OK and do handleAppend(command) - setting msg as OK will send OK back to the sender
                string tempCommand = parameters[0] + parameters[1] + "," + parameters[2] + ",PRIMARY\r\n";
                if(forwardToAllSecondaryServers(tempCommand)) {
                    cout << "Succeeded forwarding to all secondary and received +OK" << endl;
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
                }
            } else if(!currentIsPrimary() && receivedFromPrimary) {
                cout << "Received command size " << command.size() << " from primary +OK" << endl;
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
        msg = keys;
    } else {
        msg = "-ERR Unknown command\r\n";
    }
    // cout << "Mesage sending from handleCommand -> " << msg << endl;
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
            vector<string> parameters = splitKvstoreCommand(line);
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
// Function to initialize in-memory map - which reads diskfile and replays the checkpointing file
// TODO : Contact primary server and see if it is out of sync with them and update accordingly
void initialize(string path) {

    // Create the file if it doesn't exist and open it for appending
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
        if (debug) {
            printDebug("Disk file -> " + path + " loaded in-memory ");
        }
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
            vector<string> parameters = splitKvstoreCommand(line);
            handleCommand(parameters, msg);
        }
        currentlyReadingFromCheckpointFile = false;
        logFile.close();
    } else {
        cerr << "Error opening checkpoint log file" << endl;
    }

    if (currentNumberOfWritesForReplicaAndServer >= CHECKPOINTING_THRESHOLD) {
        checkpoint_table(path);
        currentNumberOfWritesForReplicaAndServer = 0;
        if (debug) {
            printDebug("Checkpointing done and now currentNumberOfWrites is -> " + to_string(currentNumberOfWritesForReplicaAndServer));
        }
    }

    // Should we perform crash recovery ?
    if (crashRecoveryFunction(path)) {
        if(debug) {
            printDebug("Crash Recovery Done");
        }
        if (currentNumberOfWritesForReplicaAndServer >= CHECKPOINTING_THRESHOLD) {
            checkpoint_table(path);
            currentNumberOfWritesForReplicaAndServer = 0;
            if (debug) {
                printDebug("Check done and now currentNumberOfWrites is -> " + to_string(currentNumberOfWritesForReplicaAndServer));
            }
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

        char buffer[BUFFER_SIZE];
        socklen_t len;
        int n;

        len = sizeof(cliaddr);
        n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&cliaddr, &len);
        if (n > 0) {
            buffer[n] = '\0';
            string message(buffer);
            cout << "Status message received: " << buffer << endl;

            size_t posColon = message.find(":");
            if (message.substr(0, posColon) == "PRIMARY"){ // primary
                cout<<"Set to Primary in replica group " << to_string(replicaGroup)<<endl;
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

                    cout<<"Primary marks secondary alive in group " << to_string(replicaGroup)<<endl;

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

                cout<<"Set to Secondary in replica group " << to_string(replicaGroup)<<endl;

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
                cout << "Dead TCP -> " << deadTCP << endl;
                auto& serverList = servers[replicaGroup]; // Reference to the vector of servers in the specified group
                for (auto it = serverList.begin(); it != serverList.end(); ++it) {
                    if (it->ip == tempIp && it->tcpPort == deadTCP){
                        it->isDead = true;
                        it->isPrimary = false;
                        cout << "Server marked as dead" << endl;
                    } 
                }
            }
        
        }

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
        int send_status = sendto(sockfd, heartbeat_message.c_str(), heartbeat_message.size(),
                                 0, (const struct sockaddr*)&servaddr, sizeof(servaddr));
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

bool checkReceivedFromPrimary(struct sockaddr_in clientaddr, socklen_t clientaddrlen) {
    cout << "primaryInfo -> " << primaryInfo.ip << ":" << primaryInfo.tcpPort << endl; // Directly print in host byte order
    cout << "Sent From -> " << inet_ntoa(clientaddr.sin_addr) << ":" << ntohs(clientaddr.sin_port) << endl; // Convert network to host byte order for printing
    bool x = (primaryInfo.ip == inet_ntoa(clientaddr.sin_addr)) && (htons(primaryInfo.tcpPort) == clientaddr.sin_port);
    cout << (x ? "true" : "false") << endl;
    return x;
}

bool checkReceivedFromSecondary(struct sockaddr_in clientaddr, socklen_t clientaddrlen) {
    // Reference to the vector of servers in the specified group
    auto& serverList = servers[myInfo.replicaGroup]; 
    
    for (auto it = serverList.begin(); it != serverList.end(); ++it) {
        if (it->isPrimary == false) {
            if((it->ip == inet_ntoa(clientaddr.sin_addr)) && (it->tcpPort == ntohs(clientaddr.sin_port))){
                return true;
            }
        }
    }
    return false;
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
            cout << "Command at failure is -> " << command << endl;
            fprintf(stderr, "Failed to read: %s\n", strerror(errno));
            exit(1);
        } else if (bytesRead == 0) {
            // Connection closed by client
            break;
        }

        for (int i = 0; i < bytesRead; ++i) {
            char ch = buffer[i];
            if (ch == '\r') {
                // Assuming next char is '\n', check boundary
                if (i + 1 < bytesRead && buffer[i + 1] == '\n') {
                    // cout << "COMMAND COMPLETE!!!" << endl;
                    if (debug) {
                        fprintf(stderr, "[ %d ] C: %s\n", conFD, command.c_str());
                    }

                    vector<string> parameters = splitKvstoreCommand(command);
                    bool receivedFromPrimary = false;
                    bool receivedFromSecondary = false;
                    bool receivedFromFrontend = false;
                    cout << "Command receieved size is -> " << command.size() << endl;
                    if (parameters.size() >= 4 && (parameters[0] == "PUT " || parameters[0] == "CPUT " || parameters[0] == "DELETE ")) {
                        if (parameters[parameters.size()-1] == "PRIMARY") {
                            receivedFromPrimary = true;
                            cout << "Received from PRIMARY is TRUE" <<endl;
                        } else if (parameters[parameters.size()-1] == "SECONDARY") {
                            cout << "Received from SECONDARY is TRUE"  << endl;
                            receivedFromSecondary = true;
                        } else {
                            cout << "Received from Frontend is TRUE" << endl;
                            receivedFromFrontend = true;
                        }
                    } else {
                        cout << "Received from Frontend is TRUE" << endl;
                        receivedFromFrontend = true;
                    }
                    handleCommand(parameters, msg, command, receivedFromPrimary, receivedFromSecondary, receivedFromFrontend);

                    // Checkpointing
                    if (currentNumberOfWritesForReplicaAndServer == CHECKPOINTING_THRESHOLD) {
                        checkpoint_table(diskFilePath);
                        currentNumberOfWritesForReplicaAndServer = 0;
                    }


                    cout << "Sending message back -> " <<  msg << endl;
                    if (write(conFD, msg.c_str(), msg.length()) < 0) {
                        fprintf(stderr, "Failed to write: %s\n", strerror(errno));
                        exit(1);
                    }

                    if (debug) {
                        fprintf(stderr, "[ %d ] S: %s\n", conFD, msg.c_str());
                    }

                    // reset for the next command
                    command.clear();
                    i++; // Skip '\n' as it's already processed
                } else {
                    // Handle cases where '\n' might come in the next batch
                    command += '\r'; // Keep '\r' in command as we're unsure about '\n'
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

    if (debug) {
        fprintf(stderr, "[ %d ] Connection closed \n", conFD);
    }

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
            // cout << "  IP: " << info.ip << ", TCP Port: " << info.tcpPort << ", UDP Port: " << info.udpPort << endl;
        }
    }

    // create a separate thread to send heartbeat to master node
    thread heartbeat_thread(sendHeartbeat);
    heartbeat_thread.detach();
    //receive status message (pri/secondary)
    thread status_thread(receiveStatus);
    status_thread.detach();


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

        if(debug){
            fprintf(stderr, "[ %d ] New connection\n", data->conFD);
        }

        string greeting = "+OK Server ready (Author: Rishi Ghia / ghiar) \r\n";
        if (write(data->conFD, greeting.c_str(), greeting.length()) < 0){
            fprintf(stderr, "Failed to write: %s\n", strerror(errno));
            exit(1);
        }
        
        if(debug){
            fprintf(stderr, "[ %d ] +OK Server ready (Author: Rishi Ghia / ghiar) \n", data->conFD);
        }
        
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