// frontendserver.cc
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <iomanip>


#include "frontendserver.h"
#include "render.h"
#include "readingHelper.h"
#include "emailHelper.h"
#include "loginHelper.h"

using namespace std;

int PORT = 10000;
int DEBUG = 0;
size_t READ_SIZE = 5;
size_t FBUFFER_SIZE = 1024;
const int MAX_CLIENTS = 100;
int mail_sock;
const int buffer_size = 4096;

volatile int client_socks[MAX_CLIENTS];
volatile int num_client;
volatile int shutting_down = 0;

vector<Node> backend_socks;
string backendIP;
int backendPort;

int NOTFOUND = 404;
int FORBIDDEN = 403;

int REDIRECT = 0;
int LOGIN = 1;
int MENU = 2;
int MAILBOX = 3;
int DRIVE = 4;

int EMAIL = 6;
int SENDEMAIL = 7;
int FORWARD = 8;

int DOWNLOAD = 5;
int RENAME = 9;
int MOVE = 10;
int DELETE = 11;
int NEWDIR = 12;
int UPLOAD = 13;

int SIGNUP = 20;
int NEWPASS = 21;

/////////////////////////////////////
//								   //
//			 Heartbeat             //
//								   //
/////////////////////////////////////

const int BUF_SIZE = 1024;

struct sockaddr_in serverSock;
int udpsock;

