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
#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>

using namespace std;

int PORT = 7000;
int UDPPORT = 7100;
int DEBUG = 0;
size_t READ_SIZE = 5;
size_t FBUFFER_SIZE = 1024;
const int MAX_CLIENTS = 100;

volatile int client_socks[MAX_CLIENTS];
volatile int num_client = 0;
volatile int shutting_down = 0;

int NOTFOUND = 404;
int FORBIDDEN = 403;

int REDIRECT = 0;
int LOGIN = 1;
int MENU = 2;
int MAILBOX = 3;
int DRIVE = 4;

int EMAIL = 6;
int SENDEMAIL = 7;
int FORWARD = 8;

int DOWNLOAD = 5;
int RENAME = 9;
int MOVE = 10;
int DELETE = 11;
int NEWDIR = 12;
int UPLOAD = 13;

int ADMIN = 20;
int SERVER = 21;

vector<string> frontend_servers = {"10000", "10001", "10002"};
vector<string> backend_servers = {"11000", "11001", "11002"};


struct sockaddr_in destSock;
struct sockaddr_in localSock;
int udpsock;



void send_msg(int port, string msg) {
	memset((char *) &destSock, 0, sizeof(destSock));
	destSock.sin_family = AF_INET;
	destSock.sin_port = htons(port);
	destSock.sin_addr.s_addr = inet_addr("127.0.0.1");

	sendto(udpsock, msg.c_str(), strlen(msg.c_str()), 0,
	      (struct sockaddr *)&destSock, sizeof(destSock));
}



string generate_cookie() {
    stringstream ss;
    time_t now = time(nullptr);
    ss << now;
    return ss.str();
}




/////////////////////////////////////
//								   //
//	   	HTTP replies & HTML        //
//								   //
/////////////////////////////////////


// redirect to the user's menu page
string redirectReply() {
	string response = "HTTP/1.1 302 Found\r\nLocation: /\r\n\r\n\r\n";
	return response;
}

// render a webpage displaying http errors
string renderErrorPage(int err_code) {

	string err = to_string(err_code);
	string err_msg = "";
	if (err_code == NOTFOUND) {
		//err = to_string(NOTFOUND);
		err_msg = "404 Not Found";
	}
	else if (err_code == FORBIDDEN) {
		//err = to_string(FORBIDDEN);
		err_msg = "403 Forbidden";
	}

	string content = "";
	content += "<html>\n";
	content += "<head><title>Error</title></head>\n";
	content += "<body>\n";
	content += "<h1>";
	content += err_msg;
	content += "</h1>\n";
	content += "</body>\n";
	content += "</html>\n";


	string header = "HTTP/1.1 " + err_msg + \
					"\r\nContent-Type: text/html\r\nContent-Length: "+ \
					to_string(content.length()) + "\r\n\r\n";
	string reply = header + content;

	return reply;
}

// render the login webpage
string renderAdminPage(string sid) {

	string content = "";

    // Start building the HTML content
	content += "<!DOCTYPE html>\n";
	content += "<html>\n";
	content += "<head>\n";
	content += "<title>Server List</title>\n";
	content += "</head>\n";
	content += "<body>\n";
	content += "<h1>Server Control Panel</h1>\n";
	content += "<ul>\n";

	// Sample list of servers
	std::vector<std::string> servers = {"Server1", "Server2", "Server3"};

	content += "<h2>Frontend</h2>\n";
	// Generate HTML list items with enable/disable forms for each server
	for (const auto& server : frontend_servers) {
		content += "<li>" + server + "\n";
		// Enable button form
		content += "<form action='http://localhost:"+ to_string(PORT) + "/" + server + "' method='post'>";
		content += "<input type='hidden' name='action' value='ENABLE'>";
		content += "<input type='submit' value='Enable'>";
		content += "</form>\n";
		// Disable button form
		content += "<form action='http://localhost:"+ to_string(PORT) + "/" + server + "' method='post'>";
		content += "<input type='hidden' name='action' value='DISABLE'>";
		content += "<input type='submit' value='Disable'>";
		content += "</form>\n";
		content += "</li>\n";
	}

	content += "<h2>Backend</h2>\n";
	for (const auto& server : backend_servers) {
		content += "<li>" + server + "\n";
		// Enable button form
		content += "<form action='http://localhost:"+ to_string(PORT) + "/" + server + "' method='post'>";
		content += "<input type='hidden' name='action' value='ENABLE'>";
		content += "<input type='submit' value='Enable'>";
		content += "</form>\n";
		// Disable button form
		content += "<form action='http://localhost:"+ to_string(PORT) + "/" + server + "' method='post'>";
		content += "<input type='hidden' name='action' value='DISABLE'>";
		content += "<input type='submit' value='Disable'>";
		content += "</form>\n";
		content += "</li>\n";
	}

	content += "</ul>\n";
	content += "</body>\n";
	content += "</html>\n";

	string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "+ \
					to_string(content.length()) + "\r\n" +\
					"Set-Cookie: sid=" + sid + \
					"\r\n\r\n";
	string reply = header + content;

	return reply;
}


