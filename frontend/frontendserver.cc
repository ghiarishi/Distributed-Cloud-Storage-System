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

using namespace std;

int PORT = 10000;
int DEBUG = 0;
size_t READ_SIZE = 100;
const int MAX_CLIENTS = 100;

volatile int client_socks[MAX_CLIENTS];
volatile int num_client;
volatile int shutting_down = 0;

int NOTFOUND = 404;
int REDIRECT = 0;
int LOGIN = 1;
int MENU = 2;
int MAILBOX = 3;
int DRIVE = 4;
int DFILE = 5;
int EMAIL = 6;


/////////////////////////////////////
//								   //
//			 Utilities             //
//								   //
/////////////////////////////////////

// Parse login data
tuple<string, string> parseLoginData(string data_str) {

    const char *data = data_str.c_str();
    // parse request url
	char tmp[strlen(data)];
	strncpy(tmp, data, strlen(data));
	tmp[strlen(data)] = '\0';
	strtok(tmp, "=");
	char *username = strtok(NULL, "&");
	strtok(NULL, "=");
	char *password = strtok(NULL, "");

    return make_tuple(string(username), string(password));
}


/////////////////////////////////////
//								   //
//	   	HTTP replies & HTML        //
//								   //
/////////////////////////////////////


// redirect to the user's menu page
string redirectReply(string username) {
	string response = "HTTP/1.1 302 Found\r\nLocation: " + username + "/menu\r\n\r\n\r\n";
	return response;
}

// render the login webpage
string renderLoginPage() {

	string content = "";
	content += "<html>\n";
	content += "<head><title>Login Page</title></head>\n";
	content += "<body>\n";
	content += "<h1>PennCloud Login</h1>\n";
	content += "<form action=\"/redirect\" method=\"post\">\n";
	content += "Username: <input type=\"text\" name=\"username\"><br>\n";
	content += "Password: <input type=\"password\" name=\"password\"><br>\n";
	content += "<input type=\"submit\" value=\"Submit\">\n";
	content += "</form>\n";
	content += "</body>\n";
	content += "</html>\n";

	string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "+ \
					to_string(content.length()) + "\r\n\r\n";
	string reply = header + content;

	return reply;
}

// render the menu webpage
string renderMenuPage(string username) {

	string content = "";
	content += "<html><body><h1>Menu</h1><ul>";
	content += "<li><a href='" + username + "/mailbox'>Mailbox</a></li>";
	content += "<li><a href='" + username + "/drive'>Drive</a></li>";
	content += "</ul></body></html>";

	string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "+ \
					to_string(content.length()) + "\r\n\r\n";
	string reply = header + content;

	return reply;
}

// render the drive webpage
// TODO: render files retrieved from backend
string renderDrivePage(string username) {

	string content = "";
	content += "<html><head><title>File List</title></head><body>";
	content += "<h1>PennCloud Drive</h1>";
	content += "<ul>";
	content += "<li><a href='" + username + "/drive/document1.txt'>document1.txt</a></li>";
	content += "<li><a href='" + username + "/drive/document2.pdf'>document2.pdf</a></li>";
	content += "<li><a href='" + username + "/drive/image1.png'>image1.png</a></li>";
	content += "<li><a href='" + username + "/drive/presentation1.pptx'>presentation1.pptx</a></li>";
	content += "<li><a href='" + username + "/drive/spreadsheet1.xlsx'>spreadsheet1.xlsx</a></li>";
	content += "</ul>";
	content += "<p>Click on a file to view details or download.</p>";
	content += "</body></html>";

	string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "+ \
						to_string(content.length()) + "\r\n\r\n";
	string reply = header + content;

	return reply;
}

// render the mailbox webpage
// TODO: render emails retrieved from the backend
string renderMailboxPage(string username) {

	string content = "";
	content += "<html><head><title>File List</title></head><body>";
	content += "<h1>PennCloud Mailbox</h1>";
	content += "<ul>";
	content += "<li><a href='" + username + "/mailbox/send'>send</a></li>";
	content += "<li><a href='" + username + "/mailbox/email_1'>email_1</a></li>";
	content += "<li><a href='" + username + "/mailbox/email_2'>email_2</a></li>";
	content += "</ul>";
	content += "<p>Click to view or send an email</p>";
	content += "</body></html>";

	string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "+ \
						to_string(content.length()) + "\r\n\r\n";
	string reply = header + content;

	return reply;
}

// render the email content page for an email (item)
string renderEmailPage(string username, string item) {

	string content = "";
	if (item == "send") {
		content += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
		content += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
		content += "<title>Send Email</title>";
		content += "<style>body { font-family: Arial, sans-serif; } textarea { width: 100%; height: 150px; }</style></head><body>";
		content += "<h1>PennCloud Email</h1>";
		content += "<form action='/send-email' method='POST'>";
		content += "<p><strong>To:</strong> <input type='email' name='to' required></p>";
		content += "<p><strong>Subject:</strong> <input type='text' name='subject' required></p>";
		content += "<p><strong>Message:</strong></p><textarea name='message' required></textarea>";
		content += "<button type='submit'>Send Email</button></form></body></html>";
	}
	else {
		content += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
		content += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
		content += "<title>Email Viewer</title>";
		content += "<style>body { font-family: Arial, sans-serif; }";
		content += "#email-content { background-color: #f8f8f8; padding: 20px; margin-bottom: 20px; }";
		content += "textarea { width: 100%; height: 150px; }</style></head><body>";
		content += "<h1>PennCloud Email</h1>";
		content += "<div id='email-content'>";
		content += "<p><strong>From:</strong> sender@example.com</p>";
		content += "<p><strong>Subject:</strong> Test Email</p>";
		content += "<p>Hello, this is a sample email content displayed here.</p></div>";
		content += "<h2>Reply or Forward</h2>";
		content += "<form action='/send-email' method='POST'>";
		content += "<p><strong>To:</strong> <input type='email' name='to' required></p>";
		content += "<p><strong>Subject:</strong> <input type='text' name='subject' value='Re: Test Email' required></p>";
		content += "<p><strong>Message:</strong></p><textarea name='message' required></textarea>";
		content += "<button type='submit'>Send</button></form></body></html>";
	}

	string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "+ \
						to_string(content.length()) + "\r\n\r\n";
	string reply = header + content;

	return reply;
}

