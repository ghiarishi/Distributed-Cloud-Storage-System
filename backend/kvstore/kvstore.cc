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

int currentNumberOfWritesForReplicaAndServer = 0;
string diskFilePath;

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

// thread struct
struct thread_data {
    int conFD;
    pthread_t threadID;
};

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
    if (command.size() > 0) {
        appendToFile(diskFilePath + "-checkpoint", command);
        currentNumberOfWritesForReplicaAndServer++;
    }
}

void handleCommand(vector<string> parameters, string &msg, string command = "") {
    if (parameters[0] == "PUT ") {
        if (parameters.size() == 4) {
            string row = parameters[1];
            string col = parameters[2];
            string value = parameters[3];  // Here, value is directly used as a string
            table[row][col] = value;
            msg = "+OK\r\n";
            handleAppend(command);
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
        if (parameters.size() == 5) {
            string row = parameters[1];
            string col = parameters[2];
            string currentValue = parameters[3];
            string newValue = parameters[4];
            if (table[row][col] == currentValue) {
                table[row][col] = newValue;
                msg = "+OK\r\n";
                handleAppend(command);
            } else {
                msg = "-ERR Conditional Write Failed\r\n";
            }
        } else {
            msg = "-ERR Invalid CPUT parameters\r\n";
        }
    } else if (parameters[0] == "DELETE ") {
        if (parameters.size() == 3) {
            string row = parameters[1];
            string col = parameters[2];
            table[row].erase(col);
            msg = "+OK\r\n";
            handleAppend(command);
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
        while (getline(logFile, line)) {
            currentNumberOfWritesForReplicaAndServer++;
            vector<string> parameters = splitKvstoreCommand(line);
            handleCommand(parameters, msg);
        }
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

                for (auto info:servers[replicaGroup]){
                    if (info.tcpPort == myInfo.tcpPort && info.ip == myInfo.ip){
                        info = myInfo;
                    }
                }

                size_t posAt = message.find("@");

                // if secondary server info provided, then mark alive
                if (posAt != std::string::npos) {
                    string ip_port = message.substr(posColon+1, posAt - posColon - 1);

                    string ipTemp = ip_port.substr(0, ip_port.find(":"));

                    int tcpTemp = stoi(ip_port.substr(ip_port.find(":") + 1));

                    cout<<"Primary marks secondary alive in group " << to_string(replicaGroup)<<endl;

                    for (auto info:servers[replicaGroup]){
                        if (info.tcpPort == tcpTemp && info.ip == ipTemp){
                            info.isDead = false;
                            info.isPrimary = false;
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

                for (auto info:servers[replicaGroup]){
                    if (info.tcpPort == myInfo.tcpPort && info.ip == myInfo.ip){
                        info = myInfo;
                    }

                    if (info.tcpPort == tcpTemp && info.ip == ipTemp){
                        info.isPrimary = true;
                        primaryInfo = info;
                    }
                }
            }

    

            // SERVER IS DEAD
            size_t posSemi = message.find(";");
            if (posSemi != std::string::npos) {
                int deadTCP = stoi(message.substr(posSemi + 1));
                auto& serverList = servers[replicaGroup]; // Reference to the vector of servers in the specified group
                for (auto it = serverList.begin(); it != serverList.end(); ++it) {
                    if (it->tcpPort == deadTCP){
                        it->isDead = true;
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

        this_thread::sleep_for(chrono::seconds(HEARTBEAT_INTERVAL)); // Send every 2 seconds, adjusted from your comment
    }

    close(sockfd);
}

// OLD THREAD FUNC (READ 1 CHAR AT A TIME)
// // thread function to run commands
// void* threadFunc(void* arg) {
//     struct thread_data* data = (struct thread_data*)arg;
//     int conFD = data->conFD;

//     char ch;
//     string command;
//     string msg;
//     bool prevCharReturn = false;
    
//     while (true) { // constantly accept input from client, one char at a time
//         if (read(conFD, &ch, 1) < 0){
//             fprintf(stderr, "Failed to read: %s\n", strerror(errno));
//             exit(1);
//         }

//         if (ch == '\r') { // if return carriage
//             prevCharReturn = true;
//         } 
//         else if (ch == '\n' && prevCharReturn) { // if /r followed by /n, command is complete

//             cout<<"COMMAND COMPLETE!!!"<<endl;
//             if(debug){
//                 fprintf(stderr, "[ %d ] C: %s\n", data->conFD, command.c_str());
//             }
//             cout<<command<<endl;

//             vector<string> parameters = splitKvstoreCommand(command);
//             // command is complete, execute it

//             handleCommand(parameters, msg, command);

//             if (currentNumberOfWritesForReplicaAndServer == CHECKPOINTING_THRESHOLD) {
//                 checkpoint_table(diskFilePath);
//                 currentNumberOfWritesForReplicaAndServer = 0;
//             }

//             if (write(conFD, msg.c_str(), msg.length()) < 0){
//                 fprintf(stderr, "Failed to write: %s\n", strerror(errno));
//                 exit(1);
//             }                   
//             if(debug){
//                 fprintf(stderr, "[ %d ] S: %s\n", conFD, msg.c_str());
//             }
    
//             // reset for the next command
//             command.clear();
//             prevCharReturn = false;
//         }
//         else {
//             if (!prevCharReturn) { // Don't add '\r' to the command string
//                 cout<<command.size()<<endl;
//                 command += ch;
//             }
//         }
//     }
//     if (close(conFD) < 0){ // close connection
//         fprintf(stderr, "Failed to close connection: %s\n", strerror(errno));
//         exit(1);
//     }

//     if(debug){
//         fprintf(stderr, "[ %d ] Connection closed \n", data->conFD);
//     }

//     // remove the socket from the open connections vector
//     vector<int>::iterator it = find(openConnections.begin(), openConnections.end(), conFD);
//     if (it != openConnections.end()) {
//         openConnections.erase(it);
//     }

//     return NULL;
// }

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
                    cout << "COMMAND COMPLETE!!!" << endl;
                    if (debug) {
                        fprintf(stderr, "[ %d ] C: %s\n", conFD, command.c_str());
                    }

                    cout << "Command Size" << command.size() << endl;

                    vector<string> parameters = splitKvstoreCommand(command);
                    handleCommand(parameters, msg, command);

                    if (currentNumberOfWritesForReplicaAndServer == CHECKPOINTING_THRESHOLD) {
                        checkpoint_table(diskFilePath);
                        currentNumberOfWritesForReplicaAndServer = 0;
                    }

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
    initialize(diskFilePath);

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