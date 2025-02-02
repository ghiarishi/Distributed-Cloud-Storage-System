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
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

using namespace std;

struct BackendServerInfo
{
	string ip;
	int tcpPort;			// communication with frontend port
	int udpPort;			// heartbeat port
	int udpPort2;			// status message port
	int tcpPort2;			// enable disable port
	bool isPrimary = false; // secondary by default
	bool isDead = true;		// dead by default
	int replicaGroup;
};

struct FrontendServerInfo
{
	string ip;
	int tcpPort; // communication with client
	int udpPort; // heartbeat/admin port
	bool isDead = true;
};

typedef map<int, vector<BackendServerInfo>> BackendServerMap;
BackendServerMap backend_servers;

typedef map<int, FrontendServerInfo> FrontendServerMap;
FrontendServerMap frontend_servers;

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
int FRONTSERVER = 21;
int BACKSERVER = 22;
int STORAGE = 23;
int VALUE = 24;

struct sockaddr_in destSock;
struct sockaddr_in localSock;
int udpsock;

bool sendToSocket(int backend_sock, string command)
{
	if (send(backend_sock, command.c_str(), command.length(), 0) < 0)
	{
		cerr << "Error sending data to backend server" << std::endl;
		return false;
	}
	return true;
}

string readFromSocket(int backend_sock)
{
	string response;
	char buffer[4096];

	while (true)
	{
		memset(buffer, 0, sizeof(buffer));

		int bytesReceived = recv(backend_sock, buffer, sizeof(buffer), 0);
		if (bytesReceived < 0)
		{
			cerr << "Error receiving response from server" << std::endl;
			return "";
		}
		// printf("the buffer is %s\n", buffer);

		response.append(buffer, bytesReceived);

		// Check if "\r\n" is present in the received data
		size_t found = response.find("\r\n");
		if (found != std::string::npos)
		{
			break; // Exit loop if "\r\n" is found
		}
	}

	return response;
}

