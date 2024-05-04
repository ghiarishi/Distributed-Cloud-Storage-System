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

int PORT = 8000;
int UDPPORT = 9000;
int DEBUG = 0;
size_t READ_SIZE = 5;
const int MAX_CLIENTS = 100;

volatile int client_socks[MAX_CLIENTS];
volatile int num_client;
volatile int shutting_down = 0;


/////////////////////////////////////
//								   //
//		     Heartbeat             //
//								   //
/////////////////////////////////////

const int BUF_SIZE = 1024;
const vector<int> ports = {20000, 20001, 20002};
map<int, int> server_status = {{20000,1}, {20001,1}, {20002,1}};
map<int, int> server_load = {{20000,0}, {20001, 0}, {20002,0}};
map<int, int> port_map = {{20000,10000}, {20001, 10001}, {20002,10002}};
const int TIMEOUT_SECONDS = 5;

struct sockaddr_in localSock;
struct sockaddr_in destSocks[3];
int udpsock;
map<int, time_t> lastHeartbeat;

pthread_mutex_t mutex;

void* sendHeartbeat(void* arg) {
    const char* heartbeatMessage = "Heartbeat";
    while (true) {
        for (int i = 0; i < ports.size(); ++i) {
            sendto(udpsock, heartbeatMessage, strlen(heartbeatMessage), 0,
                   (struct sockaddr *)&destSocks[i], sizeof(destSocks[i]));
            if (DEBUG) {
            	std::cout << "Sending heartbeat to port " << ports[i] << std::endl;
            }
        }
        sleep(2); // send heartbeat
    }
}

void* receiveHeartbeat(void* arg) {
    char buffer[BUF_SIZE];
    struct sockaddr_in from;
    socklen_t fromLen = sizeof(from);

    while (true) {
        int received = recvfrom(udpsock, buffer, BUF_SIZE, 0, (struct sockaddr *)&from, &fromLen);
        if (received > 0) {
            buffer[received] = '\0';
            int fromPort = ntohs(from.sin_port);

            if (DEBUG) {
            	std::cout << "Received: " << string(buffer) << " from port " << fromPort << std::endl;
            }

            pthread_mutex_lock(&mutex);
            lastHeartbeat[fromPort] = time(NULL);
            server_status[fromPort] = 1;
            server_load[fromPort] = atoi(buffer);
            pthread_mutex_unlock(&mutex);
        }
    }
}

void* checkTimeout(void* arg) {
    while (true) {
        pthread_mutex_lock(&mutex);
        time_t currentTime = time(NULL);
        for (int port : ports) {
            if (lastHeartbeat.find(port) != lastHeartbeat.end() &&
                difftime(currentTime, lastHeartbeat[port]) > TIMEOUT_SECONDS) {

                if (DEBUG && difftime(currentTime, lastHeartbeat[port]) < TIMEOUT_SECONDS + 2) {
                    std::cout << "[WARNING]: Server on port " << port << " is disconnected." << std::endl;
                }
                //lastHeartbeat.erase(port);
                server_status[port] = 0;
            }
        }
        pthread_mutex_unlock(&mutex);
        sleep(1);
    }
}


/////////////////////////////////////
//								   //
//		Load Balancing Redirect    //
//								   //
/////////////////////////////////////


// redirect to the specified frontend server
string redirectReply(int port) {
	string response = "HTTP/1.1 302 Found\r\nLocation: http://localhost:"+ \
						to_string(port)+"\r\n\r\n\r\n";
	return response;
}

int choose_port() {
	int port = 10000;
	int min_clients = 666666;

	pthread_mutex_lock(&mutex);
	for (auto const& status : server_status) {
		if (status.second) {
			if (server_load[status.first] < min_clients) {
				port = port_map[status.first];
				min_clients = server_load[status.first];
			}
		}
	}
	pthread_mutex_unlock(&mutex);

	return port;
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

    int read_header = 0;

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

                	// headers end
                	if (strcmp(cmd, "") == 0) {

                		read_header = 0;

						// redirect to a frontend server
                		int server_port = choose_port();
						string reply_string = redirectReply(server_port);
						const char *reply = reply_string.c_str();

						send(sock, reply, strlen(reply), 0);
						if (DEBUG) {
							fprintf(stderr, "[%d] S: %s\n", sock, reply);
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

                	// start reading headers
                	read_header = 1;
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

    //////////////////////
    // Heartbeat threads //
    //////////////////////

    pthread_mutex_init(&mutex, NULL);

	udpsock = socket(AF_INET, SOCK_DGRAM, 0);

	memset((char *) &localSock, 0, sizeof(localSock));
	localSock.sin_family = AF_INET;
	localSock.sin_port = htons(UDPPORT);
	localSock.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(udpsock, (struct sockaddr *)&localSock, sizeof(localSock));

	for (int i = 0; i < ports.size(); ++i) {
		memset((char *) &destSocks[i], 0, sizeof(destSocks[i]));
		destSocks[i].sin_family = AF_INET;
		destSocks[i].sin_port = htons(ports[i]);
		destSocks[i].sin_addr.s_addr = inet_addr("127.0.0.1");
	}

	pthread_t sendThread, receiveThread, timeoutThread;
	pthread_create(&sendThread, nullptr, sendHeartbeat, nullptr);
	pthread_create(&receiveThread, nullptr, receiveHeartbeat, nullptr);
	pthread_create(&timeoutThread, nullptr, checkTimeout, nullptr);


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