// send the file to the client for download
// TODO: modify this to send the actual file data stored in the backend
string sendFile(string username, string item) {

	string content = "";
	content += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
	content += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
	content += "<title>Email Viewer</title>";
	content += "<style>body { font-family: Arial, sans-serif; }";
	content += "#email-content { background-color: #f8f8f8; padding: 20px; margin-bottom: 20px; }";
	content += "textarea { width: 100%; height: 150px; }</style></head><body>";
	content += "<h1>Email Content</h1>";
	content += "<div id='email-content'>";
	content += "<p><strong>From:</strong> sender@example.com</p>";
	content += "<p><strong>Subject:</strong> Test Email</p>";
	content += "<p>Hello, this is a sample email content displayed here.</p></div>";
	content += "<h2>Reply or Forward</h2>";
	content += "<form action='/send-email' method='POST'>";
	content += "<p><strong>To:</strong> <input type='email' name='to' required></p>";
	content += "<p><strong>Subject:</strong> <input type='text' name='subject' value='Re: Test Email' required></p>";
	content += "<p><strong>Message:</strong></p><textarea name='message' required></textarea>";
	content += "<button type='submit'>Send</button></form></body></html>";

	string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "+ \
						to_string(content.length()) + "\r\n\r\n";
	string reply = header + content;

	return reply;
}


// render a webpage displaying http errors
string renderErrorPage(int err_code) {

	string err = to_string(err_code);
	string err_msg = "";
	if (err_code == 404) {
		err = to_string(404);
		err_msg = "404 Not Found";
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


string generateReply(int reply_code, string username = "", string item = "") {
	if (reply_code == LOGIN) {
		return renderLoginPage();
	}
	else if (reply_code == MENU) {
		return renderMenuPage(username);
	}
	else if (reply_code == REDIRECT) {
		return redirectReply(username);
	}
	else if (reply_code == DRIVE) {
		return renderDrivePage(username);
	}
	else if (reply_code == MAILBOX) {
		return renderMailboxPage(username);
	}
	else if (reply_code == DFILE) {
		return sendFile(username, item);
	}
	else if (reply_code == EMAIL) {
		return renderEmailPage(username, item);
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

    int reply_code = NOTFOUND;

    string username = "";
    // email or file item name/identifier
    string item = "";



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
					char content[contentlen];
					strncpy(content, dataBuffer, contentlen);
					content[contentlen] = '\0';

					// process the message body
					if (DEBUG) {
						fprintf(stderr, "[%d] C: %s\n", sock, content);
					}

					// redirect to user-specific menu webpage
					if (reply_code == REDIRECT) {
						tuple<string, string> credentials = parseLoginData(string(content));
						username += get<0>(credentials);
					}

					// send reply
					string reply_string = generateReply(reply_code, username, item);
					const char *reply = reply_string.c_str();

					send(sock, reply, strlen(reply), 0);
					if (DEBUG) {
						fprintf(stderr, "[%d] S: %s\n", sock, reply);
					}

					// clear the buffer
					memmove(dataBuffer, dataBuffer + contentlen, dataBufferSize - contentlen + 1);
					dataBuffer = (char*)realloc(dataBuffer, dataBufferSize - contentlen + 1);
					dataBufferSize -= contentlen;

					read_body = 0;
					contentlen = 0;
					//content_read = 0;

				}
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
                		int contentlen_len = strlen(dataBuffer) - strlen("Content-Length: ");
						char contentlen_str[contentlen_len];
						strncpy(contentlen_str, cmd+strlen("Content-Length: "), contentlen_len);
						contentlen_str[contentlen_len] = '\0';
						contentlen = atoi(contentlen_str);
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
                			string reply_string = generateReply(reply_code, username, item);
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

                	// login page
                	if (strcmp(url, "/") == 0) {
                		reply_code = LOGIN;
                	}

                	// menu page
                	else if (strcmp(url+strlen(url)-strlen("/menu"), "/menu") == 0) {
                		username = string(url).substr(0, strlen(url)-strlen("/menu"));
						reply_code = MENU;
					}

                	// mailbox page
                	else if (strcmp(url+strlen(url)-strlen("/mailbox"), "/mailbox") == 0) {
						reply_code = MAILBOX;
					}

                	// drive page
					else if (strcmp(url+strlen(url)-strlen("/drive"), "/drive") == 0) {
						reply_code = DRIVE;
					}

                	// drive file download
					else if (strstr(url, "/drive") != NULL) {
						char *pos = strstr(url, "/drive");
						char *fname_ptr = pos+strlen("/drive/");
						item = string(fname_ptr);
						reply_code = DFILE;
					}

                	// email content page
					else if (strstr(url, "/mailbox") != NULL) {
						char *pos = strstr(url, "/mailbox");
						char *fname_ptr = pos+strlen("/mailbox/");
						item = string(fname_ptr);
						reply_code = EMAIL;
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

					// redirect to menu page
					if (strcmp(url, "/redirect") == 0) {
						reply_code = REDIRECT;
					}


					// page not found
					else {
						reply_code = NOTFOUND;
					}

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
        } else {
            continue;
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