int connectToStorageNode(int port)
{
	int storage_sock;
	struct sockaddr_in server_addr;

	// Open master socket
	if ((storage_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		std::cerr << "Error creating socket" << std::endl;
		return -1;
	}

	// Server address
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	// Connect to server
	if (connect(storage_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		std::cerr << "Error connecting to server" << std::endl;
		return -1;
	}

	DEBUG ? printf("Connected to Server\n") : 0;
	return storage_sock;
}

string decodeURIComponent(const string &s)
{
	string result;
	for (size_t i = 0; i < s.length(); ++i)
	{
		if (s[i] == '%')
		{
			int val;
			istringstream is(s.substr(i + 1, 2));
			if (is >> std::hex >> val)
			{
				result += static_cast<char>(val);
				i += 2;
			}
		}
		else if (s[i] == '+')
		{
			result += ' ';
		}
		else
		{
			result += s[i];
		}
	}
	return result;
}

map<string, string> parseQuery(const string &query)
{
	map<string, string> data;
	istringstream paramStream(query);
	string pair;

	while (getline(paramStream, pair, '&'))
	{
		size_t eq = pair.find('=');
		string key = pair.substr(0, eq);
		string value = pair.substr(eq + 1);
		data[decodeURIComponent(key)] = decodeURIComponent(value);
	}

	return data;
}

vector<pair<string, string>> get_storage_content(int port)
{
	// pair<string, string> p1 = make_pair("rowkey_1", "colkey_1");
	// pair<string, string> p2 = make_pair("rowkey_2", "colkey_2");
	// pair<string, string> p3 = make_pair("rowkey_3", "colkey_3");
	// vector<pair<string, string>> all_pairs({p1, p2, p3});
	int storage_socket = connectToStorageNode(port);
	printf("storage_socket is %d\n", storage_socket);
	string command = "GETALL 0\r\n";
	sendToSocket(storage_socket, command);
	printf("test\n");
	string response = readFromSocket(storage_socket);
	printf("response is %s\n", response.c_str());
	// Remove the first line which contains "+OK"
	response = response.substr(response.find("\n") + 1);

	istringstream iss(response);
	string line;

	vector<pair<string, string>> all_pairs;

	while (getline(iss, line))
	{
		size_t pos = line.find(",");
		if (pos != string::npos)
		{
			string key = line.substr(0, pos);
			string value = line.substr(pos + 1);
			all_pairs.push_back(make_pair(key, value));
		}
	}

	// Printing the pairs
	for (const auto &p : all_pairs)
	{
		cout << "pair<string, string> p = make_pair(\"" << p.first << "\", \"" << p.second << "\");" << endl;
	}

	return all_pairs;
}

string get_value(int port, string rowkey, string colkey)
{
	int storage_socket = connectToStorageNode(port);
	printf("storage_socket is %d\n", storage_socket);
	string command = "GET " + rowkey + "," + colkey + "\r\n";
	sendToSocket(storage_socket, command);
	string response = readFromSocket(storage_socket);
	printf("response is %s\n", response.c_str());
	return response.substr(4);
}

string readFromBackendSocket(int backend_sock)
{
	string response;
	char buffer[4096];

	while (true)
	{
		memset(buffer, 0, sizeof(buffer));

		int bytesReceived = recv(backend_sock, buffer, sizeof(buffer), 0);
		if (bytesReceived < 0)
		{
			cerr << "Error receiving response from server" << std::endl;
			return "";
		}
		printf("the buffer is %s\n", buffer);

		response.append(buffer, bytesReceived);

		// Check if "\r\n" is present in the received data
		size_t found = response.find("\r\n");
		if (found != std::string::npos)
		{
			break; // Exit loop if "\r\n" is found
		}
	}

	return response;
}

int connectToMaster()
{
	int master_sock;
	struct sockaddr_in server_addr;

	// Open master socket
	if ((master_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		std::cerr << "Error creating socket" << std::endl;
		return -1;
	}

	// Server address
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(3000);
	server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	// Connect to server
	if (connect(master_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		std::cerr << "Error connecting to server" << std::endl;
		return -1;
	}

	DEBUG ? printf("Connected to Server\n") : 0;

	return master_sock;
}

// Function to decode Base64 to binary
vector<char> base64Decode(const string &encoded_data)
{
	// Create a BIO chain for Base64 decoding
	BIO *bio = BIO_new_mem_buf(encoded_data.data(), encoded_data.length());
	BIO *base64 = BIO_new(BIO_f_base64());
	BIO_set_flags(base64, BIO_FLAGS_BASE64_NO_NL);
	bio = BIO_push(base64, bio);

	// Prepare to read the decoded data
	vector<char> decoded_data(encoded_data.length()); // Allocate enough space
	int decoded_length = BIO_read(bio, decoded_data.data(), decoded_data.size());

	if (decoded_length < 0)
	{
		// Handle the case where decoding fails
		cerr << "Error decoding Base64 string." << endl;
		decoded_data.clear();
	}
	else
	{
		// Resize the vector to the actual decoded length
		decoded_data.resize(decoded_length);
	}

	// Clean up
	BIO_free_all(bio);

	return decoded_data;
}

string getConfigFile(int master_sock)
{

	// Send command to server
	string command = "REQUEST\r\n";
	if (send(master_sock, command.c_str(), command.length(), 0) < 0)
	{
		std::cerr << "Error sending command to server" << std::endl;
		return "-ERR Error sending command to server";
	}

	DEBUG ? printf("Sent command to Server\n") : 0;

	// Receive server info
	string encodedConfig = readFromBackendSocket(master_sock);
	vector<char> decodedConfigVec = base64Decode(encodedConfig);
	string decodedConfig(decodedConfigVec.begin(), decodedConfigVec.end());

	printf("decoded config is : %s\n", decodedConfig.c_str());
	return decodedConfig;
}

void parseBackendServers(const std::string &fileContents, BackendServerMap &servers)
{
	std::istringstream fileStream(fileContents);
	std::string line;

	while (std::getline(fileStream, line))
	{
		std::istringstream iss(line);
		std::vector<std::string> parts;
		std::string part;
		while (std::getline(iss, part, ','))
		{
			size_t pos = part.find(':');
			if (pos != std::string::npos)
			{
				parts.push_back(part.substr(pos + 1));
			}
			else
			{
				std::cerr << "Invalid format in part: " << part << std::endl;
			}
		}

		if (parts.size() != 6)
		{
			std::cerr << "Invalid line format: " << line << std::endl;
			// skip malformed lines
			continue;
		}

		BackendServerInfo info;

		info.replicaGroup = std::stoi(parts[0]);
		info.ip = parts[1];
		info.tcpPort = std::stoi(parts[2]);
		info.udpPort = std::stoi(parts[3]);
		info.udpPort2 = std::stoi(parts[4]);
		info.tcpPort2 = std::stoi(parts[5]);

		servers[info.replicaGroup].push_back(info);
	}
}

void parseFrontendServers(const string &filename, FrontendServerMap &servers)
{
	ifstream file(filename);
	if (!file.is_open())
	{
		cerr << "Error opening file" << endl;
		return;
	}

	string line;
	int id = 1;
	while (getline(file, line))
	{
		istringstream iss(line);
		vector<string> parts;
		string part;
		while (getline(iss, part, ','))
		{
			size_t pos = part.find(':');
			if (pos != string::npos)
			{
				parts.push_back(part.substr(pos + 1));
			}
			else
			{
				cerr << "Invalid format in part: " << part << endl;
			}
		}

		if (parts.size() != 3)
		{
			cerr << "Invalid line format: " << line << endl;
			// skip malformed lines
			continue;
		}

		FrontendServerInfo info;

		info.ip = parts[0];
		info.tcpPort = stoi(parts[1]);
		info.udpPort = stoi(parts[2]);

		servers[id] = info;

		id++;
	}
}

void send_msg_udp(int port, string msg)
{
	memset((char *)&destSock, 0, sizeof(destSock));
	destSock.sin_family = AF_INET;
	destSock.sin_port = htons(port);
	destSock.sin_addr.s_addr = inet_addr("127.0.0.1");

	sendto(udpsock, msg.c_str(), strlen(msg.c_str()), 0,
		   (struct sockaddr *)&destSock, sizeof(destSock));
}

string generate_cookie()
{
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
string redirectReply()
{
	string response = "HTTP/1.1 302 Found\r\nLocation: /\r\n\r\n\r\n";
	return response;
}

// render a webpage displaying http errors
string renderErrorPage(int err_code)
{

	string err = to_string(err_code);
	string err_msg = "";
	if (err_code == NOTFOUND)
	{
		// err = to_string(NOTFOUND);
		err_msg = "404 Not Found";
	}
	else if (err_code == FORBIDDEN)
	{
		// err = to_string(FORBIDDEN);
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

	string header = "HTTP/1.1 " + err_msg +
					"\r\nContent-Type: text/html\r\nContent-Length: " +
					to_string(content.length()) + "\r\n\r\n";
	string reply = header + content;

	return reply;
}

// render the admin webpage
string renderAdminPage(string sid)
{
	string content = "";

	// Start building the HTML content
	content += "<!DOCTYPE html>\n";
	content += "<html>\n";
	content += "<head>\n";
	content += "<title>Server Control Panel</title>\n";
	content += "<style>\n";
	content += "body { font-family: Arial, sans-serif; margin: 0; padding: 0; background-color: #f4f4f4; }\n";
	content += ".header { padding: 20px; text-align: center; background-color: #007BFF; color: white; }\n";
	content += ".header h1 { margin: 0; font-size: 2.5em; }\n";
	content += ".container { padding: 20px; max-width: 900px; margin: auto; background-color: white; border-radius: 8px; box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1); }\n";
	content += ".section-title { margin-top: 30px; font-size: 1.5em; border-bottom: 2px solid #007BFF; padding-bottom: 10px; }\n";
	content += "ul { list-style: none; padding: 0; }\n";
	content += "li { margin-bottom: 15px; padding: 15px; border: 1px solid #ddd; border-radius: 5px; background-color: #f9f9f9; }\n";
	content += "form { display: inline-block; margin-right: 10px; }\n";
	content += "input[type='submit'] { background-color: #007BFF; color: white; border: none; padding: 8px 12px; border-radius: 4px; cursor: pointer; }\n";
	content += "input[type='submit']:hover { background-color: #0056b3; }\n";
	content += "</style>\n";
	content += "</head>\n";
	content += "<body>\n";
	content += "<div class='header'>\n";
	content += "<h1>Server Control Panel</h1>\n";
	content += "</div>\n";
	content += "<div class='container'>\n";
	content += "<div class='section-title'>Frontend</div>\n";
	content += "<ul>\n";

	// Generate HTML list items with enable/disable forms for each frontend server
	for (const auto &server_info : frontend_servers)
	{
		string server = to_string(server_info.second.udpPort);
		string status = server_info.second.isDead ? "Dead" : "Alive";
		content += "<li>UDP Port: " + server + " (Status: " + status + ")\n";
		content += "<form action='http://localhost:" + to_string(PORT) + "/" + server + "' method='post'>\n";
		content += "<input type='hidden' name='action' value='ENABLE'>\n";
		content += "<input type='submit' value='Enable'>\n";
		content += "</form>\n";
		content += "<form action='http://localhost:" + to_string(PORT) + "/" + server + "' method='post'>\n";
		content += "<input type='hidden' name='action' value='DISABLE'>\n";
		content += "<input type='submit' value='Disable'>\n";
		content += "</form>\n";
		content += "</li>\n";
	}

	content += "</ul>\n";
	content += "<div class='section-title'>Backend</div>\n";
	content += "<ul>\n";

	// Generate HTML list items with enable/disable forms for each backend server
	for (const auto &server_info : backend_servers)
	{
		if (server_info.first == 0)
		{
			content += "<h3>Master</h3>\n";
		}
		else
		{
			content += "<h3>Replica " + to_string(server_info.first) + "</h3>\n";
		}
		for (const auto &replica_info : server_info.second)
		{
			string server = to_string(replica_info.tcpPort2);
			string status = replica_info.isDead ? "Dead" : "Alive";
			content += "<li>TCP Port2: " + server + " (Status: " + status + ")\n";
			content += "<form action='http://localhost:" + to_string(PORT) + "/" + server + "' method='post'>\n";
			content += "<input type='hidden' name='action' value='ENABLE'>\n";
			content += "<input type='submit' value='Enable'>\n";
			content += "</form>\n";
			content += "<form action='http://localhost:" + to_string(PORT) + "/" + server + "' method='post'>\n";
			content += "<input type='hidden' name='action' value='DISABLE'>\n";
			content += "<input type='submit' value='Disable'>\n";
			content += "</form>\n";
			content += "<form action='http://localhost:" + to_string(PORT) + "/" + server + "' method='post'>\n";
			content += "<input type='hidden' name='action' value='VIEW'>\n";
			content += "<input type='submit' value='View'>\n";
			content += "</form>\n";
			content += "</li>\n";
		}
	}

	content += "</ul>\n";
	content += "</div>\n";
	content += "</body>\n";
	content += "</html>\n";

	string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
					to_string(content.length()) + "\r\n" +
					"Set-Cookie: sid=" + sid +
					"\r\n\r\n";
	string reply = header + content;

	return reply;
}


string renderStoragePage(string sid, int backend_port)
{

	vector<pair<string, string>> all_pairs = get_storage_content(backend_port);

	string content = "";

	content += "<!DOCTYPE html>\n";
	content += "<html lang=\"en\">\n";
	content += "<head>\n";
	content += "    <meta charset=\"UTF-8\">\n";
	content += "    <title>Key-Value Display</title>\n";
	content += "    <style>\n";
	content += "        body {\n";
	content += "            font-family: Arial, sans-serif;\n";
	content += "            margin: 20px;\n";
	content += "        }\n";
	content += "        .key-value {\n";
	content += "            margin: 5px 0;\n";
	content += "        }\n";
	content += "        .button {\n";
	content += "            padding: 5px 10px;\n";
	content += "            margin-left: 10px;\n";
	content += "            background-color: #007bff;\n";
	content += "            color: white;\n";
	content += "            border: none;\n";
	content += "            cursor: pointer;\n";
	content += "        }\n";
	content += "    </style>\n";
	content += "</head>\n";
	content += "<body>\n";
	content += "    <h1>Key-Value Display</h1>\n";
	content += "    <div id=\"keyValueList\">\n";
	for (const auto &p : all_pairs)
	{
		content += "        <form action=\"/get-value\" method=\"POST\" class=\"key-value\">\n";
		content += "            <input type=\"hidden\" name=\"key\" value=\"" + p.first + "\">\n";
		content += "            <input type=\"hidden\" name=\"value\" value=\"" + p.second + "\">\n";
		content += "            <strong>" + p.first + ":</strong> " + p.second + "\n";
		content += "            <button type=\"submit\" class=\"button\" name=\"get-value\">Get-Value</button>\n";
		content += "        </form>\n";
	}

	content += "    </div>\n";
	content += "</body>\n";
	content += "</html>\n";

	string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
					to_string(content.length()) + "\r\n" +
					"Set-Cookie: sid=" + sid +
					"\r\n\r\n";
	string reply = header + content;

	return reply;
}

string renderValuePage(string sid, int backend_port, string rowkey, string colkey)
{

	string data = get_value(backend_port, rowkey, colkey);

	string content = "";

	content += "<!DOCTYPE html>\n";
	content += "<html lang=\"en\">\n";
	content += "<head>\n";
	content += "    <meta charset=\"UTF-8\">\n";
	content += "    <title>Value Display</title>\n";
	content += "    <style>\n";
	content += "        body {\n";
	content += "            font-family: Arial, sans-serif;\n";
	content += "            margin: 20px;\n";
	content += "        }\n";
	content += "    </style>\n";
	content += "</head>\n";
	content += "<body>\n";
	content += "    <h1>Value Display</h1>\n";
	content += "    <p>" + data + "</p>\n";
	content += "</body>\n";
	content += "</html>\n";

	string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
					to_string(content.length()) + "\r\n" +
					"Set-Cookie: sid=" + sid +
					"\r\n\r\n";
	string reply = header + content;

	return reply;
}

string generateReply(int reply_code, int server_port, string rowkey, string colkey, string sid = "")
{
	if (reply_code == ADMIN)
	{
		return renderAdminPage(sid);
	}
	if (reply_code == FRONTSERVER)
	{
		return renderAdminPage(sid);
	}
	if (reply_code == BACKSERVER)
	{
		return renderAdminPage(sid);
	}
	if (reply_code == STORAGE)
	{
		return renderStoragePage(sid, server_port);
	}
	if (reply_code == VALUE)
	{
		return renderValuePage(sid, server_port, rowkey, colkey);
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
void signal_handler(int sig)
{
	if (sig == SIGINT)
	{
		shutting_down = 1;

		for (int i = 0; i < num_client; i++)
		{
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
void *thread_worker(void *fd)
{
	int sock = *(int *)fd;

	// close immediately if shutting down
	if (shutting_down)
	{
		close(sock);
		pthread_exit(NULL);
	}

	char buffer[READ_SIZE];
	char *dataBuffer = (char *)malloc(1); // Allocate memory for at least 1 character
	dataBuffer[0] = '\0';				  // Null-terminate the stringƒ
	char *contentBuffer;
	size_t dataBufferSize = 0;
	// size_t content_read = 0;

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
	string rowkey = "";
	string colkey = "";

	int server_port;

	// Try reading READ_SIZE bytes of data each time
	while (1)
	{

		int bytes_read = read(sock, buffer, READ_SIZE);

		// There are some data read
		if (bytes_read > 0)
		{
			// Add the data to the data buffer
			dataBuffer = (char *)realloc(dataBuffer, dataBufferSize + bytes_read + 1);
			memcpy(dataBuffer + dataBufferSize, buffer, bytes_read);
			dataBufferSize += bytes_read;
			dataBuffer[dataBufferSize] = '\0';

			char *crlf;

			// if read_body, then read contentlen bytes
			if (read_body)
			{

				// continue reading more bytes
				if (dataBufferSize < contentlen)
				{
					continue;
				}
				else
				{
					// char content[contentlen];
					char *content = (char *)malloc((contentlen + 1) * sizeof(char));
					memcpy(content, dataBuffer, contentlen);
					content[contentlen] = '\0';

					// process the message body
					if (DEBUG)
					{

						for (int c = 0; c < contentlen; c++)
						{
							char c_tmp[1];
							strncpy(c_tmp, content + c, 1);
							c_tmp[1] = '\0';
							fprintf(stderr, "%s", c_tmp);

							// break for message body that's too long
							if (c >= 2048)
							{
								fprintf(stderr, "\n..............\n");
								break;
							}
						}
						fprintf(stderr, "\n");
					}

					if (string(content + 7) == "VIEW")
					{
						reply_code = STORAGE;
					}

					else if (reply_code == FRONTSERVER)
					{

						send_msg_udp(server_port, string(content + 7));
						if (DEBUG)
						{
							fprintf(stderr, "[%d] S: sent %s to port %d\n", sock, content + 7, server_port);
						}
						if (string(content + 7) == "ENABLE")
						{
							string cmd = "./frontendserver -v -p " + to_string(server_port - 10000) + " &";
							int cmd_status = system(cmd.c_str());
							for (auto it = frontend_servers.begin(); it != frontend_servers.end(); ++it)
							{
								printf("port is %d and updPort is %d\n", server_port, it->second.udpPort);
								if (to_string(server_port) == to_string(it->second.udpPort))
								{
									it->second.isDead = false;
									break;
								}
							}
							if (DEBUG)
							{
								fprintf(stderr, "[%d] S: starting server on port %d\n", sock, server_port);
								fprintf(stderr, "[%d] S: command status code %d\n", sock, cmd_status);
							}
						}
						else
						{
							for (auto it = frontend_servers.begin(); it != frontend_servers.end(); ++it)
							{
								printf("port is %d and updPort is %d\n", server_port, it->second.udpPort);
								if (to_string(server_port) == to_string(it->second.udpPort))
								{
									it->second.isDead = true;
									break;
								}
							}
						}
					}
					else if (reply_code == BACKSERVER)
					{
						int storageNode_sock = connectToStorageNode(server_port);
						sendToSocket(storageNode_sock, string(content + 7) + "\r\n");
						string response = readFromSocket(storageNode_sock);
					}

					else if (reply_code == VALUE)
					{
						map<string, string> msg_map = parseQuery(string(content));
						rowkey = msg_map["key"];
						colkey = msg_map["value"];
						printf("rowkey is %s\n", rowkey.c_str());
						printf("colkey is %s\n", colkey.c_str());
					}

					string reply_string = generateReply(reply_code, server_port, rowkey, colkey, sid);
					const char *reply = reply_string.c_str();
					send(sock, reply, strlen(reply), 0);
					if (DEBUG)
					{
						fprintf(stderr, "[%d] S: %s\n", sock, reply);
					}

					// clear the buffer
					memmove(dataBuffer, dataBuffer + contentlen, dataBufferSize - contentlen + 1);
					dataBuffer = (char *)realloc(dataBuffer, dataBufferSize - contentlen + 1);
					dataBufferSize -= contentlen;

					read_body = 0;
					contentlen = 0;
					// content_read = 0;
					free(content);
				}
				continue;
			}

			// look for \r\n in the data buffer
			while ((crlf = strstr(dataBuffer, "\r\n")) != NULL)
			{
				// command length excluding "/r/n"
				size_t cmdLen = crlf - dataBuffer;
				char cmd[cmdLen + 1];
				strncpy(cmd, dataBuffer, cmdLen);
				cmd[cmdLen] = '\0';
				if (DEBUG)
				{
					fprintf(stderr, "[%d] C: %s\n", sock, cmd);
				}

				// Reading Header lines
				if (read_header)
				{

					// there is a message body
					if (strncmp(cmd, "Content-Length: ", strlen("Content-Length: ")) == 0)
					{
						int contentlen_len = strlen(cmd) - strlen("Content-Length: ");
						char contentlen_str[contentlen_len];
						strncpy(contentlen_str, cmd + strlen("Content-Length: "), contentlen_len);
						contentlen_str[contentlen_len] = '\0';
						contentlen = atoi(contentlen_str);
					}

					// record content type for multi-part form data
					if (strncmp(cmd, "Content-Type: ", strlen("Content-Type: ")) == 0)
					{
						int contentType_len = strlen(cmd);
						char contentType_str[contentType_len];
						strncpy(contentType_str, cmd, contentType_len);
						contentType_str[contentType_len] = '\0';
						contentType = string(contentType_str);
					}

					// parse cookie
					else if (strncmp(cmd, "Cookie: ", strlen("Cookie: ")) == 0)
					{
						int sid_len = strlen(cmd) - strlen("Cookie: sid=");
						char sid_str[sid_len];
						strncpy(sid_str, cmd + strlen("Cookie: sid="), sid_len);
						sid_str[sid_len] = '\0';
						tmp_sid = string(sid_str);
					}

					// headers end
					else if (strcmp(cmd, "") == 0)
					{

						read_header = 0;
						// prepare to read the message body
						if (contentlen > 0)
						{
							contentBuffer = (char *)malloc((contentlen + 1) * sizeof(char));
							read_body = 1;
						}

						// no message body
						else
						{

							// send reply
							string reply_string = generateReply(reply_code, server_port, rowkey, colkey, sid);
							const char *reply = reply_string.c_str();

							send(sock, reply, strlen(reply), 0);
							if (DEBUG)
							{
								fprintf(stderr, "[%d] S: %s\n", sock, reply);
							}
						}
					}
				}

				// Process GET command
				else if (strncmp(cmd, "GET ", 4) == 0)
				{

					// parse request url
					char tmp[strlen(cmd)];
					strncpy(tmp, cmd, strlen(cmd));
					tmp[strlen(cmd)] = '\0';
					strtok(tmp, " ");
					char *url = strtok(NULL, " ");

					// admin page
					if (strcmp(url, "/admin") == 0)
					{
						reply_code = ADMIN;
					}

					// page not found
					else
					{
						reply_code = NOTFOUND;
					}

					// start reading headers
					read_header = 1;
				}

				// Process POST command
				else if (strncmp(cmd, "POST ", 5) == 0)
				{

					// parse request url
					char tmp[strlen(cmd)];
					strncpy(tmp, cmd, strlen(cmd));
					tmp[strlen(cmd)] = '\0';
					strtok(tmp, " ");
					char *url = strtok(NULL, " ");

					// get server port
					for (const auto &s : frontend_servers)
					{
						if (string(url) == "/" + to_string(s.second.udpPort))
						{
							reply_code = FRONTSERVER;
							server_port = s.second.udpPort;
							break;
						}
					}

					for (const auto &ss : backend_servers)
					{
						for (const auto &s : ss.second)
						{
							if (string(url) == "/" + to_string(s.tcpPort2))
							{
								reply_code = BACKSERVER;
								server_port = s.tcpPort2;
								break;
							}
						}
					}

					if (strcmp(url, "/get-value") == 0)
					{
						reply_code = VALUE;
					}

					// page not found
					// else {
					//	reply_code = NOTFOUND;
					//}

					// start reading headers
					read_header = 1;
				}

				// Process Unknown command
				else
				{
					char unkCmd[] = "HTTP/1.1 501 Not Implemented\r\n";
					send(sock, unkCmd, strlen(unkCmd), 0);
					if (DEBUG)
					{
						fprintf(stderr, "[%d] S: %s", sock, unkCmd);
					}
				}

				// Remove the processed command
				memmove(dataBuffer, dataBuffer + cmdLen + 2, dataBufferSize - cmdLen - 1);
				dataBuffer = (char *)realloc(dataBuffer, dataBufferSize - cmdLen - 1);
				dataBufferSize -= cmdLen + 2;
			}
		}

		else
		{
			// client exit
			free(dataBuffer);
			close(sock);
			for (int i = 0; i < MAX_CLIENTS; i++)
			{
				if (client_socks[i] == sock)
				{
					client_socks[i] = 0;
					break;
				}
			}
			if (DEBUG)
			{
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

int main(int argc, char *argv[])
{
	// signal handling
	signal(SIGINT, signal_handler);

	// parse arguments
	int opt;

	while ((opt = getopt(argc, argv, "vap:")) != -1)
	{
		switch (opt)
		{
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

	/////////////////
	// Read Config //
	/////////////////
	int master_sock = connectToMaster();
	string configFile = getConfigFile(master_sock);

	string frontend_config_path = "frontendConfig.txt";
	parseBackendServers(configFile, backend_servers);
	parseFrontendServers(frontend_config_path, frontend_servers);

	// Initialize client sockets
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		client_socks[i] = 0;
	}

	int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	// bzero(&servaddr, sizeof(servaddr));

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htons(INADDR_ANY);
	servaddr.sin_port = htons(PORT);

	int sockopt = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &sockopt, sizeof(sockopt));
	bind(listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr));
	listen(listen_fd, 10);

	///////////////
	// UDP sock  //
	///////////////

	udpsock = socket(AF_INET, SOCK_DGRAM, 0);

	memset((char *)&localSock, 0, sizeof(localSock));
	localSock.sin_family = AF_INET;
	localSock.sin_port = htons(UDPPORT);
	localSock.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(udpsock, (struct sockaddr *)&localSock, sizeof(localSock));

	///////////////
	// Main loop //
	///////////////
	while (1)
	{
		if (num_client >= MAX_CLIENTS)
		{
			continue;
		}
		struct sockaddr_in clientaddr;
		socklen_t clientaddrlen = sizeof(clientaddr);
		int *fd = (int *)malloc(sizeof(int));
		*fd = accept(listen_fd, (struct sockaddr *)&clientaddr, &clientaddrlen);
		// printf("Connection from %s\n", inet_ntoa(clientaddr.sin_addr));

		if (DEBUG)
		{
			fprintf(stderr, "[%d] New Connection\n", *fd);
		}

		// record socket fd in the sockets array
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (client_socks[i] == 0)
			{
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