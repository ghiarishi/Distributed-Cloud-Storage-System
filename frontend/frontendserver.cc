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

using namespace std;

struct Node
{
    int id;
    string ip;
    int tcp;
    int udp;
    int udp2;
    string name;
    bool isAlive;
    int socket;
    int port;

    Node(){};
    Node(int id, string ip, int tcp, int udp, int udp2, string name)
        : id(id), ip(ip), tcp(tcp), udp(udp), udp2(udp2), name(name) {}
};

struct email
{
    string from;
    string epochTime;
    string content;
    string id;
    email(){};
};

int PORT = 10000;
int DEBUG = 0;
size_t READ_SIZE = 5;
size_t FBUFFER_SIZE = 1024;
const int MAX_CLIENTS = 100;

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


/////////////////////////////////////
//                                 //
//        Extract Methods                //
//                                 //
/////////////////////////////////////

vector<email> extractEmails(string username , string returnString)
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

/////////////////////////////////////
//                                 //
//           Utilities             //
//                                 //
/////////////////////////////////////

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

// Get filename from the path
string getFileName(const string &path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos)
        return path.substr(pos + 1);
    return path;
}

// Parse login data
tuple<string, string> parseLoginData(string data_str)
{

    const char *data = data_str.c_str();
    // parse request url
    char tmp[strlen(data)];
    strncpy(tmp, data, strlen(data));
    tmp[strlen(data)] = '\0';
    strtok(tmp, "=");
    char *username = strtok(NULL, "&");
    strtok(NULL, "=");
    char *password = strtok(NULL, "");

    return make_tuple(string(username), string(password));
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
std::vector<std::vector<char>> split(const std::vector<char>& s, const std::string& delimiter) {
    std::vector<std::vector<char>> parts;
    auto it = s.begin();
    while (it != s.end()) {
        auto pos = std::search(it, s.end(), delimiter.begin(), delimiter.end());
        if (pos != s.end()) {
            parts.emplace_back(it, pos);
            it = pos + delimiter.size();
        } else {
            parts.emplace_back(it, s.end());
            break;
        }
    }
    return parts;
}

// Extracts the boundary from the Content-Type header
std::string extract_boundary(const std::string& contentType) {
    size_t pos = contentType.find("boundary=");
    if (pos == std::string::npos) return "";
    std::string boundary = contentType.substr(pos + 9);
    if (boundary.front() == '"') {
        boundary.erase(0, 1); // Remove the first quote
        boundary.erase(boundary.size() - 1); // Remove the last quote
    }
    return boundary;
}

// Parses the multipart/form-data content and returns file content and filename
std::pair<std::vector<char>, std::string> parse_multipart_form_data(const string& contentType, const vector<char>& body) {
    std::string boundary = extract_boundary(contentType);
    std::string delimiter = "--" + boundary + "\r\n";
    std::string endDelimiter = "--" + boundary + "--";
    std::vector<char> fileContent;
    std::string filename;

    std::vector<std::vector<char>> parts = split(body, delimiter);

    for (const auto& part : parts) {
        if (part.empty() || std::equal(part.begin(), part.end(), endDelimiter.begin(), endDelimiter.end())) {
            continue;
        }

        auto headerEndPos = std::search(part.begin(), part.end(), std::begin("\r\n\r\n"), std::end("\r\n\r\n") - 1);
        if (headerEndPos == part.end()) {
            continue; // Skip if there's no header
        }

        std::string headers(part.begin(), headerEndPos);
        std::vector<char> content(headerEndPos + 4, part.end() - 2); // Remove last \r\n

        if (headers.find("filename=") != std::string::npos) {
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
void send_file_data(int client_socket, string file_path, int file_size, char *data) {

    stringstream header;
    header << "HTTP/1.1 200 OK\r\n";
    header << "Content-Type: application/octet-stream\r\n";

    string file_name = getFileName(file_path);

	header << "Content-Length: " << file_size << "\r\n";
	header << "Content-Disposition: attachment; filename=\"" << file_name << "\"\r\n";
	header << "\r\n";

	send(client_socket, header.str().c_str(), header.str().size(), 0);
	if (DEBUG) {
		fprintf(stderr, "[%d] S: %s\n", client_socket, header.str().c_str());
	}

	char buffer[FBUFFER_SIZE];
	send(client_socket, data, file_size, 0);
	if (DEBUG) {
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

// Helper function to send data to backend server
bool sendToBackendSocket(int backend_sock, string command)
{
    if (send(backend_sock, command.c_str(), command.length(), 0) < 0)
    {
        cerr << "Error sending data to backend server" << std::endl;
        return false;
    }
    return true;
}

// Helper function to read from backend socket
string readFromBackendSocket(int backend_sock)
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
        printf("the buffer is %s\n", buffer);

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
    char serverInfo[1024];
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

// login verification
int authenticate(string username, string password, int currentClient)
{
    string command = "GET " + username + ",password\r\n";

    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClient].socket) : 0;
    sendToBackendSocket(backend_socks[currentClient].socket, command);

    DEBUG ? printf("Sent command to server\n") : 0;

    string response = readFromBackendSocket(backend_socks[currentClient].socket);
    string rightPassword = extractPassword(response);
    DEBUG ? printf("Response: %s Correct password is: |%s| and user entered: |%s|\n", response.c_str(), rightPassword.c_str(), password.c_str()) : 0;

    return password == rightPassword ? 1 : 0;
}

// retrieve emails in mailbox
vector<email> get_mailbox(string username, int currentClientNumber)
{
    string command = "LIST " + username + ",/emails\r\n";
    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
    sendToBackendSocket(backend_socks[currentClientNumber].socket, command);

    string response = readFromBackendSocket(backend_socks[currentClientNumber].socket);
    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

    vector<email> emails = extractEmails(username , response);
    
    return emails;
}

string getEmailContent( string emailID , int currentClientNumber){
    string command = "GET " + emailID + "\r\n";
    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
    sendToBackendSocket(backend_socks[currentClientNumber].socket, command);

    string response = readFromBackendSocket(backend_socks[currentClientNumber].socket);
    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

    return response;
}
// retrieve files/folders in drive (0 for file, 1 for folder)
vector<pair<string, int>> get_drive(string username, int currentClientNumber , string dir_path)
{
    pair<string, int> f1 = make_pair("document_1.txt", 0);
    pair<string, int> f2 = make_pair("image_1.png", 0);
    pair<string, int> d1 = make_pair("folder_1", 1);
    vector<pair<string, int>> files = {f1, f2, d1};

    string command = "LIST " + username + ",/content\r\n";
    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
    sendToBackendSocket(backend_socks[currentClientNumber].socket, command);

    string response = readFromBackendSocket(backend_socks[currentClientNumber].socket);
    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

    vector<email> emails = extractEmails(username , response);
    
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
string renderLoginPage(string sid)
{

    string content = "";
    content += "<html>\n";
    content += "<head><title>Login Page</title></head>\n";
    content += "<body>\n";
    content += "<h1>PennCloud Login</h1>\n";
    content += "<form action=\"/menu\" method=\"post\">\n";
    content += "Username: <input type=\"text\" name=\"username\"><br>\n";
    content += "Password: <input type=\"password\" name=\"password\"><br>\n";
    content += "<input type=\"submit\" value=\"Submit\">\n";
    content += "</form>\n";
    content += "</body>\n";
    content += "</html>\n";

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\n" +
                    "Set-Cookie: sid=" + sid +
                    "\r\n\r\n";
    string reply = header + content;

    return reply;
}

// render the menu webpage
string renderMenuPage(string username)
{

    string content = "";
    content += "<html><body><h1>Menu</h1><ul>";
    content += "<li><a href='/mailbox'>Mailbox</a></li>";
    content += "<li><a href='/drive'>Drive</a></li>";
    content += "</ul></body></html>";

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\n\r\n";
    string reply = header + content;

    return reply;
}

// render the drive webpage
// TODO: render files retrieved from backend
string renderDrivePage(string username,  int currentClientNumber , string dir_path = "" )
{

    vector<pair<string, int>> files = get_drive(username, currentClientNumber , dir_path);

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
            content += "<li><a href='/drive/" + name + "'>" + name + "</a>";
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

    string content = "";
    content += "<html><head><title>File List</title></head><body>";
    content += "<h1>PennCloud Mailbox</h1>";
    content += "<p>Click to view or send an email</p>";
    content += "<ul>";
    content += "<li><a href='/mailbox/send'>send</a></li>";

    for (email currEmail : emails)
    {
        string toDisplayCurr = "from : " + currEmail.from + " time : " + currEmail.epochTime + "id : " + currEmail.id;
        content += "<li><a href='/mailbox/" + currEmail.id + "'>" + toDisplayCurr + "</a></li>";
    }

    content += "</ul>";
    content += "</body></html>";

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\n\r\n";
    string reply = header + content;

    return reply;
}

// render the email content page for an email (item)
string renderEmailPage(string username, string item , int currentClientNumber)
{
    string content = getEmailContent(item , currentClientNumber);
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
        content += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
        content += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        content += "<title>Email Viewer</title>";
        content += "<style>body { font-family: Arial, sans-serif; }";
        content += "#email-content { background-color: #f8f8f8; padding: 20px; margin-bottom: 20px; }";
        content += "textarea { width: 100%; height: 150px; }</style></head><body>";
        content += "<h1>PennCloud Email</h1>";
        content += "<div id='email-content'>";
        content += "<p><strong>From:</strong> sender@example.com</p>";
        content += "<p><strong>Subject:</strong> Test Email</p>";
        content += "<p>Hello, this is a sample email content displayed here.</p></div>";
        content += "<h2>Forward</h2>";
        content += "<form action='/forward-email' method='POST'>";
        content += "<p><strong>To:</strong> <input type='email' name='to' required></p>";
        content += "<button type='submit'>forward</button></form></body></html>";
        content += "<h2>Write a Reply</h2>";
        content += "<form action='/send-email' method='POST'>";
        content += "<p><strong>To:</strong> <input type='email' name='to' required></p>";
        content += "<p><strong>Subject:</strong> <input type='text' name='subject' value='Re: Test Email' required></p>";
        content += "<p><strong>Message:</strong></p><textarea name='message' required></textarea>";
        content += "<button type='submit'>Send</button></form></body></html>";
    }

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\n\r\n";
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
                    to_string(content.length()) + "\r\n\r\n";
    string reply = header + content;

    return reply;
}

string generateReply(int reply_code, string username = "", string item = "", string sid = "", int currentClientNumber = 0)
{
    if (reply_code == LOGIN)
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
        return renderDrivePage(username, currentClientNumber , item);
    }
    else if (reply_code == MAILBOX)
    {
        return renderMailboxPage(username, currentClientNumber);
    }
    else if (reply_code == EMAIL)
    {
        return renderEmailPage(username, item , currentClientNumber);
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
        return renderDrivePage(username, currentClientNumber , item );
    }
    else if (reply_code == RENAME)
    {
        return renderDrivePage(username, currentClientNumber , item );
    }
    else if (reply_code == MOVE)
    {
        return renderDrivePage(username, currentClientNumber , item );
    }
    else if (reply_code == DELETE)
    {
        return renderDrivePage(username, currentClientNumber , item );
    }
    else if (reply_code == NEWDIR)
    {
        return renderDrivePage(username, currentClientNumber , item );
    }
    else if (reply_code == UPLOAD)
    {
        return renderDrivePage(username, currentClientNumber , item );
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
    char *dataBuffer;
    char *contentBuffer;
    size_t dataBufferSize = 0;
    // size_t content_read = 0;

    int read_header = 0;
    int read_body = 0;
    int contentlen = 0;
    string contentType = "";

    string sid = generate_cookie();
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
            // Add the data to the data buffer
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
                    char *content = (char *)malloc((contentlen+1) * sizeof(char));
					memcpy(content, dataBuffer, contentlen);
					content[contentlen] = '\0';

                    // process the message body
					if (DEBUG) {
						//fprintf(stderr, "[%d] C: %s\n", sock, content);
						//fprintf(stderr, "[%d] C: %ld\n", sock, dataBufferSize);
						for (int c = 0; c < contentlen; c++) {
							char c_tmp[1];
							strncpy(c_tmp, content+c, 1);
							c_tmp[1] = '\0';
							fprintf(stderr, "%s", c_tmp);

							// break for message body that's too long
							if (c >= 2048) {
								fprintf(stderr, "\n..............\n");
								break;
							}
						}
						fprintf(stderr, "\n");
					}

                    // request to get menu webpage
                    if (reply_code == MENU)
                    {
                        tuple<string, string> credentials = parseLoginData(string(content));
                        username = get<0>(credentials);
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
                    }
                    // forward email
                    else if (reply_code == FORWARD)
                    {
                        map<string, string> msg_map = parseQuery(string(content));
                        string to = msg_map["to"];
                        if (DEBUG)
                        {
                            fprintf(stderr, "to: %s\n", to.c_str());
                        }
                    }

                    else if (reply_code == DELETE)
                    {
                        map<string, string> msg_map = parseQuery(string(content));
                        string fname = msg_map["fileName"];
                        if (DEBUG)
                        {
                            fprintf(stderr, "fname: %s\n", fname.c_str());
                        }
                    }

                    else if (reply_code == RENAME)
                    {
                        map<string, string> msg_map = parseQuery(string(content));
                        string fname = msg_map["fileName"];
                        string new_fname = msg_map["newName"];
                        if (DEBUG)
                        {
                            fprintf(stderr, "fname: %s\nnew_fname: %s\n", fname.c_str(), new_fname.c_str());
                        }
                    }

                    else if (reply_code == MOVE)
                    {
                        map<string, string> msg_map = parseQuery(string(content));
                        string fname = msg_map["fileName"];
                        string new_path = msg_map["newPath"];
                        if (DEBUG)
                        {
                            fprintf(stderr, "fname: %s\nnew_path: %s\n", fname.c_str(), new_path.c_str());
                        }
                    }

                    else if (reply_code == DELETE)
                    {
                        map<string, string> msg_map = parseQuery(string(content));
                        string fname = msg_map["fileName"];
                        if (DEBUG)
                        {
                            fprintf(stderr, "fname: %s\n", fname.c_str());
                        }
                    }

                    else if (reply_code == NEWDIR)
                    {
                        map<string, string> msg_map = parseQuery(string(content));
                        string dirname = msg_map["folderName"];
                        if (DEBUG)
                        {
                            fprintf(stderr, "dirname: %s\n", dirname.c_str());
                        }
                    }

                    else if (reply_code == UPLOAD)
                    {
                        vector<char> content_vec;
						content_vec.assign(content, content+contentlen);
						auto msg_pair = parse_multipart_form_data(contentType, content_vec);
						vector<char> fdata = msg_pair.first;
						string fname = msg_pair.second;
						if (DEBUG) {
							fprintf(stderr, "fname: %s\nfdata_len: %ld\n", fname.c_str(), fdata.size());
						}
						contentType = "";
                    }

                    // forbidden access
                    if (reply_code != LOGIN && reply_code != REDIRECT && (logged_in != 1 || sid != tmp_sid))
                    {
                        reply_code = FORBIDDEN;
                    }

                    // send reply
                    if (reply_code == DOWNLOAD)
                    {
                        //string filename = "/home/cis5050/Downloads/graph.jpg";
						//string filename = "/home/cis5050/Downloads/hw2.zip";
						string filename = "/home/cis5050/Downloads/video.mp4";
						send_file(sock, filename);

						// NOTE: to send the actual binary data retrieved from the backend, use
						// send_file_data(sock, file_path, file_size, data);
                    }

                    string reply_string = generateReply(reply_code, username, item, sid, currentClientNumber);
                    const char *reply = reply_string.c_str();

                    send(sock, reply, strlen(reply), 0);
                    if (DEBUG)
                    {
                        fprintf(stderr, "[%d] S: %s\n", sock, reply);
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
                    fprintf(stderr, "[%d] C: %s\n", sock, cmd);
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

                            if (reply_code != LOGIN && (logged_in != 1 || sid != tmp_sid))
                            {
                                reply_code = FORBIDDEN;
                            }

                            // send reply
                            string reply_string = generateReply(reply_code, username, item, sid, currentClientNumber);
                            const char *reply = reply_string.c_str();

                            send(sock, reply, strlen(reply), 0);
                            if (DEBUG)
                            {
                                fprintf(stderr, "[%d] S: %s\n", sock, reply);
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

                    // redirect to menu page
                    if (strcmp(url, "/menu") == 0)
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
                        fprintf(stderr, "[%d] S: %s", sock, unkCmd);
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
            continue;
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
            fprintf(stderr, "[%d] New Connection\n", *fd);
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
