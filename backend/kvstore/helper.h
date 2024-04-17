#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <tuple>

using namespace std;

bool aFlag = false;
bool debug = false; // default no debugging
int portNum = 10000; // default 10000
int replicaGroup = 0;

void parseArguments(int argc, char *argv[]) {
    int opt;    
    while ((opt = getopt(argc, argv, "i:p:av")) != -1) {
        switch (opt) {
            case 'p':
                // use specified port, default 10000
                portNum = stoi(optarg);
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
    int port;
};

typedef map<int, vector<ServerInfo>> ServerMap;

void parseServers(const string& filename, ServerMap& servers) {

    cout<<"READING"<<endl;

    ifstream file(filename);
    string line;

    if (!file.is_open()) {
        cerr << "Error opening file" << endl;
        return;
    }

    while (getline(file, line)) {
        istringstream iss(line);
        string part;
        int id;
        string ip;
        int port;

        // Parse ID
        getline(iss, part, ',');
        sscanf(part.c_str(), "id:%d", &id);

        // Parse IP
        getline(iss, part, ',');
        ip = part.substr(part.find(':') + 1);


        // Parse Port
        getline(iss, part);
        sscanf(part.c_str(), "port:%d", &port);
        // Store in map
        servers[id].push_back({ip, port});
    }

    file.close();
}
