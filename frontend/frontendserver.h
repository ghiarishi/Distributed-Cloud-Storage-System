#ifndef FRONTENDSERVER_H
#define FRONTENDSERVER_H

#include <string>
#include <vector>

// Represents a node structure containing various network attributes
struct Node {
    int id;             // Unique identifier of the node
    std::string ip;     // IP address of the node
    int tcp;            // TCP port number
    int udp;            // UDP port number
    int udp2;           // Secondary UDP port number
    std::string name;   // Name of the node
    bool isAlive;       // Status indicating if the node is active
    int socket;         // Socket descriptor
    int port;           // Port number for specific operations

    // Default constructor
    Node() {};

    // Parameterized constructor to initialize Node attributes
    Node(int id, std::string ip, int tcp, int udp, int udp2, std::string name)
        : id(id), ip(ip), tcp(tcp), udp(udp), udp2(udp2), name(name) {}
};

// Represents the structure of an email
struct email {
    std::string from;      // Sender's address
    std::string epochTime; // Timestamp of when the email was sent
    std::string content;   // The main content/body of the email
    std::string id;        // Unique identifier for the email

    // Default constructor
    email() {};
};

// Global variables

// Server and general configurations
extern int PORT;
extern int DEBUG;
extern size_t READ_SIZE;
extern size_t FBUFFER_SIZE;
extern const int MAX_CLIENTS;
extern int mail_sock;
extern const int buffer_size;

// Client management
extern volatile int client_socks[];
extern volatile int num_client;
extern volatile int shutting_down;

// Backend server details
extern std::vector<Node> backend_socks;
extern std::string backendIP;
extern int backendPort;

// HTTP response codes
extern int NOTFOUND;
extern int FORBIDDEN;

// Application-specific pages or features
extern int REDIRECT;
extern int LOGIN;
extern int MENU;
extern int MAILBOX;
extern int DRIVE;
extern int EMAIL;
extern int SENDEMAIL;
extern int FORWARD;
extern int DOWNLOAD;
extern int RENAME;
extern int MOVE;
extern int DELETE;
extern int NEWDIR;
extern int UPLOAD;
extern int ADMIN;
extern int SIGNUP;
extern int NEWPASS;

#endif // FRONTENDSERVER_H
