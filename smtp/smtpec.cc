#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <cstring>
#include <netdb.h>
#include <arpa/inet.h>
#include <arpa/nameser.h> 
#include <resolv.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <thread>
#include <fcntl.h>
#include <sys/file.h>
#include <mutex>
#include <map>
#include <set>
#include <dirent.h> 
#include <regex>
using namespace std;

const int SMTP_PORT = 2500;
regex SERVICE_READY("220 .*\\r\\n");
regex OK_RESPONSE("250 .*\\r\\n"); 
regex DATA_RESPONSE("354 .*\\r\\n"); 
regex UNKNOWN_COMMAND("500 .*\\r\\n");

// Struck to store email Info
typedef struct {
    string recipient;
    string senderEmail;
    string content;
    bool sent = false;
} EmailInfo;

vector<EmailInfo> emails;

// Check if the response is an OK or DATA Response
bool isOKResponse(string str) {
    return regex_match(str, OK_RESPONSE) || regex_match(str, DATA_RESPONSE);
}

// Build each email info and store it in vector of emails with struct type EmailInfo
void buildEmailInfo(fstream& file) {
    // Assuming lines are not longer than 4096 characters
    char line[4096]; 
    string emailStart = "From <";
    string emailEnd = "\n\n";
    string toStart = "To <";
    string toEnd = ">";
    string content;
    bool inEmail = false;
    string senderEmail;
    string recipient;

    // Go through entire file
    while (file.getline(line, sizeof(line))) {
        // If we encounter "From < means we start a new email and old email must be saved -  this line is metadata"
        if (strstr(line, emailStart.c_str())) {
            // We were in an email
            if (inEmail) {
                // End of previous email, save the email info
                EmailInfo email;
                email.content = content;
                email.senderEmail = senderEmail;
                email.recipient = recipient;
                email.sent = false;
                emails.push_back(email);
                content = "";
            }
            inEmail = true;
            size_t senderPos = string(line).find(emailStart) + emailStart.length();
            senderEmail = string(line).substr(senderPos, string(line).find(toEnd)-senderPos);
        }

        // To < line encountered, again metadata
        else if (strstr(line, toStart.c_str())) {
            size_t pos = string(line).find(toStart) + toStart.length();
            recipient = string(line).substr(pos, string(line).find(toEnd) - pos);
        }

        // Actual email content
        else if (inEmail) {
            content += line;
            // Add the newline character back
            content += "\n"; 
        }

        
        if (strstr(line, emailEnd.c_str())) {
            inEmail = false;
        }
    }

    // Add the last email
    if (inEmail) {
        EmailInfo email;
        email.content = content;
        email.senderEmail = senderEmail;
        email.recipient = recipient;
        emails.push_back(email);
    }
}

// Function to connect with IP Address for SMTP server and send commands and content
bool attemptDelivery(EmailInfo email, const string& mxServer, bool verbose, string domain, struct hostent* server) {
    // Establish socket
    int sockfd;
    sockfd = socket(AF_INET, SOCK_STREAM,0 );
    if (sockfd < 0) {
        cerr << "Error creating socket" << endl;
        return false;
    }
    struct sockaddr_in serverSockAddr;
    bzero(&serverSockAddr, sizeof(serverSockAddr));
    serverSockAddr.sin_family = AF_INET;
    serverSockAddr.sin_port = htons(25);
    struct in_addr addr;
    // Copy IP Address
    memcpy(&addr, server->h_addr_list[0], sizeof(struct in_addr));
    memcpy(&serverSockAddr.sin_addr, &addr, sizeof(struct in_addr));

    // Try to connect with server
    if (connect(sockfd, (struct sockaddr *)&serverSockAddr, sizeof(serverSockAddr)) < 0) {
        cerr << "Error connecting to MX server: " << mxServer << endl;
        return false;
    }
    // Receive initial greeting (220 response)
    char buffer[256];
    int bytesRead = recv(sockfd, buffer, sizeof(buffer), 0);
    string str(buffer,bytesRead);
    bool matched = regex_match(str, SERVICE_READY);
    if (bytesRead <= 0 || !matched) {
        cerr << "Error: Unexpected greeting from MX server" << endl;
        return false;
    }
    if (verbose) cout << "[S] " << buffer;

    string domainCommand = "HELO " + email.senderEmail.substr(email.senderEmail.find('@') + 1) + "\r\n";
    string mailFrom = "MAIL FROM: <"+ email.senderEmail + ">\r\n";
    // Simplified SMTP commands (HELO, MAIL FROM, RCPT TO, DATA)
    vector<string> commands = {
        domainCommand,
        mailFrom,  
        "RCPT TO: <" + email.recipient + ">\r\n",
        "DATA\r\n"
    };

    for (const string& cmd : commands) {
        if (send(sockfd, cmd.c_str(), cmd.length(), 0) <= 0) {
            cerr << "Error sending command: " << cmd << endl;
            // Exit on failure 
            return false; 
        }
        memset(buffer, 0, sizeof(buffer));
        bytesRead = recv(sockfd, buffer, sizeof(buffer), 0);
        string str(buffer,bytesRead);
        if (bytesRead <= 0 || !isOKResponse(str)) { 
            cerr << "Error in SMTP exchange" << endl;
            return false;
        }
        if (verbose) cout << "[S] " << str;
    }

    send(sockfd, email.content.c_str(), email.content.length(), 0);
    send(sockfd, "\r\n", 2, 0); 
    if (send(sockfd, ".\r\n", 3, 0) <= 0) {
        cerr << "Error: Failed to send end of email marker" << endl;
        // Indicate failure
        return false;  
    }

    // Check for final response indicating success
    char buffer1[256];
    bytesRead = recv(sockfd, buffer1, sizeof(buffer1), 0);

    string str1(buffer1, bytesRead);
    if (verbose) {
        cout << "[S] " << str1;
    }
    if (bytesRead <= 0 || !isOKResponse(str1)) {
        cerr << "Error: Email delivery likely failed (check server logs)" << endl;
        // Indicate potential failure
        return false; 
    } 

    // Delivery successful
    close(sockfd);
    return true;

}

