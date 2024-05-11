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
#include <string>
#include <utility>
#include <string> 


#include "frontendserver.h"

#ifndef LOGINHELP_H
#define LOGINHELP_H

using namespace std;


string extractPassword(string returnString);
std::string urlEncode(const std::string &value);
tuple<string, string> parseLoginData(string data_str);
string decodeURIComponent(const string &s);
map<string, string> parseQuery(const string &query);
std::vector<std::vector<char>> split(const std::vector<char> &s, const std::string &delimiter);
std::string extract_boundary(const std::string &contentType);
std::pair<std::vector<char>, std::string> parse_multipart_form_data(const string &contentType, const vector<char> &body);
void send_chunk(int client_socket, const vector<char> &data);
void send_file(int client_socket, const string &file_path);
void send_file_data(int client_socket, string file_path, int file_size, char *data);
string generate_cookie();
void mailMessage(string username, string to, string subject, string message);
int connectToMail();
int authenticate(string username, string password, int currentClient);

#endif // RENDER_H