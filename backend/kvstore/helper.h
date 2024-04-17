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