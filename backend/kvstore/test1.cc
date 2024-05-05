#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

int main() {
    // Create a socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    // Server address
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(2001);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        std::cerr << "Connection failed\n";
        return 1;
    }

    // Open the file
    std::ifstream file1("/home/cis5050/sp24-cis5050-T18/backend/kvstore/test1.txt", ios::binary);
    if (!file1.is_open()) {
        std::cerr << "Failed to open file\n";
        return 1;
    }

    // Use a buffer to read and send data
    const size_t bufferSize = 4096;
    char buffer[bufferSize];
    long long count = 0;

    while (file1.read(buffer, bufferSize) || file1.gcount() > 0) {
        int bytesToSend = file1.gcount();  // Get the number of bytes read from the file
        if (write(sock, buffer, bytesToSend) != bytesToSend) {
            std::cerr << "Send failed\n";
            break;
        }
        count += bytesToSend;
        // usleep(10000); // Slightly longer sleep to avoid overloading network for large sends
    }

    write(sock,"\r\n",2);

    file1.close();

    ssize_t bytesRead;
    char buffer1[bufferSize];
    string response;

    while (bytesRead < 5) {
        bytesRead += read(sock, buffer1, bufferSize);
        // Append the read data to the response string
        cout << "Bytes Read -> " << bytesRead << endl;
        response.append(buffer1, bytesRead);
        cout << "Response read from socket so far ->" << response << endl;
    }

    // Check if the response contains "OK"
    if (response.find("+OK") != std::string::npos) {
        return 0;
    } else {
        return 1;
    }
}
