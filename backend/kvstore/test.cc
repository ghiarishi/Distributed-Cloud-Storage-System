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
    std::ifstream file("/home/cis5050/Desktop/sp24-cis5050-T18/backend/kvstore/text.txt", ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file\n";
        return 1;
    }

    // Use a buffer to read and send data
    const size_t bufferSize = 4096;
    char buffer[bufferSize];
    int count = 0;

    while (file.read(buffer, bufferSize) || file.gcount() > 0) {
        int bytesToSend = file.gcount();  // Get the number of bytes read from the file
        if (write(sock, buffer, bytesToSend) != bytesToSend) {
            std::cerr << "Send failed\n";
            break;
        }
        count += bytesToSend;
        usleep(5000); // Slightly longer sleep to avoid overloading network for large sends
    }

    // Sending end of message marker
    write(sock, "\r\n", 2);

    cout << "Total bytes sent: " << count << endl;

    // Close the file and the socket
    file.close();
    close(sock);

    return 0;
}
