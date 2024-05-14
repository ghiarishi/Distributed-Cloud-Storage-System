// Standard C headers
#include <stdio.h>      // I/O operations
#include <stdlib.h>     // Memory management, conversions
#include <string.h>     // String manipulation functions
#include <unistd.h>     // POSIX operating system API
#include <pthread.h>    // POSIX threads

// System headers for network programming
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>

// C++ standard library headers
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

// OpenSSL headers
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

// Other standard library headers
#include <iomanip>
#include <utility>

#include "frontendserver.h" // Custom frontend server declarations

// Include guard to prevent multiple inclusions of this header file
#ifndef EMAILHELPER_H
#define EMAILHELPER_H

// Include necessary standard C++ headers for email processing functions
#include <string>   // C++ string class
#include <vector>   // C++ vector container

/**
 * @brief Extracts the subject and message from an email string.
 * 
 * @param email The raw email content as a string.
 * @return A tuple containing the subject and message extracted from the email.
 */
std::tuple<std::string, std::string> extractSubjectAndMessage(const std::string &email);

/**
 * @brief Extracts a file path from a given string.
 * 
 * @param path The raw path string.
 * @return The extracted path as a string.
 */
std::string extractPath(const std::string &path);

// End of include guard
#endif // EMAILHELPER_H
