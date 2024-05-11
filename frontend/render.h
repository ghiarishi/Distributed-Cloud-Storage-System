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

#include "frontendserver.h"
// render.h

#ifndef RENDER_H
#define RENDER_H

#include <string>

using std::string;

// redirect to the user's menu page
string redirectReply();

// render the login webpage
string renderLoginPage(string sid, string errorMessage = "");
string renderMenuPage(string username);
string renderDrivePage(string username, int currentClientNumber, string dir_path = "");
string getFileName(const string &path);
string renderMailboxPage(string username, int currentClientNumber);
string renderEmailPage(string username, string item, int currentClientNumber);
string renderErrorPage(int err_code);
string generateReply(int reply_code, string username = "", string item = "", string sid = "", int currentClientNumber = 0);

#endif // RENDER_H
