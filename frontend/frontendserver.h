#ifndef FRONTENDSERVER_H
#define FRONTENDSERVER_H

#include <string>
#include <vector>

// Node structure
struct Node
{
    int id;
    std::string ip;
    int tcp;
    int udp;
    int udp2;
    std::string name;
    bool isAlive;
    int socket;
    int port;

    Node(){};
    Node(int id, std::string ip, int tcp, int udp, int udp2, std::string name)
        : id(id), ip(ip), tcp(tcp), udp(udp), udp2(udp2), name(name) {}
};

// Email structure
struct email
{
    std::string from;
    std::string epochTime;
    std::string content;
    std::string id;
    email(){};
};

// Global variables
extern int PORT;
extern int DEBUG;
extern size_t READ_SIZE;
extern size_t FBUFFER_SIZE;
extern const int MAX_CLIENTS;
extern int mail_sock;
extern const int buffer_size;

extern volatile int client_socks[];
extern volatile int num_client;
extern volatile int shutting_down;

extern std::vector<Node> backend_socks;
extern std::string backendIP;
extern int backendPort;

extern int NOTFOUND;
extern int FORBIDDEN;

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
