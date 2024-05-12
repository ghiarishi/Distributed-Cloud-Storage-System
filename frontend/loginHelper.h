// Standard C headers for input/output, memory management, string manipulation, and system calls
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>    // For POSIX threads

// System headers for socket programming and network operations
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

// OpenSSL headers for encryption and encoding/decoding functions
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

// Other C++ headers for data manipulation and stream formatting
#include <iomanip>
#include <utility>
#include <string>

#include "frontendserver.h" // Declarations specific to frontend server

#ifndef LOGINHELP_H
#define LOGINHELP_H

using namespace std;

/**
 * Extracts the password from a given string.
 * @param returnString Input string containing password information.
 * @return Extracted password.
 */
string extractPassword(string returnString);

/**
 * Encodes a string for safe transmission in URLs.
 * @param value Input string to encode.
 * @return URL-encoded string.
 */
std::string urlEncode(const std::string &value);

/**
 * Parses login data string into username and password.
 * @param data_str Input login data string.
 * @return Tuple containing username and password.
 */
tuple<string, string> parseLoginData(string data_str);

/**
 * Decodes a percent-encoded URI component.
 * @param s Encoded string.
 * @return Decoded string.
 */
string decodeURIComponent(const string &s);

/**
 * Parses a query string into a map of key-value pairs.
 * @param query Input query string.
 * @return Map containing key-value pairs.
 */
map<string, string> parseQuery(const string &query);

/**
 * Splits a vector of characters using a specified delimiter.
 * @param s Input character vector.
 * @param delimiter Delimiter string.
 * @return Vector of vectors containing the split data.
 */
std::vector<std::vector<char>> split(const std::vector<char> &s, const std::string &delimiter);

/**
 * Extracts boundary information from the Content-Type header.
 * @param contentType Input Content-Type header string.
 * @return Extracted boundary string.
 */
std::string extract_boundary(const std::string &contentType);

/**
 * Parses multipart form data into a pair containing the data and its filename.
 * @param contentType Input Content-Type header.
 * @param body Body content as a vector of characters.
 * @return Pair containing the parsed data and filename.
 */
std::pair<std::vector<char>, std::string> parse_multipart_form_data(const string &contentType, const vector<char> &body);

/**
 * Sends a data chunk to the specified client socket.
 * @param client_socket Client socket descriptor.
 * @param data Data to send.
 */
void send_chunk(int client_socket, const vector<char> &data);

/**
 * Sends an entire file to the specified client socket.
 * @param client_socket Client socket descriptor.
 * @param file_path Path to the file to send.
 */
void send_file(int client_socket, const string &file_path);

/**
 * Sends specific file data to a client socket.
 * @param client_socket Client socket descriptor.
 * @param file_path Path to the file.
 * @param file_size Size of the file.
 * @param data Pointer to the data buffer.
 */
void send_file_data(int client_socket, string file_path, int file_size, char *data);

/**
 * Generates a secure cookie.
 * @return Generated cookie as a string.
 */
string generate_cookie();

/**
 * Sends an email message with the specified parameters.
 * @param username Sender's username.
 * @param to Recipient's email address.
 * @param subject Subject of the email.
 * @param message Main content/body of the email.
 */
void mailMessage(string username, string to, string subject, string message);

/**
 * Connects to the mail server.
 * @return Socket descriptor for the mail server connection.
 */
int connectToMail();

/**
 * Authenticates a user against the mail server.
 * @param username User's username.
 * @param password User's password.
 * @param currentClient Current client socket descriptor.
 * @return Status code indicating success or failure of authentication.
 */
int authenticate(string username, string password, int currentClient);

#endif // LOGINHELP_H
