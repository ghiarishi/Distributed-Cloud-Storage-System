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

string masterIP;
int masterTCP;
int masterUDP;

string myIP;
int myTCP;
int myUDP;

// Function to parse command line arguments
void parseArguments(int argc, char *argv[]) {
    int opt;    
    while ((opt = getopt(argc, argv, "i:p:av")) != -1) {
        switch (opt) {
            case 'p':
                myTCP = stoi(optarg);
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

struct ServerInfo {
    string ip;
    int tcpPort;
    int udpPort;
};

void printDebug(string debugLog) {
    cerr << debugLog << endl;
}

typedef map<int, vector<ServerInfo>> ServerMap;

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


void parseServers(const string& filename, ServerMap& servers) {
    cout << "READING" << endl;

    ifstream file(filename);
    string line;

    if (!file.is_open()) {
        cerr << "Error opening file" << endl;
        return;
    }

    while (getline(file, line)) {
        istringstream iss(line);
        string part;
        vector<string> parts;
        while (getline(iss, part, ',')) {
            parts.push_back(part);
        }

        if (parts.size() != 4) {
            cerr << "Invalid line format: " << line << endl;
            continue;  // Skip malformed lines
        }

        int id = stoi(extractValue(parts[0]));
        string ip = extractValue(parts[1]);
        int tcpPort = stoi(extractValue(parts[2]));
        int udpPort = stoi(extractValue(parts[3]));

        // Store in map
        servers[id].push_back({ip, tcpPort, udpPort});
    }

    file.close();
}


void send_heartbeat(const string& ip, int masterUDP, int localUDP) {

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "Error opening socket" << endl;
        return;
    }

    // Local address structure for binding to a specific port
    struct sockaddr_in localaddr;
    localaddr.sin_family = AF_INET;
    localaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    localaddr.sin_port = htons(localUDP);

    if (bind(sockfd, (struct sockaddr*)&localaddr, sizeof(localaddr)) < 0) {
        cerr << "Error binding to local port" << endl;
        close(sockfd);
        return;
    }

    // Setup destination address structure
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(masterUDP);
    servaddr.sin_addr.s_addr = inet_addr(ip.c_str());

    const char* heartbeat_message = "Heartbeat Packet";
    while (true) {
        int send_status = sendto(sockfd, heartbeat_message, strlen(heartbeat_message),
                                 0, (const struct sockaddr*)&servaddr, sizeof(servaddr));
        if (send_status < 0) {
            cerr << "Error sending heartbeat" << endl;
        }
        cout<<"heartbeat"<<endl;

        this_thread::sleep_for(chrono::seconds(2)); // Send every 2 seconds, adjusted from your comment
    }

    close(sockfd);
}

#define BUFFER_SIZE 1024

void recvHeartbeat(int port) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "Error opening socket" << endl;
        return;
    }

    struct sockaddr_in servaddr, cliaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        cerr << "Bind failed" << endl;
        close(sockfd);
        return;
    }

    char buffer[BUFFER_SIZE];
    socklen_t len;
    int n;

    while (true) {
        len = sizeof(cliaddr);  //len is value/resuslt
        n = recvfrom(sockfd, (char *)buffer, BUFFER_SIZE, MSG_WAITALL, (struct sockaddr *) &cliaddr, &len);
        cout<<"recvd"<<endl;
        buffer[n] = '\0';

        cout << "Heartbeat received from IP: " << inet_ntoa(cliaddr.sin_addr)
             << " Port: " << ntohs(cliaddr.sin_port) << endl;
    }

    close(sockfd);
}