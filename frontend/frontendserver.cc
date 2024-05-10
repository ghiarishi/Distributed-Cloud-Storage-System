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

int ADMIN = 14;
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

/////////////////////////////////////
//                                 //
//        Extract Methods                //
//                                 //
/////////////////////////////////////

std::tuple<std::string, std::string> extractSubjectAndMessage(const std::string &email)
{
    std::string subject, message;

    // Find the position of "Subject: "
    size_t subjectPos = email.find("Subject: ");
    if (subjectPos != std::string::npos)
    {
        // Extract the subject starting from the position after "Subject: "
        subject = email.substr(subjectPos + 9); // 9 is the length of "Subject: "

        // Find the position of the next occurrence of "\n" after the subject
        size_t newlinePos = subject.find("\n");
        if (newlinePos != std::string::npos)
        {
            // Extract the message starting from the position after the newline
            message = subject.substr(newlinePos + 1);
            // Remove the trailing newline if present
            if (!message.empty() && message.back() == '\n')
            {
                message.pop_back();
            }
            // Trim any leading whitespace from the message
            message.erase(0, message.find_first_not_of(" \t\n\r\f\v"));
        }
    }

    return std::make_tuple(subject, message);
}

// extract path after /drive/
string extractPath(const string &path)
{
    string key = "drive/";
    size_t pos = path.find(key);

    if (pos != string::npos)
    {
        return path.substr(pos + key.length());
    }
    else
    {
        // Return an empty string if "/drive/" is not found
        return "";
    }
}

// Get filename from the path
string getFileName(const string &path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos)
        return path.substr(pos + 1);
    return path;
}

int countOccurrences(const std::string &str, char target)
{
    int count = 0;
    for (char c : str)
    {
        if (c == target)
        {
            count++;
        }
    }
    return count;
}

vector<pair<string, int>> extractFiles(string username, string returnString, string directoryPath)
{
    vector<string> paths;
    istringstream iss(returnString);
    string line;
    vector<pair<string, int>> files;

    while (std::getline(iss, line))
    {
        // Assuming "/content/" is fixed part of the path
        size_t startPos = line.find("/content/");
        if (startPos != std::string::npos)
        {
            // Extract path starting from "/content/"
            std::string path = line.substr(startPos + 9);
            printf(" The path is |%s| directoryPath is |%s|\n", path.c_str(), directoryPath.c_str());
            startPos = path.find(directoryPath);
            string remainingText = path.substr(startPos + directoryPath.size());
            printf("The remainingText is %s\n", remainingText.c_str());
            if (directoryPath.size() == 0 && remainingText.find("/") == std::string::npos)
            {
                printf("we are adding path\n");
                paths.push_back(path);
            }
            else if (directoryPath.size() != 0 && remainingText.find("/") != std::string::npos &&
                     remainingText.find("/", remainingText.find("/") + 1) == std::string::npos)
            {
                printf("we are adding path\n");
                paths.push_back(path);
            }
        }
    }

    for (string currPath : paths)
    {
        string fileName = getFileName(currPath);
        int isFolder = (currPath.find('.') == std::string::npos);
        pair<string, int> filePair = make_pair(fileName, isFolder);
        files.push_back(filePair);
    }
    return files;
}

vector<email> extractEmails(string username, string returnString)
{
    vector<email> emails;
    istringstream iss(returnString);
    string line;

    printf(" the return string is %s\n", returnString.c_str());
    while (getline(iss, line))
    {
        if (line.empty())
            continue; // Skip empty lines
        if (line.substr(0, 8) != "/emails/")
            continue; // Skip lines that do not start with '/emails/'

        email e;

        // Find positions of separators
        size_t pos1 = line.find('/', 8);
        size_t pos2 = line.find(',', pos1 + 1);

        // Extract sender, epochTime, and content
        e.from = line.substr(8, pos1 - 8);
        e.epochTime = line.substr(pos1 + 1, pos2 - pos1 - 1);
        e.id = username + ",/" + line.substr(pos2 + 2, line.size() - pos2 - 2); // Adjust indices to skip ", and "
        printf("the ID is %s\n", e.id.c_str());

        // Add email to the vector
        emails.push_back(e);
    }

    return emails;
}

string extractPassword(string returnString)
{
    // Find the position of the first space after "+OK "
    size_t startPos = 4; // Length of "+OK "
    size_t endPos = returnString.find("\r\n", startPos);

    // Extract the substring between "+OK " and "\r\n"
    string password = returnString.substr(startPos, endPos - startPos);
    return password;
}

pair<string, int> extractIPAndPort(const string &serverInfo)
{
    pair<std::string, int> result;

    size_t ipStart = serverInfo.find(":") + 1;                  // Find the start of the IP address
    size_t ipEnd = serverInfo.find(":", ipStart);               // Find the end of the IP address
    result.first = serverInfo.substr(ipStart, ipEnd - ipStart); // Extract the IP address

    size_t portStart = ipEnd + 1;                                                 // Find the start of the port number
    size_t portEnd = serverInfo.find("\r\n", portStart);                          // Find the end of the port number
    result.second = std::stoi(serverInfo.substr(portStart, portEnd - portStart)); // Extract the port number

    return result;
}

std::string urlEncode(const std::string &value)
{
    std::ostringstream encodedStream;
    encodedStream << std::hex << std::uppercase;

    for (char ch : value)
    {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~')
        {
            encodedStream << ch;
        }
        else
        {
            encodedStream << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(ch));
        }
    }

    return encodedStream.str();
}


