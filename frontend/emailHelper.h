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
#ifndef EMAILHELPER_H
#define EMAILHELPER_H

#include <string>
#include <vector>

std::tuple<std::string, std::string> extractSubjectAndMessage(const std::string &email);
string extractPath(const string &path);

#endif // FRONTENDSERVER_H