string generateReply(int reply_code, string username = "", string item = "", string sid = "") {
	if (reply_code == ADMIN) {
		return renderAdminPage(sid);
	}
	if (reply_code == SERVER) {
		return renderAdminPage(sid);
	}

    string reply = renderErrorPage(reply_code);
    return reply;
}


/////////////////////////////////////
//								   //
//		Threads and Signals        //
//								   //
/////////////////////////////////////


// signal handler for SIGINT
void signal_handler(int sig) {
    if (sig == SIGINT) {
    	shutting_down = 1;

    	for (int i = 0; i < num_client; i++) {
			char msg[] = "-ERR Server shutting down\r\n";
			// set client socket to non-blocking
			fcntl(client_socks[i], F_SETFL, fcntl(client_socks[i], F_GETFL, 0) | O_NONBLOCK);
			// send shut-down message
			send(client_socks[i], msg, strlen(msg), 0);
			// close the socket
			close(client_socks[i]);
		}

        exit(0);
    }
}

// thread function for communication with clients
void *thread_worker(void *fd) {
	int sock = *(int*)fd;

	// close immediately if shutting down
	if (shutting_down) {
		close(sock);
		pthread_exit(NULL);
	}

    char buffer[READ_SIZE];
    char *dataBuffer;
    char *contentBuffer;
    size_t dataBufferSize = 0;
    //size_t content_read = 0;

    int read_header = 0;
    int read_body = 0;
    int contentlen = 0;
    string contentType = "";

    string sid = generate_cookie();
    string tmp_sid = "";

    int reply_code = NOTFOUND;

    string username = "";
    int logged_in = 0;
    // email or file item name/identifier
    string item = "";

    int server_port;



    // Try reading READ_SIZE bytes of data each time
    while (1) {

        int bytes_read = read(sock, buffer, READ_SIZE);

        // There are some data read
        if (bytes_read > 0) {
            // Add the data to the data buffer
        	dataBuffer = (char*)realloc(dataBuffer, dataBufferSize + bytes_read + 1);
            memcpy(dataBuffer + dataBufferSize, buffer, bytes_read);
            dataBufferSize += bytes_read;
            dataBuffer[dataBufferSize] = '\0';

            char *crlf;

            // if read_body, then read contentlen bytes
            if (read_body) {

            	// continue reading more bytes
				if (dataBufferSize < contentlen) {
					continue;
				}
				else {
					//char content[contentlen];
					char *content = (char *)malloc((contentlen+1) * sizeof(char));
					memcpy(content, dataBuffer, contentlen);
					content[contentlen] = '\0';

					// process the message body
					if (DEBUG) {

						for (int c = 0; c < contentlen; c++) {
							char c_tmp[1];
							strncpy(c_tmp, content+c, 1);
							c_tmp[1] = '\0';
							fprintf(stderr, "%s", c_tmp);

							// break for message body that's too long
							if (c >= 2048) {
								fprintf(stderr, "\n..............\n");
								break;
							}
						}
						fprintf(stderr, "\n");
					}

					string reply_string = generateReply(reply_code, username, item, sid);
					const char *reply = reply_string.c_str();

					if (reply_code == ADMIN) {
						send(sock, reply, strlen(reply), 0);
						if (DEBUG) {
							fprintf(stderr, "[%d] S: %s\n", sock, reply);
						}
					}

					else if (reply_code == SERVER) {
						send(sock, reply, strlen(reply), 0);
						if (DEBUG) {
							fprintf(stderr, "[%d] S: %s\n", sock, reply);
						}

						send_msg(server_port+10000, string(content+7));
						if (DEBUG) {
							fprintf(stderr, "[%d] S: sent %s to port %d\n", sock, content+7, server_port);
						}
						if (string(content+7) == "ENABLE") {
							string cmd = "./frontendserver -v -p " + to_string(server_port) + " &";
							int cmd_status = system(cmd.c_str());
							if (DEBUG) {
								fprintf(stderr, "[%d] S: starting server on port %d\n", sock, server_port);
								fprintf(stderr, "[%d] S: command status code %d\n", sock, cmd_status);
							}
						}
					}

					// clear the buffer
					memmove(dataBuffer, dataBuffer + contentlen, dataBufferSize - contentlen + 1);
					dataBuffer = (char*)realloc(dataBuffer, dataBufferSize - contentlen + 1);
					dataBufferSize -= contentlen;

					read_body = 0;
					contentlen = 0;
					//content_read = 0;
					free(content);

				}
				continue;
            }

            // look for \r\n in the data buffer
            while ((crlf = strstr(dataBuffer, "\r\n")) != NULL)  {
            	//command length excluding "/r/n"
                size_t cmdLen = crlf - dataBuffer;
                char cmd[cmdLen+1];
                strncpy(cmd, dataBuffer, cmdLen);
                cmd[cmdLen] = '\0';
                if (DEBUG) {
                	fprintf(stderr, "[%d] C: %s\n", sock, cmd);
                }

                // Reading Header lines
                if (read_header) {

                	// there is a message body
                	if (strncmp(cmd, "Content-Length: ", strlen("Content-Length: ")) == 0) {
                		int contentlen_len = strlen(cmd) - strlen("Content-Length: ");
						char contentlen_str[contentlen_len];
						strncpy(contentlen_str, cmd+strlen("Content-Length: "), contentlen_len);
						contentlen_str[contentlen_len] = '\0';
						contentlen = atoi(contentlen_str);
                	}

                	// record content type for multi-part form data
                	if (strncmp(cmd, "Content-Type: ", strlen("Content-Type: ")) == 0) {
                		int contentType_len = strlen(cmd);
						char contentType_str[contentType_len];
						strncpy(contentType_str, cmd, contentType_len);
						contentType_str[contentType_len] = '\0';
				        contentType = string(contentType_str);
					}

                	// parse cookie
                	else if (strncmp(cmd, "Cookie: ", strlen("Cookie: ")) == 0) {
						int sid_len = strlen(cmd) - strlen("Cookie: sid=");
						char sid_str[sid_len];
						strncpy(sid_str, cmd+strlen("Cookie: sid="), sid_len);
						sid_str[sid_len] = '\0';
						tmp_sid = string(sid_str);
					}

                	// headers end
                	else if (strcmp(cmd, "") == 0) {

                		read_header = 0;
                		// prepare to read the message body
                		if (contentlen > 0) {
                			contentBuffer = (char*)malloc((contentlen+1) * sizeof(char));
                			read_body = 1;
                		}

                		// no message body
                		else {

                			// send reply
                			string reply_string = generateReply(reply_code, username, item, sid);
							const char *reply = reply_string.c_str();

							send(sock, reply, strlen(reply), 0);
							if (DEBUG) {
								fprintf(stderr, "[%d] S: %s\n", sock, reply);
							}
                		}
                	}
                }


                // Process GET command
                else if (strncmp(cmd, "GET ", 4) == 0) {

                	// parse request url
                	char tmp[strlen(cmd)];
					strncpy(tmp, cmd, strlen(cmd));
					tmp[strlen(cmd)] = '\0';
					strtok(tmp, " ");
                	char *url = strtok(NULL, " ");

                	// admin page
                	if (strcmp(url, "/admin") == 0) {
                		reply_code = ADMIN;
                	}

                	// page not found
                	else {
                		reply_code = NOTFOUND;
                	}

                	// start reading headers
                	read_header = 1;
                }

                // Process POST command
				else if (strncmp(cmd, "POST ", 5) == 0) {

					// parse request url
					char tmp[strlen(cmd)];
					strncpy(tmp, cmd, strlen(cmd));
					tmp[strlen(cmd)] = '\0';
					strtok(tmp, " ");
					char *url = strtok(NULL, " ");

					// get server port
					for (const auto &p : frontend_servers) {
						if (string(url) == "/" + p) {
							reply_code = SERVER;
							server_port = stoi(p);
							break;
						}
					}

					for (const auto &p : backend_servers) {
						if (string(url) == "/" + p) {
							reply_code = SERVER;
							server_port = stoi(p);
							break;
						}
					}

					// page not found
					//else {
					//	reply_code = NOTFOUND;
					//}

					// start reading headers
					read_header = 1;
				}


                // Process Unknown command
                else {
                	char unkCmd[] = "HTTP/1.1 501 Not Implemented\r\n";
                	send(sock, unkCmd, strlen(unkCmd), 0);
                	if (DEBUG) {
						fprintf(stderr, "[%d] S: %s", sock, unkCmd);
					}
                }

                // Remove the processed command
                memmove(dataBuffer, dataBuffer + cmdLen + 2, dataBufferSize - cmdLen - 1);
                dataBuffer = (char*)realloc(dataBuffer, dataBufferSize - cmdLen - 1);
                dataBufferSize -= cmdLen + 2;
            }
        }

        else {
        	// client exit
        	free(dataBuffer);
			close(sock);
			for (int i = 0; i < MAX_CLIENTS; i++) {
				if (client_socks[i] == sock) {
					client_socks[i] = 0;
					break;
				}
			}
        	if (DEBUG) {
				fprintf(stderr, "[%d] Connection closed\n", sock);
			}
        	num_client -= 1;
			pthread_exit(NULL);
        }
    }


}


