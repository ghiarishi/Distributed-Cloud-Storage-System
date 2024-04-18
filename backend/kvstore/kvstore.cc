// imports
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h> 
#include <pthread.h>
#include <vector>
#include <signal.h>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <iterator>
#include <sstream>
#include <string>
#include <fstream>
#include "helper.h"
#include "constants.h"

using namespace std;

// Define the key-value store
vector<int> openConnections;

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

// Function to split command received by this backend KVStore server. Split into actually command and the parameters - row,column,binaryvalue
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

// Function to handle the split command given as parameters vector
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
    } else {
        msg = "-ERR Unknown command\r\n";
    }
}

// Function to initialize in-memory map - which reads diskfile and replays the checkpointing file
// TODO : Contact primary server and see if it is out of sync with them and update accordingly
void initialize(string path) {
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

// thread function to run commands
void* threadFunc(void* arg) {
    struct thread_data* data = (struct thread_data*)arg;
    int conFD = data->conFD;

    char ch;
    string command;
    string msg;
    bool prevCharReturn = false;

    while (true) { // constantly accept input from client, one char at a time
        if (read(conFD, &ch, 1) < 0){
            fprintf(stderr, "Failed to read: %s\n", strerror(errno));
            exit(1);
        }
        if (ch == '\r') { // if return carriage
            prevCharReturn = true;
        } 
        else if (ch == '\n' && prevCharReturn) { // if /r followed by /n, command is complete
            if(debug){
                fprintf(stderr, "[ %d ] C: %s\n", data->conFD, command.c_str());
            }

            cout<<command<<endl;

            vector<string> parameters = splitKvstoreCommand(command);
            // command is complete, execute it

            handleCommand(parameters, msg, command);

            if (currentNumberOfWritesForReplicaAndServer == CHECKPOINTING_THRESHOLD) {
                checkpoint_table(diskFilePath);
                currentNumberOfWritesForReplicaAndServer = 0;
            }

            if (write(conFD, msg.c_str(), msg.length()) < 0){
                fprintf(stderr, "Failed to write: %s\n", strerror(errno));
                exit(1);
            }                   
            if(debug){
                fprintf(stderr, "[ %d ] S: %s\n", conFD, msg.c_str());
            }
    
            // reset for the next command
            command.clear();
            prevCharReturn = false;
        }
        else {
            if (!prevCharReturn) { // Don't add '\r' to the command string
                command += ch;
            }
        }
    }

    if (close(conFD) < 0){ // close connection
        fprintf(stderr, "Failed to close connection: %s\n", strerror(errno));
        exit(1);
    }

    if(debug){
        fprintf(stderr, "[ %d ] Connection closed \n", data->conFD);
    }

    // remove the socket from the open connections vector
    vector<int>::iterator it = find(openConnections.begin(), openConnections.end(), conFD);
    if (it != openConnections.end()) {
        openConnections.erase(it);
    }

    return NULL;
}

int main(int argc, char *argv[]){
    signal(SIGINT, sigHandler);
    parseArguments(argc, argv);
    
    if (debug) {
        printDebug("Arguments parsed");
    }

    diskFilePath = "./storage/RP" + to_string(replicaGroup) + "-" + to_string(portNum);
    initialize(diskFilePath);

    if (debug) {
        printDebug("Initialization Done");
    }

    if(aFlag) {
        cerr<<"Name: Rishi Ghia, SEAS Login: ghiar"<<endl;
        return 0;
    }
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
    servaddr.sin_port = htons(portNum);

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