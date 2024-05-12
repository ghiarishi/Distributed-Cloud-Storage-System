// Standard C headers for input/output, memory management, string manipulation, and system calls
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>    // POSIX threads

// System headers for network and socket programming
#include <sys/types.h>
#include <netinet/in.h>   // IPv4 and IPv6 protocol family
#include <arpa/inet.h>    // IP address conversion functions
#include <signal.h>       // Signal handling
#include <fcntl.h>        // File control options

// C++ standard library headers
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

// OpenSSL headers for encoding/decoding and buffer functions
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

// Other standard library headers for formatting and utility functions
#include <iomanip>
#include <utility>
#include <string>

#ifndef READHELP_H
#define READHELP_H

using namespace std;

/**
 * Sends a command to the backend socket associated with a specific client.
 * @param clientNumber The client's unique identifier.
 * @param command The command string to send.
 * @param username The client's username for identification.
 * @return True if the message was sent successfully, false otherwise.
 */
bool sendToBackendSocket(int clientNumber, string command, string username);

/**
 * Sends a command directly to the specified backend socket.
 * @param backend_sock The backend socket descriptor.
 * @param command The command string to send.
 * @return True if the message was sent successfully, false otherwise.
 */
bool sendToSocket(int backend_sock, string command);

/**
 * Reads data from the backend socket associated with a specific client.
 * @param clientNumber The client's unique identifier.
 * @param username The client's username for identification.
 * @return The received data as a string.
 */
string readFromBackendSocket(int clientNumber, string username);

/**
 * Reads data directly from the specified backend socket.
 * @param backend_sock The backend socket descriptor.
 * @return The received data as a string.
 */
string readFromSocket(int backend_sock);

/**
 * Connects to the backend server using a username and client number.
 * @param username The client's username for identification.
 * @param clientNum The client's unique identifier.
 * @return The socket descriptor for the backend server connection.
 */
int connectToBackend(string username, int clientNum);

/**
 * Extracts IP address and port number from a given server info string.
 * @param serverInfo The server information string (e.g., "192.168.1.1:8080").
 * @return A pair containing the extracted IP address and port number.
 */
pair<string, int> extractIPAndPort(const string &serverInfo);

/**
 * Decodes a Base64-encoded string into a vector of characters.
 * @param encoded_data The Base64-encoded string.
 * @return The decoded data as a vector of characters.
 */
vector<char> base64Decode(const string &encoded_data);

/**
 * Decodes a Base64-encoded string into its original string format.
 * @param encoded_data The Base64-encoded string.
 * @return The decoded data as a string.
 */
string base64DecodeString(const string &encoded_data);

/**
 * Encodes a vector of characters into a Base64 string.
 * @param data The vector of characters to encode.
 * @return The encoded data as a Base64 string.
 */
string base64Encode(const vector<char> &data);

/**
 * Encodes a string into a Base64-encoded string.
 * @param dataString The string to encode.
 * @return The encoded data as a Base64 string.
 */
string base64Encode(const string &dataString);

#endif // READHELP_H
