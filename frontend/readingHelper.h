// frontendserver.cc
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
// render.h

#ifndef READHELP_H
#define READHELP_H

using namespace std;

bool sendToBackendSocket(int clientNumber, string command, string username);
bool sendToSocket(int backend_sock, string command);
string readFromBackendSocket(int clientNumber, string username);
string readFromSocket(int backend_sock);
int connectToBackend(string username, int clientNum);
pair<string, int> extractIPAndPort(const string &serverInfo);

#endif // RENDER_H