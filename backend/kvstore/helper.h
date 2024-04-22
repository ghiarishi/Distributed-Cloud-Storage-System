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

#define BUFFER_SIZE 1024

using namespace std;

// Define the key-value store
unordered_map<string, unordered_map<string, string>> table;
vector<int> openConnections;

bool aFlag = false;
bool debug = false; // default no debugging
int replicaGroup = 0;

struct ServerInfo {
    std::string ip;
    int tcpPort;
    int udpPort;
    int udpPort2;
    bool isPrimary = false; // default false
    bool isDead = false;
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
    cerr << debugLog << endl;
}

// Function to truncate fileName given as parameter/argument
void truncateFile(string fileName) {
    ofstream file(fileName, ofstream::out | ofstream::trunc);
    file.close();
}

// Function to write to disk file from the in-memory table
void checkpoint_table(const string& diskFile) {
    string temp_filename = diskFile + ".tmp"; // Temporary filename

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

        // Rename on success
        remove(diskFile.c_str());
        rename(temp_filename.c_str(), diskFile.c_str());
        truncateFile(diskFile + "-checkpoint"); 
    } else {
        cerr << "Error opening temporary file for writing" << endl;
    }
}

// Function to append to filePath - the line to append is also given as a parameter
void appendToFile(string filePath, string lineToAppend) {
    ofstream file(filePath, ios::app);
    if (file.is_open()) {
        file << lineToAppend << "\n";
        file.close();
    } else {
        cerr << "Error opening file " << filePath << endl;
    }
}

// Helper function to extract the value after a colon in a config string
string extractValue(const string& data) {
    auto pos = data.find(':');
    if (pos != string::npos && pos + 1 < data.size()) {
        return data.substr(pos + 1);
    }
    return "";
}


void parseServers(const std::string& filename, ServerMap& servers) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error opening file" << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::vector<std::string> parts;
        std::string part;
        while (std::getline(iss, part, ',')) {
            size_t pos = part.find(':');
            if (pos != std::string::npos) {
                parts.push_back(part.substr(pos + 1));
            } else {
                std::cerr << "Invalid format in part: " << part << std::endl;
            }
        }

        if (parts.size() != 5) {
            std::cerr << "Invalid line format: " << line << std::endl;
            continue;  // Skip malformed lines
        }

        ServerInfo info;
    
        info.replicaGroup = std::stoi(parts[0]);
        info.ip = parts[1];
        info.tcpPort = std::stoi(parts[2]);
        info.udpPort = std::stoi(parts[3]);
        info.udpPort2 = std::stoi(parts[4]);

        servers[info.replicaGroup].push_back(info);

    }
}