/////////////////////////////////////
//								   //
//			    Main               //
//								   //
/////////////////////////////////////

int main(int argc, char *argv[]) {
	// signal handling
	signal(SIGINT, signal_handler);

	//parse arguments
	int opt;

	while ((opt = getopt(argc, argv, "vap:")) != -1) {
		switch (opt) {
		case 'p':
			PORT = atoi(optarg);
			break;
		case 'a':
			fprintf(stderr, "Zihao Deng / zihaoden\n");
			exit(1);
		case 'v':
			DEBUG = 1;
			break;

		}
	}
	// Initialize client sockets
	for(int i = 0; i < MAX_CLIENTS; i++) {
		client_socks[i] = 0;
	}

    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    //bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    int sockopt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &sockopt, sizeof(sockopt));
    bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    listen(listen_fd, 10);

    ///////////////
	// UDP sock  //
	///////////////

    udpsock = socket(AF_INET, SOCK_DGRAM, 0);

	memset((char *) &localSock, 0, sizeof(localSock));
	localSock.sin_family = AF_INET;
	localSock.sin_port = htons(UDPPORT);
	localSock.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(udpsock, (struct sockaddr *)&localSock, sizeof(localSock));

	///////////////
	// Main loop //
	///////////////
    while (1) {
    	if (num_client >= MAX_CLIENTS) {
    		continue;
    	}
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen = sizeof(clientaddr);
        int *fd = (int*)malloc(sizeof(int));
        *fd = accept(listen_fd, (struct sockaddr*)&clientaddr, &clientaddrlen);
        //printf("Connection from %s\n", inet_ntoa(clientaddr.sin_addr));

        if (DEBUG) {
        	fprintf(stderr, "[%d] New Connection\n", *fd);
        }

        // record socket fd in the sockets array
		for(int i = 0; i < MAX_CLIENTS; i++) {
			if(client_socks[i] == 0) {
				client_socks[i] = *fd;
				pthread_t thread;
				pthread_create(&thread, NULL, thread_worker, fd);
				break;
			}
		}
		num_client += 1;

    }

    exit(0);
}
