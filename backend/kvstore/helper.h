#include "constants.h"
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <map>
#include <vector>
#include <thread>
#include <tuple>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unordered_map>

using namespace std;


// define the key-value store as a map of maps
unordered_map<string, unordered_map<string, string>> table;
vector<int> openConnections;

bool aFlag = false;
bool debug = false; // default no debugging
int replicaGroup = 0;

struct ServerInfo {
    string ip;
    int tcpPort; // communication with frontend port
    int udpPort; // heartbeat port
    int udpPort2; // status message port
    int tcpPort2; // enable disable port
    bool isPrimary = false; // secondary by default
    bool isDead = true; // dead by default
    int replicaGroup;
};

typedef map<int, vector<ServerInfo>> ServerMap;
ServerMap servers;
ServerInfo myInfo;

// Function to parse command line arguments
void parseArguments(int argc, char *argv[]) {
    int opt;    
    while ((opt = getopt(argc, argv, "i:p:av")) != -1) {
        switch (opt) {
            case 'p':
                myInfo.tcpPort = stoi(optarg);
                break;
            case 'i':
                replicaGroup = stoi(optarg);
                break;
            case 'a':
                // full name and seas login to stderr, then exit
                aFlag = true;
                break;
            case 'v': 
                // print debug output to server
                debug = true;
                break;
        }
    }
}

void printDebug(string debugLog) {
    if (debug){
        cerr << debugLog << endl;
    }
}

// function to truncate fileName given as parameter/argument
void truncateFile(string fileName) {
    ofstream file(fileName, ofstream::out | ofstream::trunc);
    file.close();
}

// function to write to disk file from the in-memory table
void checkpoint_table(const string& diskFile) {
    string temp_filename = diskFile + ".tmp"; // temporary filename

    ofstream tempFile(temp_filename);

    string line;
    int currentMessageNum = 0;
    bool writeMessage = true;

    if (tempFile.is_open()) {
        time_t result = time(nullptr);
        tempFile << result << endl;
        for (const auto& [row, columnMap] : table) {
            for (const auto& [column, value] : columnMap) {
                tempFile << row << "," << column << "," << value << "\n";
            }
        }
        tempFile.close();

        // rename on success
        remove(diskFile.c_str());
        rename(temp_filename.c_str(), diskFile.c_str());
        truncateFile(diskFile + "-checkpoint"); 
    } else {
        cerr << "Error opening temporary file for writing" << endl;
    }
}

// function to append to filePath - the line to append is also given as a parameter
void appendToFile(string filePath, string lineToAppend) {
    ofstream file(filePath, ios::app);
    if (file.is_open()) {
        file << lineToAppend << "\n";
        file.close();
    } else {
        cerr << "Error opening file " << filePath << endl;
    }
}

// helper function to extract the value after a colon in a config string
string extractValue(const string& data) {
    auto pos = data.find(':');
    if (pos != string::npos && pos + 1 < data.size()) {
        return data.substr(pos + 1);
    }
    return "";
}

// go through config.txt file and extract server data
void parseServers(const string& filename, ServerMap& servers) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Error opening file" << endl;
        return;
    }

    string line;
    while (getline(file, line)) {
        istringstream iss(line);
        vector<string> parts;
        string part;
        while (getline(iss, part, ',')) {
            size_t pos = part.find(':');
            if (pos != string::npos) {
                parts.push_back(part.substr(pos + 1));
            } else {
                cerr << "Invalid format in part: " << part << endl;
            }
        }

        if (parts.size() != 6) {
            cerr << "Invalid line format: " << line << endl;
            // skip malformed lines
            continue;  
        }

        ServerInfo info;
    
        info.replicaGroup = stoi(parts[0]);
        info.ip = parts[1];
        info.tcpPort = stoi(parts[2]);
        info.udpPort = stoi(parts[3]);
        info.udpPort2 = stoi(parts[4]);
        info.tcpPort2 = stoi(parts[5]);

        servers[info.replicaGroup].push_back(info);
    }
}