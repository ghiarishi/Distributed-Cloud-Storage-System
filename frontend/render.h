// Standard C headers for input/output, memory management, string manipulation, and system calls
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>    // POSIX threads

// System headers for network programming and socket communication
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

// Other C++ headers for data formatting
#include <iomanip>

#include "frontendserver.h" // Declarations specific to frontend server

#ifndef RENDER_H
#define RENDER_H

#include <string>

using std::string;

/**
 * Redirects the user to the menu page.
 * @return An HTTP response string containing the redirection instruction.
 */
string redirectReply();

/**
 * Renders the login webpage.
 * @param sid The session identifier.
 * @param errorMessage Optional error message to display on the page.
 * @return The HTML content of the login page.
 */
string renderLoginPage(string sid, string errorMessage = "");

/**
 * Renders the main menu page for the given user.
 * @param username The username of the current user.
 * @return The HTML content of the menu page.
 */
string renderMenuPage(string username);

/**
 * Renders the drive page for the given user, showing the current directory.
 * @param username The username of the current user.
 * @param currentClientNumber The client number associated with the user.
 * @param dir_path Optional directory path to display, defaults to root.
 * @return The HTML content of the drive page.
 */
string renderDrivePage(string username, int currentClientNumber, string dir_path = "");

/**
 * Extracts the filename from a given path.
 * @param path The full path from which to extract the filename.
 * @return The filename extracted from the path.
 */
string getFileName(const string &path);

/**
 * Renders the mailbox page for the given user.
 * @param username The username of the current user.
 * @param currentClientNumber The client number associated with the user.
 * @return The HTML content of the mailbox page.
 */
string renderMailboxPage(string username, int currentClientNumber);

/**
 * Renders the email view page for a specific email.
 * @param username The username of the current user.
 * @param item The specific email identifier or message.
 * @param currentClientNumber The client number associated with the user.
 * @return The HTML content of the email page.
 */
string renderEmailPage(string username, string item, int currentClientNumber);

/**
 * Renders an error page based on the provided error code.
 * @param err_code The HTTP error code to display.
 * @return The HTML content of the error page.
 */
string renderErrorPage(int err_code);

/**
 * Generates an HTTP response based on various parameters.
 * @param reply_code The HTTP status code for the response.
 * @param username Optional username associated with the response.
 * @param item Optional item identifier (e.g., filename or email ID).
 * @param sid Optional session identifier.
 * @param currentClientNumber Optional client number associated with the user.
 * @return The full HTTP response message.
 */
string generateReply(int reply_code, string username = "", string item = "", string sid = "", int currentClientNumber = 0);

#endif // RENDER_H
