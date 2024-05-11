#include "frontendserver.h"
#include "render.h"
#include "readingHelper.h"


using namespace std;



string extractPassword(string returnString)
{
    // Find the position of the first space after "+OK "
    size_t startPos = 4; // Length of "+OK "
    size_t endPos = returnString.find("\r\n", startPos);

    // Extract the substring between "+OK " and "\r\n"
    string password = returnString.substr(startPos, endPos - startPos);
    return password;
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
