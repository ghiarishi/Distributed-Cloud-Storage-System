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

using namespace std;

// Define the key-value store
unordered_map<string, unordered_map<string, string>> table;

vector<int> openConnections;
bool debug = false; // default no debugging
int portNum = 10000; // default 10000

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

vector<string> parseParameters(const string& command) {
    istringstream iss(command);
    return vector<string>((istream_iterator<string>(iss)), istream_iterator<string>());
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
            // command is complete, execute it
            if (command.rfind("PUT ", 0) == 0) {
                auto params = parseParameters(command.substr(4));
                if (params.size() == 3) {
                    string row = params[0];
                    string col = params[1];
                    string value = params[2];  // Here, value is directly used as a string
                    table[row][col] = value;
                    msg = "+OK\r\n";
                } else {
                    msg = "-ERR Invalid PUT parameters\r\n";
                }
            } else if (command.rfind("GET ", 0) == 0) {
                auto params = parseParameters(command.substr(4));
                if (params.size() == 2) {
                    string row = params[0];
                    string col = params[1];
                    if (table.find(row) != table.end() && table[row].find(col) != table[row].end()) {
                        msg = "+OK " + table[row][col] + "\r\n";
                    } else {
                        msg = "-ERR Not Found\r\n";
                    }
                } else {
                    msg = "-ERR Invalid GET parameters\r\n";
                }
            } else if (command.rfind("CPUT ", 0) == 0) {
                auto params = parseParameters(command.substr(5));
                if (params.size() == 4) {
                    string row = params[0];
                    string col = params[1];
                    string currentValue = params[2];
                    string newValue = params[3];
                    if (table[row][col] == currentValue) {
                        table[row][col] = newValue;
                        msg = "+OK\r\n";
                    } else {
                        msg = "-ERR Conditional Write Failed\r\n";
                    }
                } else {
                    msg = "-ERR Invalid CPUT parameters\r\n";
                }
            } else if (command.rfind("DELETE ", 0) == 0) {
                auto params = parseParameters(command.substr(7));
                if (params.size() == 2) {
                    string row = params[0];
                    string col = params[1];
                    table[row].erase(col);
                    msg = "+OK\r\n";
                } else {
                    msg = "-ERR Invalid DELETE parameters\r\n";
                }
            } else {
                msg = "-ERR Unknown command\r\n";
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

    // getopt to parse '-p', '-a', '-v' argument options
    int opt;    
    while ((opt = getopt(argc, argv, "p:av")) != -1) {
        switch (opt) {
            case 'p':
                // use specified port, default 10000
                portNum = stoi(optarg);
               break;
            case 'a':
                // full name and seas login to stderr, then exit
                cerr<<"Name: Rishi Ghia, SEAS Login: ghiar"<<endl;
                return 0;
            case 'v': 
                // print debug output to server
                debug = true;
                break;
        }
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