/////////////////////////////////////
//                                 //
//           Utilities             //
//                                 //
/////////////////////////////////////

vector<char> base64Decode(const string &encoded_data)
{
    // Create a BIO chain for Base64 decoding
    BIO *bio = BIO_new_mem_buf(encoded_data.data(), encoded_data.length());
    BIO *base64 = BIO_new(BIO_f_base64());
    BIO_set_flags(base64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(base64, bio);

    // Prepare to read the decoded data
    vector<char> decoded_data(encoded_data.length()); // Allocate enough space
    int decoded_length = BIO_read(bio, decoded_data.data(), decoded_data.size());

    if (decoded_length < 0)
    {
        // Handle the case where decoding fails
        cerr << "Error decoding Base64 string." << endl;
        decoded_data.clear();
    }
    else
    {
        // Resize the vector to the actual decoded length
        decoded_data.resize(decoded_length);
    }

    // Clean up
    BIO_free_all(bio);

    return decoded_data;
}

string base64DecodeString(const string &encoded_data)
{
    // Create a BIO chain for Base64 decoding
    BIO *bio = BIO_new_mem_buf(encoded_data.data(), encoded_data.length());
    BIO *base64 = BIO_new(BIO_f_base64());
    BIO_set_flags(base64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(base64, bio);

    // Prepare to read the decoded data
    vector<char> decoded_data(encoded_data.length()); // Allocate enough space
    int decoded_length = BIO_read(bio, decoded_data.data(), decoded_data.size());

    if (decoded_length < 0)
    {
        // Handle the case where decoding fails
        cerr << "Error decoding Base64 string." << endl;
        decoded_data.clear();
    }
    else
    {
        // Resize the vector to the actual decoded length
        decoded_data.resize(decoded_length);
    }

    // Clean up
    BIO_free_all(bio);

    string stringDecodedData(decoded_data.begin(), decoded_data.end());
    return stringDecodedData;
}

string base64Encode(const vector<char> &data)
{
    // Create a BIO object for base64 encoding
    BIO *bio = BIO_new(BIO_s_mem());
    BIO *base64 = BIO_new(BIO_f_base64());
    BIO_set_flags(base64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(base64, bio);

    // Write data to BIO
    BIO_write(bio, data.data(), data.size());
    BIO_flush(bio);

    // Get the encoded data
    BUF_MEM *bio_buf;
    BIO_get_mem_ptr(bio, &bio_buf);

    // Convert the encoded data to a std::string
    std::string encoded_data(bio_buf->data, bio_buf->length);

    // Clean up
    BIO_free_all(bio);

    return encoded_data;
}

string base64Encode(const string &dataString)
{
    vector<char> data(dataString.begin(), dataString.end());
    // Create a BIO object for base64 encoding
    BIO *bio = BIO_new(BIO_s_mem());
    BIO *base64 = BIO_new(BIO_f_base64());
    BIO_set_flags(base64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(base64, bio);

    // Write data to BIO
    BIO_write(bio, data.data(), data.size());
    BIO_flush(bio);

    // Get the encoded data
    BUF_MEM *bio_buf;
    BIO_get_mem_ptr(bio, &bio_buf);

    // Convert the encoded data to a std::string
    std::string encoded_data(bio_buf->data, bio_buf->length);

    // Clean up
    BIO_free_all(bio);

    return encoded_data;
}

// get backendNodes from the fileName
vector<Node> getBackendNodes(string filename)
{
    vector<Node> nodes;
    ifstream file(filename);
    if (!file.is_open())
    {
        cerr << "Error opening file: " << filename << endl;
        return nodes; // return an empty vector if file couldn't be opened
    }

    string line;
    while (getline(file, line))
    {
        stringstream ss(line);
        string token;

        // Parse each line
        int id, tcp, udp, udp2;
        string ip;
        getline(ss, token, ','); // id
        id = stoi(token.substr(token.find(":") + 1));
        getline(ss, token, ','); // ip
        ip = token.substr(token.find(":") + 1);
        getline(ss, token, ','); // tcp
        tcp = stoi(token.substr(token.find(":") + 1));
        getline(ss, token, ','); // udp
        udp = stoi(token.substr(token.find(":") + 1));
        getline(ss, token, ','); // udp2
        udp2 = stoi(token.substr(token.find(":") + 1));

        // Construct Node object and push it to the vector
        nodes.emplace_back(id, ip, tcp, udp, udp2, line);
        printf("The whole line is %s\n", line.c_str());
    }

    file.close();
    return nodes;
}
// Parse login data
tuple<string, string> parseLoginData(string data_str)
{
    std::istringstream iss(data_str);
    std::string pair;
    std::string username, password;

    while (std::getline(iss, pair, '&'))
    {
        std::istringstream issPair(pair);
        std::string key, value;
        std::getline(issPair, key, '=');
        std::getline(issPair, value, '=');

        if (key == "username")
        {
            username = value;
        }
        else if (key == "password")
        {
            password = value;
        }
    }

    return make_tuple(username, password);
}

string decodeURIComponent(const string &s)
{
    string result;
    for (size_t i = 0; i < s.length(); ++i)
    {
        if (s[i] == '%')
        {
            int val;
            istringstream is(s.substr(i + 1, 2));
            if (is >> std::hex >> val)
            {
                result += static_cast<char>(val);
                i += 2;
            }
        }
        else if (s[i] == '+')
        {
            result += ' ';
        }
        else
        {
            result += s[i];
        }
    }
    return result;
}

map<string, string> parseQuery(const string &query)
{
    map<string, string> data;
    istringstream paramStream(query);
    string pair;

    while (getline(paramStream, pair, '&'))
    {
        size_t eq = pair.find('=');
        string key = pair.substr(0, eq);
        string value = pair.substr(eq + 1);
        data[decodeURIComponent(key)] = decodeURIComponent(value);
    }

    return data;
}

// Helper function to split binary data by delimiter
std::vector<std::vector<char>> split(const std::vector<char> &s, const std::string &delimiter)
{
    std::vector<std::vector<char>> parts;
    auto it = s.begin();
    while (it != s.end())
    {
        auto pos = std::search(it, s.end(), delimiter.begin(), delimiter.end());
        if (pos != s.end())
        {
            parts.emplace_back(it, pos);
            it = pos + delimiter.size();
        }
        else
        {
            parts.emplace_back(it, s.end());
            break;
        }
    }
    return parts;
}

// Extracts the boundary from the Content-Type header
std::string extract_boundary(const std::string &contentType)
{
    size_t pos = contentType.find("boundary=");
    if (pos == std::string::npos)
        return "";
    std::string boundary = contentType.substr(pos + 9);
    if (boundary.front() == '"')
    {
        boundary.erase(0, 1);                // Remove the first quote
        boundary.erase(boundary.size() - 1); // Remove the last quote
    }
    return boundary;
}

// Parses the multipart/form-data content and returns file content and filename
std::pair<std::vector<char>, std::string> parse_multipart_form_data(const string &contentType, const vector<char> &body)
{
    std::string boundary = extract_boundary(contentType);
    std::string delimiter = "--" + boundary + "\r\n";
    std::string endDelimiter = "--" + boundary + "--";
    std::vector<char> fileContent;
    std::string filename;

    std::vector<std::vector<char>> parts = split(body, delimiter);

    for (const auto &part : parts)
    {
        if (part.empty() || std::equal(part.begin(), part.end(), endDelimiter.begin(), endDelimiter.end()))
        {
            continue;
        }

        auto headerEndPos = std::search(part.begin(), part.end(), std::begin("\r\n\r\n"), std::end("\r\n\r\n") - 1);
        if (headerEndPos == part.end())
        {
            continue; // Skip if there's no header
        }

        std::string headers(part.begin(), headerEndPos);
        std::vector<char> content(headerEndPos + 4, part.end() - 2); // Remove last \r\n

        if (headers.find("filename=") != std::string::npos)
        {
            size_t namePos = headers.find("name=\"");
            size_t nameEndPos = headers.find("\"", namePos + 6);
            std::string fieldName = headers.substr(namePos + 6, nameEndPos - (namePos + 6));

            size_t filenamePos = headers.find("filename=\"");
            size_t filenameEndPos = headers.find("\"", filenamePos + 10);
            filename = headers.substr(filenamePos + 10, filenameEndPos - (filenamePos + 10));

            fileContent = std::move(content);
            break; // Assuming only one file per upload for simplicity
        }
    }

    return {fileContent, filename};
}

void send_chunk(int client_socket, const vector<char> &data)
{
    if (data.empty())
        return;
    stringstream chunk_size;
    chunk_size << hex << data.size(); // Convert size to hex
    string size_hex = chunk_size.str();

    send(client_socket, size_hex.c_str(), size_hex.size(), 0);
    send(client_socket, "\r\n", 2, 0);
    send(client_socket, data.data(), data.size(), 0);
    send(client_socket, "\r\n", 2, 0);
}

void send_file(int client_socket, const string &file_path)
{
    ifstream file(file_path, ios::binary | ios::ate);

    auto file_size = file.tellg();
    file.seekg(0, ios::beg);

    stringstream header;
    header << "HTTP/1.1 200 OK\r\n";
    header << "Content-Type: application/octet-stream\r\n";

    string file_name = getFileName(file_path);

    header << "Content-Length: " << file_size << "\r\n";
    header << "Content-Disposition: attachment; filename=\"" << file_name << "\"\r\n";
    header << "Connection : keep-alive\r\n";
    header << "Connection : keep-alive\r\n";
    header << "\r\n";

    send(client_socket, header.str().c_str(), header.str().size(), 0);
    if (DEBUG)
    {
        fprintf(stderr, "[%d] S: %s\n", client_socket, header.str().c_str());
    }

    char buffer[FBUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
    {
        send(client_socket, buffer, file.gcount(), 0);
    }

    if (DEBUG)
    {
        fprintf(stderr, "[%d] S: file sent for downloading\n", client_socket);
    }

    file.close();
}

// send file data to the client. file_path is the path in the backend (used for getting filename).
void send_file_data(int client_socket, string file_path, int file_size, char *data)
{

    stringstream header;
    header << "HTTP/1.1 200 OK\r\n";
    header << "Content-Type: application/octet-stream\r\n";

    string file_name = getFileName(file_path);

    header << "Content-Length: " << file_size << "\r\n";
    header << "Content-Disposition: attachment; filename=\"" << file_name << "\"\r\n";
    header << "Connection : keep-alive\r\n";
    header << "\r\n";

    send(client_socket, header.str().c_str(), header.str().size(), 0);
    if (DEBUG)
    {
        fprintf(stderr, "[%d] S: %s\n", client_socket, header.str().c_str());
    }

    char buffer[FBUFFER_SIZE];
    send(client_socket, data, file_size, 0);
    if (DEBUG)
    {
        fprintf(stderr, "[%d] S: file sent for downloading\n", client_socket);
    }
}

string generate_cookie()
{
    stringstream ss;
    time_t now = time(nullptr);
    ss << now;
    return ss.str();
}

/////////////////////////////////////
//                                 //
//       Backend Interaction       //
//                                 //
/////////////////////////////////////

int connectToBackend(string username, int clientNum)
{
    int master_sock, backend_sock;
    struct sockaddr_in server_addr;

    // Open master socket
    if ((master_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        std::cerr << "Error creating socket" << std::endl;
        return -1;
    }

    // Server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(2000);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to server
    if (connect(master_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "Error connecting to server" << std::endl;
        return -1;
    }

    DEBUG ? printf("Connected to Server\n") : 0;

    // Send command to server
    string command = "GET_SERVER:" + username + "\r\n";
    if (send(master_sock, command.c_str(), command.length(), 0) < 0)
    {
        std::cerr << "Error sending command to server" << std::endl;
        return -1;
    }

    DEBUG ? printf("Sent command to Server\n") : 0;

    // Receive server info
    char serverInfo[buffer_size];
    if (recv(master_sock, serverInfo, sizeof(serverInfo), 0) < 0)
    {
        std::cerr << "Error receiving response from server" << std::endl;
        return -1;
    }

    DEBUG ? printf("Received response %s\n", serverInfo) : 0;
    auto ipAndPort = extractIPAndPort(serverInfo);

    // Connect to backend server
    if ((backend_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        std::cerr << "Error creating socket" << std::endl;
        return -1;
    }

    server_addr.sin_addr.s_addr = inet_addr(ipAndPort.first.c_str());
    server_addr.sin_port = htons(ipAndPort.second);

    if (connect(backend_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "Error connecting to backend sock" << std::endl;
        return -1;
    }

    char buffer[4096];
    if (recv(backend_sock, buffer, sizeof(buffer), 0) < 0)
    {
        std::cerr << "Error receiving response from server" << std::endl;
        return -1;
    }

    printf("Response: %s\n", buffer);

    backend_socks[clientNum].ip = ipAndPort.first;
    backend_socks[clientNum].port = ipAndPort.second;
    backend_socks[clientNum].socket = backend_sock;
    return 0;
}

// Helper function to send data to backend server
bool sendToBackendSocket(int clientNumber, string command, string username)
{
    int backend_sock = backend_socks[clientNumber].socket;
    if (send(backend_sock, command.c_str(), command.length(), 0) < 0)
    {
        cerr << "Error sending data to backend server" << std::endl;
        if (!connectToBackend(username, clientNumber))
        {
            cerr << "Failed to reconnect to backend server" << std::endl;
            return false;
        }
        // Retry sending after successful reconnection
        if (send(backend_sock, command.c_str(), command.length(), 0) < 0)
        {
            cerr << "Error sending data after reconnection" << std::endl;
            return false;
        }
    }
    return true;
}

bool sendToSocket(int backend_sock, string command)
{
    if (send(backend_sock, command.c_str(), command.length(), 0) < 0)
    {
        cerr << "Error sending data to backend server" << std::endl;
        return false;
    }
    return true;
}

// Helper function to read from backend socket
string readFromBackendSocket(int clientNumber, string username)
{
    string response;
    char buffer[4096];
    int backend_sock = backend_socks[clientNumber].socket;

    while (true)
    {
        memset(buffer, 0, sizeof(buffer));

        int bytesReceived = recv(backend_sock, buffer, sizeof(buffer), 0);
        if (bytesReceived < 0)
        {
            cerr << "Error receiving response from server" << std::endl;
            if (!connectToBackend(username, clientNumber))
            {
                cerr << "Failed to reconnect to backend server" << std::endl;
                return "";
            }
            continue; // Retry reading after successful reconnection
        }

        response.append(buffer, bytesReceived);

        size_t found = response.find("\r\n");
        if (found != std::string::npos)
        {
            break;
        }
    }

    return response;
}

string readFromSocket(int backend_sock)
{
    string response;
    char buffer[4096];

    while (true)
    {
        memset(buffer, 0, sizeof(buffer));

        int bytesReceived = recv(backend_sock, buffer, sizeof(buffer), 0);
        if (bytesReceived < 0)
        {
            cerr << "Error receiving response from server" << std::endl;
            return "";
        }
        // printf("the buffer is %s\n", buffer);

        response.append(buffer, bytesReceived);

        // Check if "\r\n" is present in the received data
        size_t found = response.find("\r\n");
        if (found != std::string::npos)
        {
            break; // Exit loop if "\r\n" is found
        }
    }

    return response;
}

void mailMessage(string username, string to, string subject, string message)
{
    // CHANGES!!!----
    size_t posTo = to.find('@');
    size_t posFrom = to.find('@');
    string domain = to.substr(posTo + 1);
    string mailFrom = "MAIL FROM:<" + username + "@" + domain + ">\r\n";
    sendToSocket(mail_sock, mailFrom);
    string response = readFromSocket(mail_sock);
    DEBUG ? printf("Response is %s\n", response.c_str()) : 0;
    // if ( !containsSubstring(response , "250 OK")){
    //     break;
    // }
    // now we need to say who its to
    string mailTo = "RCPT TO:<" + to + ">\r\n";
    sendToSocket(mail_sock, mailTo);
    // even if its non existent thats ok
    response = readFromSocket(mail_sock);
    // now send data message
    sendToSocket(mail_sock, "DATA\r\n");
    response = readFromSocket(mail_sock);
    // now send data
    string fromEmail = username + "@" + domain;
    string toEmail = to;
    string messageData = "From: <" + fromEmail + ">\r\nTo: <" + toEmail + ">\r\nSubject: " + subject + "\r\n" + message + "\r\n\r\n.\r\n";
    sendToSocket(mail_sock, messageData);
    response = readFromSocket(mail_sock);
    //-----
}

void deleteEmail(string username, string item, int currentClientNumber)
{
    printf("in deleteEmail \n");
    printf("item is %s \n", item.c_str());

    // wow
    //get everything post delete
    string prefix = "delete/";
    size_t pos = item.find(prefix);
    string result = item.substr(pos + prefix.length());
    string command = "DELETE " + result + "\r\n";
    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
    sendToBackendSocket(currentClientNumber, command, username);
    string response = readFromBackendSocket(currentClientNumber, username);
    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;
}
int connectToMail()
{
    int mail_sock_temp;
    struct sockaddr_in server_addr;

    // Open master socket
    if ((mail_sock_temp = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        std::cerr << "Error creating socket" << std::endl;
        return -1;
    }

    // Server address
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(2500);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    inet_pton(AF_INET, "127.0.0.1", &(server_addr.sin_addr));

    // Connect to server
    if (connect(mail_sock_temp, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "Error connecting to server" << std::endl;
        return -1;
    }

    DEBUG ? printf("Connected to Server\n") : 0;

    // Send command to server
    string command = "HELO tester\r\n";
    if (send(mail_sock_temp, command.c_str(), command.length(), 0) < 0)
    {
        std::cerr << "Error sending command to server" << std::endl;
        return -1;
    }

    DEBUG ? printf("Sent command to Server\n") : 0;

    // Receive server info
    char serverInfo[1024];
    if (recv(mail_sock_temp, serverInfo, sizeof(serverInfo), 0) < 0)
    {
        std::cerr << "Error receiving response from server" << std::endl;
        return -1;
    }

    DEBUG ? printf("Received response |%s|\n", serverInfo) : 0;
    string serverInfoString(serverInfo);
    if (serverInfoString.find("220 localhost service ready") == std::string::npos)
    {
        std::cerr << "Recived error response from server" << std::endl;
        return -1;
    }

    return mail_sock_temp;
}

// login verification
int authenticate(string username, string password, int currentClient)
{
    string command = "GET " + username + ",password\r\n";
    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClient].socket) : 0;
    sendToBackendSocket(currentClient, command, username);
    DEBUG ? printf("Sent command to server\n") : 0;
    string response = readFromBackendSocket(currentClient, username);
    string rightPasswordEncoded = extractPassword(response);
    string decodedPassword = base64DecodeString(rightPasswordEncoded);
    DEBUG ? printf("Response: %s decoded correct password is: |%s| and user entered: |%s|\n", response.c_str(), decodedPassword.c_str(), password.c_str()) : 0;

    if (decodedPassword.size() == 0)
    {
        return 0;
    }
    return password == decodedPassword ? 1 : 0;
}

// retrieve emails in mailbox
vector<email> get_mailbox(string username, int currentClientNumber)
{
    string command = "LIST " + username + ",/emails\r\n";
    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
    sendToBackendSocket(currentClientNumber, command, username);

    string response = readFromBackendSocket(currentClientNumber, username);
    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

    vector<email> emails = extractEmails(username, response);

    return emails;
}

string getEmailContent(string emailID, int currentClientNumber, string username)
{
    string command = "GET " + emailID + "\r\n";
    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
    sendToBackendSocket(currentClientNumber, command, username);

    string response = readFromBackendSocket(currentClientNumber, username);
    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

    size_t pos = response.find("+OK");
    string encodedMessage = response.substr(pos + 4);
    printf("encodedMessages is %s\n", encodedMessage.c_str());

    return encodedMessage;
}
// retrieve files/folders in drive (0 for file, 1 for folder)
vector<pair<string, int>> get_drive(string username, int currentClientNumber, string dir_path)
{
    // pair<string, int> f1 = make_pair("document_1.txt", 0);
    // pair<string, int> f2 = make_pair("image_1.png", 0);
    // pair<string, int> d1 = make_pair("folder_1", 1);
    // vector<pair<string, int>> files = {f1, f2, d1};
    printf("dir path is : %s\n", dir_path.c_str());
    string command = "LIST " + username + ",/content/" + dir_path + "\r\n";
    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
    sendToBackendSocket(currentClientNumber, command, username);

    string response = readFromBackendSocket(currentClientNumber, username);
    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

    vector<pair<string, int>> files = extractFiles(username, response, dir_path);

    string target_entry = dir_path;
    files.erase(std::remove_if(files.begin(), files.end(), [&](const pair<string, int> &file)
                               { return file.first == target_entry; }),
                files.end());

    DEBUG ? printf("The files we have are  \n") : 0;
    for (const auto &pair : files)
    {
        std::cout << "(" << pair.first << ", " << pair.second << ")" << std::endl;
    }
    DEBUG ? printf("\n") : 0;

    return files;
}

/////////////////////////////////////
//                                 //
//      HTTP replies & HTML        //
//                                 //
/////////////////////////////////////

// redirect to the user's menu page
string redirectReply()
{
    string response = "HTTP/1.1 302 Found\r\nLocation: /\r\n\r\n\r\n";
    return response;
}

// Render Admin Page
string renderAdminPage()
{
    printf("calling get backend nodes\n");
    vector<Node> backendNodes = getBackendNodes("../backend/kvstore/config.txt");

    string content = "";
    content += "<html>\n";
    content += "<head><title>Admin Console</title></head>\n";
    content += "<body>\n";
    content += "<h1>Admin Console</h1>\n";

    // Display backend nodes
    content += "<h2>Backend Nodes</h2>\n";
    content += "<ul>\n";
    for (const Node &node : backendNodes)
    {
        content += "<li>" + node.name + " - Status: " + (node.isAlive ? "Alive" : "Down") + " ";
        // Add buttons for each node
        content += "<form action=\"/action\" method=\"post\">";
        content += "<input type=\"hidden\" name=\"node_name\" value=\"" + node.name + "\">";
        content += "<input type=\"submit\" name=\"action\" value=\"Disable\">";
        content += "<input type=\"submit\" name=\"action\" value=\"Restart\">";
        content += "</form>";
        content += "</li>\n";
    }
    content += "</ul>\n";

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\n\r\n";
    string reply = header + content;

    return reply;
}
// render the login webpage
string renderLoginPage(string sid, string errorMessage = "")
{
    string content = "";
    content += "<html>\n";
    content += "<head><title>Login Page</title></head>\n";
    content += "<body style='font-family: Arial, sans-serif; background-color: #f0f0f0; text-align: center; padding-top: 50px;'>\n";
    content += "<h1 style='color: #333;'>PennCloud Login</h1>\n";
    content += "<div style='background-color: white; padding: 20px; margin: auto; width: 300px; box-shadow: 0 4px 8px rgba(0,0,0,0.1);'>\n";
    content += "<h2>Log in</h2>\n";
    if (!errorMessage.empty())
    {
        content += "<p style='color: red;'>" + errorMessage + "</p>\n";
    }
    content += "<form action=\"/menu\" method=\"post\">\n";
    content += "Username: <input type=\"text\" name=\"username\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "Password: <input type=\"password\" name=\"password\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "<input type=\"submit\" value=\"Submit\" style='width: 100%; padding: 10px; background-color: #4CAF50; color: white; border: none; cursor: pointer;'>\n";
    content += "</form>\n";
    content += "</div>\n";
    content += "<p><a href=\"#\" onclick='toggleDisplay(\"signup\", \"changepass\")' style='color: blue; cursor: pointer;'>Sign Up</a></p>\n";
    content += "<p><a href=\"#\" onclick='toggleDisplay(\"changepass\", \"signup\")' style='color: blue; cursor: pointer;'>Change Password</a></p>\n";
    content += "<div id='signup' style='display: none; background-color: white; padding: 20px; margin: auto; width: 300px; box-shadow: 0 4px 8px rgba(0,0,0,0.1);'>\n";
    content += "<h2>Sign Up</h2>\n";
    content += "<form action=\"/signup\" method=\"post\">\n";
    content += "Username: <input type=\"text\" name=\"username\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "Password: <input type=\"password\" name=\"password\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "<input type=\"submit\" value=\"Submit\" style='width: 100%; padding: 10px; background-color: #4CAF50; color: white; border: none; cursor: pointer;'>\n";
    content += "</form>\n";
    content += "</div>\n";
    content += "<div id='changepass' style='display: none; background-color: white; padding: 20px; margin: auto; width: 300px; box-shadow: 0 4px 8px rgba(0,0,0,0.1);'>\n";
    content += "<h2>Change Password</h2>\n";
    content += "<form action=\"/newpass\" method=\"post\">\n";
    content += "Username: <input type=\"text\" name=\"username\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "Old Password: <input type=\"text\" name=\"oldpass\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "New Password: <input type=\"text\" name=\"newpass\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "<input type=\"submit\" value=\"Submit\" style='width: 100%; padding: 10px; background-color: #4CAF50; color: white; border: none; cursor: pointer;'>\n";
    content += "</form>\n";
    content += "</div>\n";
    content += "<script>\n";
    content += "function toggleDisplay(showId, hideId) {\n";
    content += "  var showElement = document.getElementById(showId);\n";
    content += "  var hideElement = document.getElementById(hideId);\n";
    content += "  if (showElement.style.display === 'none') {\n";
    content += "    showElement.style.display = 'block';\n";
    content += "    hideElement.style.display = 'none';\n";
    content += "  } else {\n";
    content += "    showElement.style.display = 'none';\n";
    content += "  }\n";
    content += "}\n";
    content += "</script>\n";
    content += "</body>\n";
    content += "</html>\n";

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\n" +
                    "Set-Cookie: sid=" + sid +
                    "\r\n\r\n";
    string reply = header + content;

    return reply;
}

// render menu page
string renderMenuPage(string username)
{
    string content = "";
    content += "<html>\n";
    content += "<head><title>Menu</title></head>\n";
    content += "<body style='font-family: Arial, sans-serif; background-color: #f0f0f0; text-align: center; padding-top: 50px;'>\n";
    content += "<h1 style='color: #333;'>Welcome, " + username + "!</h1>\n";
    content += "<div style='background-color: white; padding: 20px; margin: auto; width: 300px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); text-align: center;'>\n";
    content += "<ul style='list-style: none; padding: 0;'>\n";
    content += "<li style='margin: 10px 0;'><button onclick=\"window.location.href='/mailbox'\" style='width: 90%; padding: 10px; background-color: #4CAF50; color: white; border: none; cursor: pointer;'>Mailbox</button></li>\n";
    content += "<li style='margin: 10px 0;'><button onclick=\"window.location.href='/drive'\" style='width: 90%; padding: 10px; background-color: #4CAF50; color: white; border: none; cursor: pointer;'>Drive</button></li>\n";
    content += "</ul>\n";
    content += "</div>\n";
    content += "<button onclick=\"window.location.href='/'\" style='margin-top: 20px; padding: 10px; width: 300px; background-color: #f44336; color: white; border: none; cursor: pointer;'>Sign Out</button>\n";
    content += "</body>\n";
    content += "</html>\n";

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\n\r\n";
    string reply = header + content;

    return reply;
}

// render the drive webpage
// TODO: render files retrieved from backend
string renderDrivePage(string username, int currentClientNumber, string dir_path = "")
{

    vector<pair<string, int>> files = get_drive(username, currentClientNumber, dir_path);
    printf("directory path is : %s\n", dir_path.c_str());
    printf("username is : %s\n", username.c_str());

    string content = "";
    content += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
    content += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    content += "<title>PennCloud Drive</title>";
    content += "<style>body { font-family: Arial, sans-serif; } ul { list-style-type: none; } li { margin-bottom: 10px; }</style></head><body>";
    content += "<h1>PennCloud Drive</h1>";

    content += "<h2>Create Folder</h2>";
    content += "<button onclick=\"document.getElementById('create-form').style.display='block'\">Create Folder</button>";
    content += "<div id='create-form' style='display:none;'>";
    content += "<form action='/create-folder' method='post'>";
    content += "<label for='folder-name'>Folder Name:</label>";
    content += "<input type='text' id='folder-name' name='folderName' required>";
    content += "<button type='submit'>Create</button>";
    content += "</form></div>";

    content += "<h2>Upload File</h2>";
    content += "<form action='/upload-file' method='post' enctype='multipart/form-data'>";
    content += "<input type='file' name='fileToUpload' required>";
    content += "<button type='submit'>Upload File</button>";
    content += "</form>";

    content += "<h2>Content</h2>";
    content += "<ul>";

    for (const pair<string, int> p : files)
    {
        string name = p.first;
        int isdir = p.second;
        if (isdir)
        {
            if (dir_path == "")
            {
                content += "<li><a href='/drive/" + name + "'>" + name + "</a>";
            }
            else
            {
                content += "<li><a href='/drive/" + dir_path + "/" + name + "'>" + name + "</a>";
            }
            content += "<form action='/rename' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<input type='text' name='newName' placeholder='New name'>";
            content += "<button type='submit'>Rename</button>";
            content += "</form>";

            content += "<form action='/move' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<input type='text' name='newPath' placeholder='New path'>";
            content += "<button type='submit'>Move</button>";
            content += "</form>";

            content += "<form action='/delete' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<button type='submit'>Delete</button>";
            content += "</form>";
        }
        else
        {
            content += "<li>" + name;
            content += "<form action='/rename' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<input type='text' name='newName' placeholder='New name'>";
            content += "<button type='submit'>Rename</button>";
            content += "</form>";

            content += "<form action='/move' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<input type='text' name='newPath' placeholder='New path'>";
            content += "<button type='submit'>Move</button>";
            content += "</form>";

            content += "<form action='/delete' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<button type='submit'>Delete</button>";
            content += "</form>";

            content += "<form action='/download' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<button type='submit'>Download</button>";
            content += "</form>";
        }
    }
    content += "</body></html>";

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\n\r\n";
    string reply = header + content;

    return reply;
}

// render the mailbox webpage
// TODO: render emails retrieved from the backend
string renderMailboxPage(string username, int currentClientNumber)
{
    vector<email> emails = get_mailbox(username, currentClientNumber);

    // Start building the page content
    string content = "";
    content += "<html><head><title>Mailbox</title></head><body>";
    content += "<h1>PennCloud Mailbox</h1>";
    content += "<p>Click to view or send an email.</p>";
    content += "<ul>";
    content += "<li><a href='/mailbox/send'>Send an Email</a></li>";
    content += "</ul>";

    // Email list section
    content += "<table border='1' style='width: 100%;'>";
    content += "<tr><th>From</th><th>Time</th><th>Actions</th></tr>";
    for (email currEmail : emails)
    {
        string timeDecoded = base64DecodeString(currEmail.epochTime);
        string toDisplayCurr = currEmail.from + " (" + timeDecoded + ")";
        string viewLink = "<a href='/mailbox/" + currEmail.id + "'>View</a>";
        string deleteLink = "<a href='/mailbox/delete/" + currEmail.id + "'>Delete</a>";

        // Render each email in a table row with actions
        content += "<tr>";
        content += "<td>" + toDisplayCurr + "</td>";
        content += "<td>" + timeDecoded + "</td>";
        content += "<td>" + viewLink + " | " + deleteLink + "</td>";
        content += "</tr>";
    }
    content += "</table>";
    content += "</body></html>";

    // Construct the full HTTP response
    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\nConnection: keep-alive\r\n\r\n";
    string reply = header + content;

    return reply;
}

// render the email content page for an email (item)
string renderEmailPage(string username, string item, int currentClientNumber)
{
    string content;
    printf("item is %s\n", item.c_str());
    if (item == "send")
    {
        content += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
        content += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        content += "<title>Send Email</title>";
        content += "<style>body { font-family: Arial, sans-serif; } textarea { width: 100%; height: 150px; }</style></head><body>";
        content += "<h1>PennCloud Email</h1>";
        content += "<form action='/send-email' method='POST'>";
        content += "<p><strong>To:</strong> <input type='email' name='to' required></p>";
        content += "<p><strong>Subject:</strong> <input type='text' name='subject' required></p>";
        content += "<p><strong>Message:</strong></p><textarea name='message' required></textarea>";
        content += "<button type='submit'>Send Email</button></form></body></html>";
    }
    else
    {
        string encodedMessage = getEmailContent(item, currentClientNumber, username);
        vector<char> decodedEmailVector = base64Decode(encodedMessage);
        string decodedEmail(decodedEmailVector.begin(), decodedEmailVector.end());
        printf("encoded email is %s\n", encodedMessage.c_str());
        printf("decoded email is %s\n", decodedEmail.c_str());
        // Split the decoded email into lines
        vector<string> emailLines;
        stringstream ss(decodedEmail);
        string line;
        while (getline(ss, line, '\n'))
        {
            emailLines.push_back(line);
        }

        string sender, subject, body;
        // Extract sender, subject, and body from emailLines
        for (const string &emailLine : emailLines)
        {
            if (emailLine.find("From:") == 0)
            {
                // Extract sender's name (part before '@' and after '<')
                size_t start = emailLine.find("<");
                size_t end = emailLine.find("@");
                if (start != string::npos && end != string::npos)
                {
                    sender = emailLine.substr(start + 1, end - start - 1);
                }
            }
            else if (emailLine.find("Subject:") == 0)
            {
                subject = emailLine.substr(9); // Extract subject
            }
            else
            {
                // Assume everything else is part of the email body
                body += emailLine + "<br>";
            }
        }
        vector<char> itemVec(item.begin(), item.end());
        string itemEncoded = base64Encode(itemVec);
        content += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
        content += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        content += "<title>Email Viewer</title>";
        content += "<style>body { font-family: Arial, sans-serif; }";
        content += "#email-content { background-color: #f8f8f8; padding: 20px; margin-bottom: 20px; }";
        content += "textarea { width: 100%; height: 150px; }</style></head><body>";
        content += "<h1>PennCloud Email</h1>";
        content += "<div id='email-content'>";
        content += "<p><strong>From:</strong> " + sender + "</p>";
        content += "<p><strong>Subject:</strong> " + subject + "</p>";
        content += "<p><strong>Message:</strong> " + body + "</p></div>";
        content += "<h2>Forward</h2>";
        content += "<form action='/forward-email' method='POST'>";
        content += "<input type='hidden' name='email_id' value='" + itemEncoded + "'>"; // Include original email content
        content += "<p><strong>To:</strong> <input type='email' name='to' required></p>";
        content += "<button type='submit'>forward</button></form></body></html>";
        content += "<h2>Write a Reply</h2>";
        content += "<form action='/send-email' method='POST'>";
        content += "<p><strong>To:</strong> <input type='email' name='to' required></p>";
        content += "<p><strong>Subject:</strong> <input type='text' name='subject' value='Re: " + subject + "' required></p>";
        content += "<p><strong>Message:</strong></p><textarea name='message' required></textarea>";
        content += "<button type='submit'>Send</button></form></body></html>";
    }

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\nConnection : keep-alive" + "\r\n\r\n";
    string reply = header + content;

    return reply;
}

// render a webpage displaying http errors
string renderErrorPage(int err_code)
{

    string err = to_string(err_code);
    string err_msg = "";
    if (err_code == NOTFOUND)
    {
        // err = to_string(NOTFOUND);
        err_msg = "404 Not Found";
    }
    else if (err_code == FORBIDDEN)
    {
        // err = to_string(FORBIDDEN);
        err_msg = "403 Forbidden";
    }

    string content = "";
    content += "<html>\n";
    content += "<head><title>Error</title></head>\n";
    content += "<body>\n";
    content += "<h1>";
    content += err_msg;
    content += "</h1>\n";
    content += "</body>\n";
    content += "</html>\n";

    string header = "HTTP/1.1 " + err_msg +
                    "\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\nConnection : keep-alive" + "\r\n\r\n";
    string reply = header + content;

    return reply;
}

string generateReply(int reply_code, string username = "", string item = "", string sid = "", int currentClientNumber = 0)
{
    if (reply_code == LOGIN)
    {
        return renderLoginPage(sid);
    }
    else if (reply_code == SIGNUP)
    {
        return renderLoginPage(sid);
    }
    else if (reply_code == NEWPASS)
    {
        return renderLoginPage(sid);
    }
    else if (reply_code == REDIRECT)
    {
        return redirectReply();
    }
    else if (reply_code == MENU)
    {
        return renderMenuPage(username);
    }
    else if (reply_code == DRIVE)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == MAILBOX)
    {
        return renderMailboxPage(username, currentClientNumber);
    }
    else if (reply_code == EMAIL)
    {
        // if the reply_code starts with delete we actually render the mailbox Krimo
        if (item.rfind("delete", 0) == 0)
        {
            deleteEmail(username, item, currentClientNumber);
            return renderMailboxPage(username, currentClientNumber);
        }
        return renderEmailPage(username, item, currentClientNumber);
    }
    else if (reply_code == SENDEMAIL)
    {
        return renderMailboxPage(username, currentClientNumber);
    }
    else if (reply_code == FORWARD)
    {
        return renderMailboxPage(username, currentClientNumber);
    }
    else if (reply_code == DOWNLOAD)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == RENAME)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == MOVE)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == DELETE)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == NEWDIR)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == UPLOAD)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == ADMIN)
    {
        return renderAdminPage();
    }

    string reply = renderErrorPage(reply_code);
    return reply;
}

/////////////////////////////////////
//                                 //
//      Threads and Signals        //
//                                 //
/////////////////////////////////////

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

                    else if (strcmp(url + strlen(url) - strlen("/admin"), "/admin") == 0)
                    {
                        reply_code = ADMIN;
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