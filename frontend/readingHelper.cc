#include "frontendserver.h"
#include "render.h"
#include "loginHelper.h"
#include "readingHelper.h"

using namespace std;


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