// MX Records for a domain
vector<string> getMxRecordsForDomain(const string& domain) {
    vector<string> mxRecords;
    u_char nsbuf[4096];
    char dispbuf[4096];
    int responseLength;

    if (res_init() != 0) {
        cerr << "Error initializing resolver" << endl;
        return mxRecords; 
    }


    int anslen;
    responseLength = res_query(domain.c_str(), ns_c_in, ns_t_mx, nsbuf, sizeof(nsbuf));
    if (responseLength < 0) {
        cerr << "Error querying MX records for domain: " << domain << endl;
        return mxRecords; 
    }


    // Parse DNS response (simplified)
    ns_msg msg;
    if (ns_initparse(nsbuf, responseLength, &msg) < 0) {
        cerr << "Error parsing MX response" << endl;
        return mxRecords;
    }


    ns_rr rr;
    for (int i = 0; i < ns_msg_count(msg, ns_s_an); i++) {
        if (ns_parserr(&msg, ns_s_an, i, &rr) == 0) {
            ns_sprintrr(&msg, &rr, NULL, NULL, dispbuf, sizeof(dispbuf));
            mxRecords.push_back(dispbuf);
        }
    }

    return mxRecords;
}

// Process command line arguments
void processCommandLineArgs(int argc, char* argv[], string& mailboxDirectory, bool& verbose) {
    int opt;
    while ((opt = getopt(argc, argv, "p:av")) != -1) {
        switch (opt) {
            case 'v':
                verbose = true;
                break;
            case 'a':
                cout << "Pranshu Kumar (pranshuk)" << endl;
                exit(0);
            default:
                cerr << "Invalid command line option." << endl;
                exit(1);
        }
    }

    // Check for the directory argument (after processing flags)
    if (optind >= argc) { 
        cerr << "Usage: " << argv[0] << " <directory> [-v] [-p port] [-a]" << endl;
        exit(1);
    }

    mailboxDirectory = argv[optind];

    if (mailboxDirectory.empty()) {
        cerr << "Error: Mailbox directory is required." << endl;
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    string mailboxDirectory = "";
    bool verbose = false;

    // Process command-line arguments
    processCommandLineArgs(argc, argv, mailboxDirectory, verbose);

    // Open the mqueue file
    string mqueueFile = mailboxDirectory + "/mqueue";

    int fd = open(mqueueFile.c_str(), O_RDWR);
    if (fd == -1) {
        cerr << "Error opening mqueue file for locking: " << mqueueFile << endl;
        return 1;
    }

    // Attempt to obtain an exclusive lock
    if (flock(fd, LOCK_EX) == -1) {
        cerr << "Error obtaining exclusive lock on mqueue file: " << mqueueFile << endl;
        close(fd); 
        return 1;
    }

    // Lock obtained -  open the file stream for both reading and writing
    fstream mqueue(mqueueFile);
    if (!mqueue.is_open()) {
        cerr << "Error opening mqueue file after locking : " << mqueueFile << endl;
        flock(fd,LOCK_UN);
        close(fd);
        return 1;
    }

    buildEmailInfo(mqueue);

    if (emails.size() > 0) {
        for(int i = 0; i < emails.size(); i++) {
            string domain = emails[i].recipient.substr(emails[i].recipient.find('@') + 1);
            vector<string> mxRecords = getMxRecordsForDomain(domain);
            if (mxRecords.empty()) {
                cerr << "Error: No MX records found for domain: " << domain << endl;
                 // Skip to the next email
                continue;
            }
            struct hostent* host_info = NULL;
            // Attempt delivery to each MX server
            for (const string& mxServer : mxRecords) {
                size_t lastSpace = mxServer.find_last_of(' ');
                string lastString = mxServer.substr(lastSpace + 1);
                lastString.pop_back();
                host_info = gethostbyname(lastString.c_str());
                if (host_info == nullptr) {
                    cerr << "Failed to get host info" << endl;
                }

                if (host_info->h_addr_list[0] == nullptr) {
                    cerr << "No IP addresses found" << endl;
                }

                struct in_addr addr;
                memcpy(&addr, host_info->h_addr_list[0], sizeof(struct in_addr));
                if (attemptDelivery(emails[i], inet_ntoa(addr), verbose, domain, host_info)) { 
                    emails[i].sent = true; 
                    mxRecords.clear();
                    // Move to the next email in the mqueue file, as this email was delivered, and we don;t need to try a new mx record
                    break; 
                } 
            }
        }
        // Seek to the beginning of the file and truncate
        lseek(fd, 0, SEEK_SET);
        ftruncate(fd, 0);
        fstream mqueue(mqueueFile, ios::out); 
        if (!mqueue.is_open()) {
            cerr << "Error opening mqueue file for writing: " << mqueueFile << endl;
            return 1;
        }

        // Write unsent mails to file
        for (int i = 0; i < emails.size(); i++) {
            if (!emails[i].sent) { 
                mqueue << "From <" << emails[i].senderEmail << ">\r\n";
                mqueue << "To <" << emails[i].recipient << ">\r\n";
                 // Include the newline
                mqueue << emails[i].content;
            }
        }
    }

    // Close file and release lock
    mqueue.close();
    flock(fd, LOCK_UN);
    close(fd);

    return 0;
}
