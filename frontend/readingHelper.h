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


#ifndef READHELP_H
#define READHELP_H

using namespace std;

bool sendToBackendSocket(int clientNumber, string command, string username);
bool sendToSocket(int backend_sock, string command);
string readFromBackendSocket(int clientNumber, string username);
string readFromSocket(int backend_sock);
int connectToBackend(string username, int clientNum);
pair<string, int> extractIPAndPort(const string &serverInfo);
vector<char> base64Decode(const string &encoded_data);
string base64DecodeString(const string &encoded_data);
string base64Encode(const vector<char> &data);
string base64Encode(const string &dataString);


#endif 