void *handleHeartbeat(void *arg)
{
    char buffer[BUF_SIZE];
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    while (true)
    {
        int received = recvfrom(udpsock, buffer, BUF_SIZE, 0, (struct sockaddr *)&clientAddr, &clientAddrLen);
        if (received > 0)
        {
            buffer[received] = '\0';
            if (DEBUG)
            {
                std::cout << "Received heartbeat: " << buffer << std::endl;
            }
            if (string(buffer) == "DISABLE")
            {
                raise(SIGINT);
            }

            // Send a response integer back to the client
            int response_int = num_client;
            char response[1024];
            sprintf(response, "%d", response_int);
            sendto(udpsock, response, strlen(response), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
            if (DEBUG)
            {
                std::cout << "Sent response: " << response << std::endl;
            }
        }
    }
    return nullptr;
}

// signal handler for SIGINT
void signal_handler(int sig)
{
    if (sig == SIGINT)
    {
        shutting_down = 1;

        for (int i = 0; i < num_client; i++)
        {
            char msg[] = "-ERR Server shutting down\r\n";
            // set client socket to non-blocking
            fcntl(client_socks[i], F_SETFL, fcntl(client_socks[i], F_GETFL, 0) | O_NONBLOCK);
            // send shut-down message
            send(client_socks[i], msg, strlen(msg), 0);
            // close the socket
            close(client_socks[i]);
        }

        exit(0);
    }
}

// thread function for communication with clients
void *thread_worker(void *fd)
{
    printf("entered thread_worker\n");
    int sock = *(int *)fd;

    // This is the current client's number
    int currentClientNumber = num_client;

    // close immediately if shutting down
    if (shutting_down)
    {
        close(sock);
        pthread_exit(NULL);
    }

    char buffer[READ_SIZE];
    char *dataBuffer = (char *)malloc(1); // Allocate memory for at least 1 character
    dataBuffer[0] = '\0';                 // Null-terminate the string
    char *contentBuffer;
    size_t dataBufferSize = 0;
    // size_t content_read = 0;

    int read_header = 0;
    int read_body = 0;
    int contentlen = 0;
    string contentType = "";

    string sid = generate_cookie();
    printf("generated cookie\n");

    string tmp_sid = "";

    int reply_code = NOTFOUND;

    string username = "";
    int logged_in = 0;
    // email or file item name/identifier
    string item = "";

    // Try reading READ_SIZE bytes of data each time
    while (1)
    {

        int bytes_read = read(sock, buffer, READ_SIZE);

        // There are some data read
        if (bytes_read > 0)
        {

            dataBuffer = (char *)realloc(dataBuffer, dataBufferSize + bytes_read + 1);
            memcpy(dataBuffer + dataBufferSize, buffer, bytes_read);
            dataBufferSize += bytes_read;
            dataBuffer[dataBufferSize] = '\0';

            char *crlf;

            // if read_body, then read contentlen bytes
            if (read_body)
            {

                // continue reading more bytes
                if (dataBufferSize < contentlen)
                {
                    continue;
                }
                else
                {
                    char *content = (char *)malloc((contentlen + 1) * sizeof(char));
                    memcpy(content, dataBuffer, contentlen);
                    content[contentlen] = '\0';

                    // TODO uncomment if statement
                    //  process the message body
                    //  if (DEBUG)
                    //  {
                    //      // fprintf(stderr, "[%d] C: %s\n", sock, content);
                    //      // fprintf(stderr, "[%d] C: %ld\n", sock, dataBufferSize);
                    //      for (int c = 0; c < contentlen; c++)
                    //      {
                    //          char c_tmp[1];
                    //          strncpy(c_tmp, content + c, 1);
                    //          c_tmp[1] = '\0';
                    //          fprintf(stderr, "%s", c_tmp);

                    //         // break for message body that's too long
                    //         if (c >= 2048)
                    //         {
                    //             fprintf(stderr, "\n..............\n");
                    //             break;
                    //         }
                    //     }
                    //     fprintf(stderr, "\n");
                    // }

                    // request to get menu webpage
                    if (reply_code == MENU)
                    {
                        printf("request for menu\n");
                        tuple<string, string> credentials = parseLoginData(string(content));
                        printf("username is %s\n", get<0>(credentials).c_str());
                        username = get<0>(credentials);
                        printf("got username\n");
                        string password = get<1>(credentials);

                        DEBUG ? printf("Trying to connect to Backend\n") : 0;
                        connectToBackend(username, currentClientNumber);
                        // incorrect login credentials, log in again
                        if (authenticate(username, password, currentClientNumber) == 0)
                        {
                            reply_code = REDIRECT;
                            username = "";
                        }
                        else
                        {
                            logged_in = 1;
                        }
                    }

                    else if (reply_code == SIGNUP)
                    {

                        tuple<string, string> credentials = parseLoginData(string(content));
                        username = get<0>(credentials);
                        string password = get<1>(credentials);

                        connectToBackend(username, currentClientNumber);
                        string passwordEncoded = base64Encode(password);
                        DEBUG ? printf("ORIGINAL PASS = %s\n", password.c_str()) : 0;
                        DEBUG ? printf("ENCODED PASS = %s\n", passwordEncoded.c_str()) : 0;

                        // PUT username,password,passwordValue
                        string command = "PUT " + username + ",password," + passwordEncoded + "\r\n";
                        DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
                        sendToBackendSocket(currentClientNumber, command, username);

                        DEBUG ? printf("Sent command to server\n") : 0;
                        string response = readFromBackendSocket(currentClientNumber, username);
                        DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

                        reply_code = REDIRECT;
                        username = "";
                    }

                    else if (reply_code == NEWPASS)
                    {
                        DEBUG ? printf("IN NEWPASS \n") : 0;
                        map<string, string> msg_map = parseQuery(string(content));
                        string username = msg_map["username"];
                        string oldpass = msg_map["oldpass"];
                        string newpass = msg_map["newpass"];
                        DEBUG ? printf("OLD PASS %s\n", oldpass.c_str()) : 0;
                        DEBUG ? printf("NEW PASS %s\n", newpass.c_str()) : 0;
                        oldpass = urlEncode(oldpass);
                        newpass = urlEncode(newpass);
                        DEBUG ? printf("OLD PASS REGULAR %s\n", oldpass.c_str()) : 0;
                        DEBUG ? printf("NEW PASS REGULAR %s\n", newpass.c_str()) : 0;
                        oldpass = base64Encode(vector<char>(oldpass.begin(), oldpass.end()));
                        newpass = base64Encode(vector<char>(newpass.begin(), newpass.end()));
                        DEBUG ? printf("OLD PASS DECODED %s\n", oldpass.c_str()) : 0;
                        DEBUG ? printf("NEW PASS DECODED %s\n", newpass.c_str()) : 0;
                        cout << oldpass << endl;
                        cout << newpass << endl;
                        cout << to_string(oldpass == newpass) << endl;

                        connectToBackend(username, currentClientNumber);

                        // TODO store the username and password here
                        // CPUT username,password,oldPasswordValue,newPasswordValue
                        string command = "CPUT " + username + ",password," + oldpass + "," + newpass + "\r\n";
                        DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
                        sendToBackendSocket(currentClientNumber, command, username);

                        DEBUG ? printf("Sent command to server\n") : 0;
                        string response = readFromBackendSocket(currentClientNumber, username);
                        DEBUG ? printf("Response: %s \n", response.c_str()) : 0;
                    }
                    // send or reply email
                    else if (reply_code == SENDEMAIL)
                    {
                        map<string, string> msg_map = parseQuery(string(content));
                        string to = msg_map["to"];
                        string subject = msg_map["subject"];
                        string message = msg_map["message"];
                        if (DEBUG)
                        {
                            fprintf(stderr, "to: %s\nsubject: %s\nmessage: %s\n", to.c_str(), subject.c_str(), message.c_str());
                        }
                        // at this point we have the parts of the email
                        // send to mail the parts
                        mailMessage(username, to, subject, message);
                    }
                    // forward email
                    else if (reply_code == FORWARD)
                    {
                        printf("in forward\n");
                        map<string, string> msg_map = parseQuery(string(content));
                        string to = msg_map["to"];
                        string emailIdEndoded = msg_map["email_id"];
                        vector<char> emailIdVec = base64Decode(emailIdEndoded);
                        string emailId(emailIdVec.begin(), emailIdVec.end());
                        if (DEBUG)
                        {
                            fprintf(stderr, "to: %s\n", to.c_str());
                            fprintf(stderr, "email_id: %s\n", emailId.c_str());
                        }
                        // first query for this email
                        string command = "GET " + emailId + "\r\n";
                        sendToBackendSocket(currentClientNumber, command, username);
                        DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
                        string response = readFromBackendSocket(currentClientNumber, username);
                        DEBUG ? printf("Response: %s \n", response.c_str()) : 0;
                        // wow
                        string prefix = "+OK ";
                        response = response.substr(prefix.length());
                        vector<char> responseVec = base64Decode(response);
                        string responseDecoded(responseVec.begin(), responseVec.end());
                        DEBUG ? printf("Response decoded : %s \n", responseDecoded.c_str()) : 0;

                        std::string subject, message;
                        std::tie(subject, message) = extractSubjectAndMessage(responseDecoded);
                        DEBUG ? printf("subject is : |%s| message is |%s|\n", subject.c_str(), message.c_str()) : 0;
                        mailMessage(username, to, subject, message);
                    }

                    else if (reply_code == DELETE)
                    {
                        map<string, string> msg_map = parseQuery(string(content));
                        string fname = msg_map["fileName"];
                        string fpath = fname;
                        if (item.size() != 0)
                        {
                            fpath = item + "/" + fname;
                        }
                        if (DEBUG)
                        {
                            fprintf(stderr, "fname: %s\n", fname.c_str());
                        }
                        // Delete username,/content/bongo/spaceflare.jpg

                        string command = "LIST " + username + ",/content/" + fpath + "\r\n";
                        DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
                        sendToBackendSocket(currentClientNumber, command, username);
                        string response = readFromBackendSocket(currentClientNumber, username);
                        DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

                        std::istringstream iss(response);
                        std::string line;
                        while (std::getline(iss, line))
                        {
                            if (line.size() > 0)
                            {
                                command = "DELETE " + username + "," + line + "\r\n";
                                DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
                                sendToBackendSocket(currentClientNumber, command, username);
                                response = readFromBackendSocket(currentClientNumber, username);
                                DEBUG ? printf("Response: %s \n", response.c_str()) : 0;
                            }
                        }
                    }

                    else if (reply_code == RENAME)
                    {
                        map<string, string> msg_map = parseQuery(string(content));
                        string fname = msg_map["fileName"];
                        string new_fname = msg_map["newName"];
                        string fpath = fname;
                        string new_fpath = new_fname;
                        if (item.size() != 0)
                        {
                            fpath = item + "/" + fname;
                            new_fpath = item + "/" + new_fname;
                        }

                        if (DEBUG)
                        {
                            fprintf(stderr, "fname: %s\nnew_fname: %s\n", fname.c_str(), new_fname.c_str());
                        }
                        string command = "GET " + username + ",/content/" + fpath + "\r\n";
                        DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
                        sendToBackendSocket(currentClientNumber, command, username);
                        string response = readFromBackendSocket(currentClientNumber, username);
                        DEBUG ? printf("Response: %.100s \n", response.substr(0, 200).c_str()) : 0;

                        // remove +OK
                        string prefix = "+OK ";
                        response = response.substr(prefix.length());

                        command = "PUT " + username + ",/content/" + new_fpath + "," + response;
                        DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.substr(0, 200).c_str(), backend_socks[currentClientNumber].socket) : 0;
                        sendToBackendSocket(currentClientNumber, command, username);
                        response = readFromBackendSocket(currentClientNumber, username);
                        DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

                        // DELETe amuffin,/content/test-1-3.pdf
                        command = "DELETE " + username + ",/content/" + fpath + "\r\n";
                        DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
                        sendToBackendSocket(currentClientNumber, command, username);
                        response = readFromBackendSocket(currentClientNumber, username);
                        DEBUG ? printf("Response: %s \n", response.c_str()) : 0;
                    }

                    else if (reply_code == MOVE)
                    {
                        map<string, string> msg_map = parseQuery(string(content));
                        string fname = msg_map["fileName"];
                        string new_path = msg_map["newPath"];
                        string fpath = fname;
                        string new_fpath = new_path;
                        if (item.size() != 0)
                        {
                            fpath = item + "/" + fname;
                            new_fpath = new_path + "/" + fname;
                        }
                        if (DEBUG)
                        {
                            fprintf(stderr, "fname: %s\nnew_path: %s\n", fname.c_str(), new_path.c_str());
                            fprintf(stderr, "fpath: %s\nnew_fpath: %s\n", fpath.c_str(), new_fpath.c_str());
                        }
                        int isFolder = (fname.find('.') == std::string::npos);
                        if (isFolder)
                        {
                            // LIST cally,/content or LIST cally,/content/butterflyFour
                            string command = "LIST " + username + ",/content/" + fpath + "\r\n";
                            DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.substr(0, 200).c_str(), backend_socks[currentClientNumber].socket) : 0;
                            sendToBackendSocket(currentClientNumber, command, username);
                            string response = readFromBackendSocket(currentClientNumber, username);
                            DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

                            stringstream ss(response);
                            string line;
                            vector<string> lines;

                            while (getline(ss, line, '\n'))
                            {
                                lines.push_back(line);
                            }
                            // Process each line
                            for (const auto &line : lines)
                            {
                                if (line != "\r\n")
                                {
                                    // For each LIST item ( for ex : /content/butterflyFour/filename.txt )
                                    // GET cally,/content/butterflyFour/filename.txt
                                    // PUT cally,/content/newpath,valuethatweGot
                                    // DELETE cally,/content/butterflyFour/filename.txt
                                    string command = "GET " + username + ",/content/" + fpath + "\r\n";
                                    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.substr(0, 200).c_str(), backend_socks[currentClientNumber].socket) : 0;
                                    sendToBackendSocket(currentClientNumber, command, username);
                                    string response = readFromBackendSocket(currentClientNumber, username);
                                    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

                                    string prefix = "+OK ";
                                    response = response.substr(prefix.length());
                                    command = "PUT " + username + ",/content/" + new_fpath + "/" + fname + "," + response;
                                    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.substr(0, 200).c_str(), backend_socks[currentClientNumber].socket) : 0;
                                    sendToBackendSocket(currentClientNumber, command, username);
                                    response = readFromBackendSocket(currentClientNumber, username);
                                    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

                                    command = "DELETE " + username + ",/content/" + fpath + "\r\n";
                                    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.substr(0, 200).c_str(), backend_socks[currentClientNumber].socket) : 0;
                                    sendToBackendSocket(currentClientNumber, command, username);
                                    response = readFromBackendSocket(currentClientNumber, username);
                                    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;
                                }
                            }
                        }
                        else
                        {
                            // in this case it's file that we're trying to move
                            string command = "GET " + username + ",/content/" + fpath + "\r\n";
                            DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.substr(0, 200).c_str(), backend_socks[currentClientNumber].socket) : 0;
                            sendToBackendSocket(currentClientNumber, command, username);
                            string response = readFromBackendSocket(currentClientNumber, username);
                            DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

                            string prefix = "+OK ";
                            response = response.substr(prefix.length());
                            command = "PUT " + username + ",/content/" + new_fpath + "," + response;
                            DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.substr(0, 200).c_str(), backend_socks[currentClientNumber].socket) : 0;
                            sendToBackendSocket(currentClientNumber, command, username);
                            response = readFromBackendSocket(currentClientNumber, username);
                            DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

                            command = "DELETE " + username + ",/content/" + fpath + "\r\n";
                            DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.substr(0, 200).c_str(), backend_socks[currentClientNumber].socket) : 0;
                            sendToBackendSocket(currentClientNumber, command, username);
                            response = readFromBackendSocket(currentClientNumber, username);
                            DEBUG ? printf("Response: %s \n", response.c_str()) : 0;
                        }
                    }

                    else if (reply_code == NEWDIR)
                    {
                        map<string, string> msg_map = parseQuery(string(content));
                        string dirname = msg_map["folderName"];
                        string dirpath = item + "/" + dirname;
                        if (DEBUG)
                        {
                            fprintf(stderr, "dirname: %s\n", dirname.c_str());
                        }
                        string command = "PUT " + username + ",/content/" + dirpath + "," + "dummyData" + "\r\n";
                        if (item.size() == 0)
                        {
                            command = "PUT " + username + ",/content" + dirpath + "," + "dummyData" + "\r\n";
                        }
                        DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
                        sendToBackendSocket(currentClientNumber, command, username);
                        DEBUG ? printf("Sent command to server\n") : 0;
                        string response = readFromBackendSocket(currentClientNumber, username);
                    }

                    else if (reply_code == UPLOAD)
                    {
                        vector<char> content_vec;
                        content_vec.assign(content, content + contentlen);
                        auto msg_pair = parse_multipart_form_data(contentType, content_vec);
                        // this is binary data
                        vector<char> fdata = msg_pair.first;
                        string fdataString = base64Encode(fdata);
                        printf("the filedata is %s\n", fdataString.c_str());
                        // this is binary name
                        string fname = msg_pair.second;
                        string fpath = fname;
                        if (item.size() != 0)
                        {
                            fpath = item + "/" + fname;
                        }

                        string filePath = fname;
                        // PUT user,/content/<folderPath>, base64EncodedValueOfFile
                        printf(" the size of data is %ld\n", fdata.size());
                        string command = "PUT " + username + ",/content/" + fpath + "," + fdataString + "\r\n";
                        DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.substr(0, 100).c_str(), backend_socks[currentClientNumber].socket) : 0;
                        sendToBackendSocket(currentClientNumber, command, username);

                        DEBUG ? printf("Sent command to server\n") : 0;
                        string response = readFromBackendSocket(currentClientNumber, username);
                        // DEBUG ? printf("Response: %s \n", response.c_str()) : 0;
                        //  need to get complete path
                        contentType = "";
                    }

                    // forbidden access
                    if (reply_code != LOGIN && reply_code != SIGNUP && reply_code != NEWPASS && reply_code != REDIRECT && (logged_in != 1 || sid != tmp_sid))
                    {
                        reply_code = FORBIDDEN;
                    }

                    // send reply
                    if (reply_code == DOWNLOAD)
                    {
                        string contentStr(content);
                        printf("content is %s\n", content);
                        size_t pos = contentStr.find('=');
                        string filename = contentStr.substr(pos + 1);
                        string downloadLocation = "/home/cis5050/Downloads/" + contentStr.substr(pos + 1);
                        printf("filename is %s\n", filename.c_str());
                        string filepath = "";
                        if (item.size() == 0)
                        {
                            filepath = filename;
                        }
                        else
                        {
                            filepath = item + "/" + filename;
                        }
                        cout << "FILEPATH=" << filepath << endl;
                        string command = "GET " + username + ",/content/" + filepath + "\r\n";
                        DEBUG ? printf("Sending to frontend: %s\n", command.c_str()) : 0;

                        // NOTE: to send the actual binary data retrieved from the backend, use
                        sendToBackendSocket(currentClientNumber, command, username);
                        printf("sent to backend socket\n");
                        string response = readFromBackendSocket(currentClientNumber, username);
                        string prefix = "+OK ";
                        cout << "Response.size() -> " << response.size() << endl;
                        response = response.substr(prefix.length());
                        string responseDecoded = base64DecodeString(response);
                        send_file_data(sock, downloadLocation, responseDecoded.size(), &responseDecoded[0]);
                        // string downloadLocationTest = "/home/cis5050/Downloads/butterfly.jpg";
                        // send_file(sock , downloadLocationTest);
                    }

                    string reply_string = generateReply(reply_code, username, item, sid, currentClientNumber);
                    const char *reply = reply_string.c_str();

                    if (reply_code != DOWNLOAD)
                    {
                        send(sock, reply, strlen(reply), 0);
                        if (DEBUG)
                        {
                            // TODO Need to uncomment
                            // fprintf(stderr, "[%d] S: %s\n", sock, reply);
                        }
                    }

                    // clear the buffer
                    memmove(dataBuffer, dataBuffer + contentlen, dataBufferSize - contentlen + 1);
                    dataBuffer = (char *)realloc(dataBuffer, dataBufferSize - contentlen + 1);
                    dataBufferSize -= contentlen;

                    read_body = 0;
                    contentlen = 0;
                    // content_read = 0;
                    free(content);
                }
                continue;
            }

            // look for \r\n in the data buffer
            while ((crlf = strstr(dataBuffer, "\r\n")) != NULL)
            {
                // command length excluding "/r/n"
                size_t cmdLen = crlf - dataBuffer;
                char cmd[cmdLen + 1];
                strncpy(cmd, dataBuffer, cmdLen);
                cmd[cmdLen] = '\0';
                if (DEBUG)
                {
                    // TODO : Need to uncomment 5/8
                    // fprintf(stderr, "[%d] C: %s\n", sock, cmd);
                }

                // Reading Header lines
                if (read_header)
                {

                    // there is a message body
                    if (strncmp(cmd, "Content-Length: ", strlen("Content-Length: ")) == 0)
                    {
                        int contentlen_len = strlen(cmd) - strlen("Content-Length: ");
                        char contentlen_str[contentlen_len];
                        strncpy(contentlen_str, cmd + strlen("Content-Length: "), contentlen_len);
                        contentlen_str[contentlen_len] = '\0';
                        contentlen = atoi(contentlen_str);
                    }

                    // record content type for multi-part form data
                    if (strncmp(cmd, "Content-Type: ", strlen("Content-Type: ")) == 0)
                    {
                        int contentType_len = strlen(cmd);
                        char contentType_str[contentType_len];
                        strncpy(contentType_str, cmd, contentType_len);
                        contentType_str[contentType_len] = '\0';
                        contentType = string(contentType_str);
                    }

                    // parse cookie
                    else if (strncmp(cmd, "Cookie: ", strlen("Cookie: ")) == 0)
                    {
                        int sid_len = strlen(cmd) - strlen("Cookie: sid=");
                        char sid_str[sid_len];
                        strncpy(sid_str, cmd + strlen("Cookie: sid="), sid_len);
                        sid_str[sid_len] = '\0';
                        tmp_sid = string(sid_str);
                    }

                    // headers end
                    else if (strcmp(cmd, "") == 0)
                    {

                        read_header = 0;
                        // prepare to read the message body
                        if (contentlen > 0)
                        {
                            contentBuffer = (char *)malloc((contentlen + 1) * sizeof(char));
                            read_body = 1;
                        }

                        // no message body
                        else
                        {
                            // forbidden access

                            if (reply_code != LOGIN && reply_code != SIGNUP && reply_code != NEWPASS && (logged_in != 1 || sid != tmp_sid))
                            {
                                reply_code = FORBIDDEN;
                            }

                            // send reply
                            string reply_string = generateReply(reply_code, username, item, sid, currentClientNumber);
                            const char *reply = reply_string.c_str();

                            send(sock, reply, strlen(reply), 0);
                            if (DEBUG)
                            {
                                // TODO need to uncomment
                                // fprintf(stderr, "[%d] S: %s\n", sock, reply);
                            }
                        }
                    }
                }

                // Process GET command
                else if (strncmp(cmd, "GET ", 4) == 0)
                {

                    // parse request url
                    char tmp[strlen(cmd)];
                    strncpy(tmp, cmd, strlen(cmd));
                    tmp[strlen(cmd)] = '\0';
                    strtok(tmp, " ");
                    char *url = strtok(NULL, " ");

                    // login page
                    if (strcmp(url, "/") == 0)
                    {
                        reply_code = LOGIN;
                    }
                    // mailbox page
                    else if (strcmp(url, "/mailbox") == 0)
                    {
                        reply_code = MAILBOX;
                    }

                    // drive page
                    else if (strncmp(url, "/drive", strlen("/drive")) == 0)
                    {
                        reply_code = DRIVE;
                        item = extractPath(string(url));
                    }

                    // email content page
                    else if (strstr(url, "/mailbox") != NULL)
                    {
                        char *pos = strstr(url, "/mailbox");
                        char *fname_ptr = pos + strlen("/mailbox/");
                        item = string(fname_ptr);
                        reply_code = EMAIL;
                    }

                    // page not found
                    else
                    {
                        reply_code = NOTFOUND;
                    }

                    // start reading headers
                    read_header = 1;
                }

                // Process POST command
                else if (strncmp(cmd, "POST ", 5) == 0)
                {

                    // parse request url
                    char tmp[strlen(cmd)];
                    strncpy(tmp, cmd, strlen(cmd));
                    tmp[strlen(cmd)] = '\0';
                    strtok(tmp, " ");
                    char *url = strtok(NULL, " ");

                    if (strcmp(url, "/signup") == 0)
                    {
                        reply_code = SIGNUP;
                    }

                    else if (strcmp(url, "/newpass") == 0)
                    {
                        reply_code = NEWPASS;
                    }

                    // redirect to menu page
                    else if (strcmp(url, "/menu") == 0)
                    {
                        reply_code = MENU;
                    }

                    else if (strcmp(url, "/send-email") == 0)
                    {
                        reply_code = SENDEMAIL;
                    }

                    else if (strcmp(url, "/forward-email") == 0)
                    {
                        reply_code = FORWARD;
                    }

                    else if (strcmp(url, "/download") == 0)
                    {
                        reply_code = DOWNLOAD;
                    }

                    else if (strcmp(url, "/rename") == 0)
                    {
                        reply_code = RENAME;
                    }

                    else if (strcmp(url, "/move") == 0)
                    {
                        reply_code = MOVE;
                    }

                    else if (strcmp(url, "/delete") == 0)
                    {
                        reply_code = DELETE;
                    }

                    else if (strcmp(url, "/create-folder") == 0)
                    {
                        reply_code = NEWDIR;
                    }

                    else if (strcmp(url, "/upload-file") == 0)
                    {
                        reply_code = UPLOAD;
                    }

                    // page not found
                    else
                    {
                        reply_code = NOTFOUND;
                    }

                    // start reading headers
                    read_header = 1;
                }

                // Process Unknown command
                else
                {
                    char unkCmd[] = "HTTP/1.1 501 Not Implemented\r\n";
                    send(sock, unkCmd, strlen(unkCmd), 0);
                    if (DEBUG)
                    {
                        // TODO need to uncomment
                        // fprintf(stderr, "[%d] S: %s", sock, unkCmd);
                    }
                }

                // Remove the processed command
                memmove(dataBuffer, dataBuffer + cmdLen + 2, dataBufferSize - cmdLen - 1);
                dataBuffer = (char *)realloc(dataBuffer, dataBufferSize - cmdLen - 1);
                dataBufferSize -= cmdLen + 2;
            }
        }
        else
        {
            // client exit
            free(dataBuffer);
            close(sock);
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (client_socks[i] == sock)
                {
                    client_socks[i] = 0;
                    break;
                }
            }
            if (DEBUG)
            {
                // TODO need to uncomment
                // fprintf(stderr, "[%d] Connection closed\n", sock);
            }
            num_client -= 1;
            pthread_exit(NULL);
        }
    }
}

