using namespace std;

bool aFlag = false;
bool debug = false; // default no debugging
int portNum = 10000; // default 10000
int replicaGroup = 0;


unordered_map<string, unordered_map<string, string>> table;

// Function to parse command line arguments
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