/////////////////////////////////////
//                                 //
//              Main               //
//                                 //
/////////////////////////////////////

int main(int argc, char *argv[])
{
    // signal handling
    signal(SIGINT, signal_handler);

    // parse arguments
    int opt;

    while ((opt = getopt(argc, argv, "vap:")) != -1)
    {
        switch (opt)
        {
        case 'p':
            PORT = atoi(optarg);
            break;
        case 'a':
            fprintf(stderr, "Zihao Deng / zihaoden\n");
            exit(1);
        case 'v':
            DEBUG = 1;
            break;
        }
    }
    // Initialize client sockets
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        client_socks[i] = 0;
    }

    // Initialize backendsockets
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        backend_socks.push_back(Node());
    }

    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    // bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    int sockopt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &sockopt, sizeof(sockopt));
    bind(listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    listen(listen_fd, 10);

    printf("about to connect to mailSock\n");
    // set mailSocket
    mail_sock = connectToMail();
    printf("connected to mailSock\n");

    //////////////////////
    // Heartbeat thread //
    //////////////////////

    udpsock = socket(AF_INET, SOCK_DGRAM, 0);

    memset((char *)&serverSock, 0, sizeof(serverSock));
    serverSock.sin_family = AF_INET;
    serverSock.sin_port = htons(PORT + 10000);
    serverSock.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(udpsock, (struct sockaddr *)&serverSock, sizeof(serverSock)) < 0)
    {
        std::cerr << "Error binding socket" << std::endl;
        return 1;
    }

    pthread_t threadId;
    pthread_create(&threadId, nullptr, handleHeartbeat, nullptr);

    ///////////////
    // Main loop //
    ///////////////

    while (1)
    {
        if (num_client >= MAX_CLIENTS)
        {
            continue;
        }
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen = sizeof(clientaddr);
        int *fd = (int *)malloc(sizeof(int));
        *fd = accept(listen_fd, (struct sockaddr *)&clientaddr, &clientaddrlen);
        // printf("Connection from %s\n", inet_ntoa(clientaddr.sin_addr));

        if (DEBUG)
        {
            // TODO need to uncomment
            // fprintf(stderr, "[%d] New Connection\n", *fd);
        }

        // record socket fd in the sockets array
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (client_socks[i] == 0)
            {
                client_socks[i] = *fd;
                pthread_t thread;
                pthread_create(&thread, NULL, thread_worker, fd);
                break;
            }
        }
        num_client += 1;
    }

    exit(0);
}