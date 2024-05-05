#include <iostream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <thread>
#include <fcntl.h>
#include <sys/file.h>
#include <mutex>
#include <map>
#include <set>
#include <dirent.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
using namespace std;

typedef struct
{
  int client_socket;
  pthread_t thread_id;
} Connection;

enum class SMTPState
{
  Initial,
  HeloReceived,
  MailFromReceived,
  RcptToReceived
};

// Constant String messages
const string service_ready_response = "220 localhost service ready\r\n";
const string unknown_command = "500 syntax error, command unrecognized\r\n";
const string quit_command = "221 Service closing transmission channel\r\n";
string invalid501_response = "501 Syntax error in parameters or arguments\r\n";

// Map to maintain mutex on mailboxes
map<string, pthread_mutex_t> mailboxMutexes;
// Vector to main connections
vector<Connection> connections;
vector<pthread_t> joinThreads;
int serverSocketFd;

// Flags for debug and for relay
bool debugLog = false;
bool relay = true;

// String for directory
string directory;
int *clientSocketFd;
pthread_t mainThreadId;


bool sendToBackendSocket(int backend_sock, string command)
{
    if (send(backend_sock, command.c_str(), command.length(), 0) < 0)
    {
        cerr << "Error sending data to backend server" << std::endl;
        return false;
    }
    return true;
}

// Helper function to read from backend socket
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

string base64Encode(const std::string& input) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;

    // Create a Base64 filter/sink
    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    // Create a memory buffer source
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    // Write input data to the Base64 sink
    BIO_write(bio, input.c_str(), static_cast<int>(input.length()));
    BIO_flush(bio);

    // Retrieve the result
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string result(bufferPtr->data, bufferPtr->length);

    // Clean up
    BIO_free_all(bio);

    return result;
}

pair<string, int> extractIPAndPort(const string &serverInfo)
{
  pair<std::string, int> result;

  size_t ipStart = serverInfo.find(":") + 1;                  // Find the start of the IP address
  size_t ipEnd = serverInfo.find(":", ipStart);               // Find the end of the IP address
  result.first = serverInfo.substr(ipStart, ipEnd - ipStart); // Extract the IP address

  size_t portStart = ipEnd + 1;                                                 // Find the start of the port number
  size_t portEnd = serverInfo.find("\r\n", portStart);                          // Find the end of the port number
  result.second = std::stoi(serverInfo.substr(portStart, portEnd - portStart)); // Extract the port number

  return result;
}

// This method connects us to the master node and then gives us the specific backend socket to connect to
int connectToBackend(string username)
{
  int master_sock, backend_sock;
  struct sockaddr_in server_addr;

  // Open master socket
  if ((master_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    std::cerr << "Error creating socket" << std::endl;
    return -1;
  }

  // Server address
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(2000);
  server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  // Connect to server
  if (connect(master_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    std::cerr << "Error connecting to server" << std::endl;
    return -1;
  }

  debugLog ? printf("Connected to Server\n") : 0;

  // Send command to server
  string command = "GET_SERVER:" + username + "\r\n";
  if (send(master_sock, command.c_str(), command.length(), 0) < 0)
  {
    std::cerr << "Error sending command to server" << std::endl;
    return -1;
  }

  debugLog ? printf("Sent command to Server\n") : 0;

  // Receive server info
  char serverInfo[1024];
  if (recv(master_sock, serverInfo, sizeof(serverInfo), 0) < 0)
  {
    std::cerr << "Error receiving response from server" << std::endl;
    return -1;
  }

  debugLog ? printf("Received response %s\n", serverInfo) : 0;
  auto ipAndPort = extractIPAndPort(serverInfo);

  // Connect to backend server
  if ((backend_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    std::cerr << "Error creating socket" << std::endl;
    return -1;
  }

  server_addr.sin_addr.s_addr = inet_addr(ipAndPort.first.c_str());
  server_addr.sin_port = htons(ipAndPort.second);

  if (connect(backend_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    std::cerr << "Error connecting to backend sock" << std::endl;
    return -1;
  }

  char buffer[4096];
  if (recv(backend_sock, buffer, sizeof(buffer), 0) < 0)
  {
    std::cerr << "Error receiving response from server" << std::endl;
    return -1;
  }

  printf("Response: %s\n", buffer);

  return backend_sock;
}

/*
 - Function to initialize mailboxMutexes
 -  Populates map<string, pthread_mutex_t> mailBoxMutexes
*/
void initMailboxMutexes(const string &mailboxDirectory)
{
  DIR *dir = opendir(mailboxDirectory.c_str());
  if (dir)
  {
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
      // Skip "." and ".." entries
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      {
        continue;
      }

      string filename = entry->d_name;

      if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".mbox")
      {
        filename = mailboxDirectory + filename.substr(0, filename.find(".mbox"));
        pthread_mutex_t mutex;
        pthread_mutex_init(&mutex, NULL);
        mailboxMutexes[filename] = mutex;
      }
    }

    // EC Logic - If in relay mode add a logic to create a key for mqueue file
    if (relay)
    {
      string filename = mailboxDirectory + "/" + "mqueue";
      int fdRelay = open(filename.c_str(), O_RDWR | O_CREAT, 0664);
      if (fdRelay == -1)
      {
        cerr << "Error creating lock file: " << filename
             << " error: " << strerror(errno) << endl;
      }
      else
      {
        // Close the file immediately we just needed to create it if it didn't exist
        close(fdRelay);
        pthread_mutex_t mutex;
        pthread_mutex_init(&mutex, NULL);
        mailboxMutexes[mailboxDirectory + "mqueue"] = mutex;
      }
    }
    closedir(dir);
  }
  else
  {
    cerr << "Error initializing map for users" << endl;
  }
}

// Cleanup function calls the kill on all child threads and then closes server socket
void cleanup(pthread_t self)
{
  for (auto connection : connections)
  {
    if (pthread_equal(connection.thread_id, self))
    {
      continue;
    }
    pthread_kill(connection.thread_id, SIGUSR1);
    pthread_join(connection.thread_id, NULL);
  }
  // Closing Server Socket and Exiting
  free(clientSocketFd);
  close(serverSocketFd);
}

// Server - Ctrl+C handler (SIGINT Handler) - Copied from Echo server
void siginthandler(int param)
{
  // Send SIGUSR1 to all threads except the one executing this handler (which is Main thread)
  pthread_t self = pthread_self();
  if (self == mainThreadId && debugLog)
  {
    cerr << "\nSIGINT Received" << endl;
  }
  // Call the cleanup Process
  cleanup(self);
  exit(0);
}

// SIGUSR1 handler - each thread sends a message to it's client and shuts down - Copied from echo server
void signalUserHandler(int param)
{
  pthread_t self = pthread_self();
  for (auto &connection : connections)
  {
    if (pthread_equal(connection.thread_id, self))
    {
      const char *msg = "-ERR Server shutting down\r\n";
      send(connection.client_socket, msg, strlen(msg), MSG_NOSIGNAL);
      close(connection.client_socket);
      if (debugLog)
        cerr << "[" << connection.client_socket << "] Connection Closed" << endl;
      break;
    }
  }
  pthread_exit(NULL);
}

// Helper function to send response to defined client socket file descriptor
void sendResponse(int client_socket, const string &response)
{
  if (send(client_socket, response.c_str(), response.length(), MSG_NOSIGNAL) > 0)
  {
    if (debugLog)
    {
      cerr << "[" << client_socket << "] S: " << response;
    }
  }
}

// Helper function to send badSequence of commands response
void badSequenceOfCommands(int client_socket)
{
  string badSequenceOfCommandsString = "503 Bad Sequence of Commands\r\n";
  sendResponse(client_socket, badSequenceOfCommandsString);
}

// Case insensitive comparison of commands
bool caseInsensitiveStringCompare(const string &str1, const string &str2)
{

  for (size_t i = 0; i < str2.length(); ++i)
  {
    if (tolower(str1[i]) != tolower(str2[i]))
    {
      return false;
    }
  }

  return true;
}

// Thread function to handle each connection
void *handle_connection(void *arg)
{
  signal(SIGUSR1, signalUserHandler);
  // Block SIGINT in child threads - https://edstem.org/us/courses/53560/discussion/4346857
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  pthread_sigmask(SIG_BLOCK, &mask, NULL);

  int client_socket = *((int *)arg);
  // Naveen's OH - Incorporate an extra character because maybe 1000 characters given and 1001th character is the null character
  char buffer[1001];
  int bytes_read;
  int total_bytes_read = 0;
  string mailFrom;
  set<string> rcptTo;
  set<string> rcptToRelay;
  int fd;
  // Send greeting message
  if (send(client_socket, service_ready_response.c_str(), strlen(service_ready_response.c_str()), 0) > 0)
  {
    if (debugLog)
      cerr << "[" << client_socket << "] S: " << service_ready_response;
    free(arg);
  }
  else
  {
    // TODO : Error Block - Nothing to be done - David's OH
  }

  SMTPState state = SMTPState::Initial;
  // Handle commands
  while ((bytes_read = recv(client_socket, buffer + total_bytes_read, sizeof(buffer) - total_bytes_read, 0)) > 0)
  {
    total_bytes_read += bytes_read;
    // Null-terminate the buffer
    buffer[total_bytes_read] = '\0';

    // Pointer to the first EOL encountered
    char *endOfLine = nullptr;
    while ((endOfLine = strstr(buffer, "\r\n")) != nullptr)
    {

      // TODO : Check if we need print entire buffer or just till end of line?
      if (debugLog)
        cerr << "[" << client_socket << "] C: " << string(buffer, endOfLine - buffer + 2);

      // All commands block first check if the server is in state to receive command and if not, they responsd with bad sequence of commands

      if (caseInsensitiveStringCompare(buffer, "HELO "))
      {
        if (state != SMTPState::Initial && state != SMTPState::HeloReceived)
        {
          badSequenceOfCommands(client_socket);
        }
        else
        {
          // Extract the text after "HELO "
          char *text = buffer + 5;
          // Empty HELO
          if (strlen(text) == 0 || (strlen(text) == 2 && text[0] == '\r' && text[1] == '\n'))
          {
            // If there is nothing after "HELO ", send an error response as domain cannot be null
            sendResponse(client_socket, "501 Syntax error in parameters or arguments\r\n");
          }
          // HELO Has something after it
          else
          {
            state = SMTPState::HeloReceived;
            // Handle the case where there is something after "HELO "
            string str(text);
            char *end = strchr(buffer, '\r');
            string response = "250 localhost\r\n";
            sendResponse(client_socket, response);
          }
        }
      }

      else if (caseInsensitiveStringCompare(buffer, "MAIL FROM:"))
      {
        if (state != SMTPState::HeloReceived)
        {
          badSequenceOfCommands(client_socket);
        }
        else
        {
          char *email_start = strchr(buffer, '<');
          char *email_end = strchr(buffer, '>');
          if (email_start != NULL && email_end != NULL && email_start < email_end)
          {
            mailFrom = string(email_start + 1, email_end - email_start - 1);
            // Find '@' to split local part and domain
            size_t atPos = mailFrom.find('@');
            if (atPos == string::npos)
            {
              sendResponse(client_socket, "501 Syntax Error, No @ provided\r\n");
            }
            else
            {
              string localPart = mailFrom.substr(0, atPos - 1);
              string domain = mailFrom.substr(atPos + 1);
              if (localPart.empty() || domain.empty())
              {
                sendResponse(client_socket, "501 Syntax Error, local or domain part empty");
              }
              else
              {
                // Assuming a successful response
                string response = "250 OK : Sender ok\r\n";
                sendResponse(client_socket, response);
                state = SMTPState::MailFromReceived;
              }
            }
          }
          else
          {
            // Invalid syntax, send an error response
            sendResponse(client_socket, invalid501_response);
          }
        }
      }

      else if (caseInsensitiveStringCompare(buffer, "RCPT TO:"))
      {
        if (state != SMTPState::MailFromReceived && state != SMTPState::RcptToReceived)
        {
          badSequenceOfCommands(client_socket);
        }
        else
        {
          char *email_start = strchr(buffer, '<');
          char *email_end = strchr(buffer, '>');
          if (email_start != NULL && email_end != NULL && email_start < email_end)
          {
            string email = string(email_start + 1, email_end - email_start - 1);
            /*
             - Check if the email address ends with "@localhost"
             - Even in case of relay the @localhost check applies because the @localhost inbox mailbox existence can be checked

             - Else if it's not localhost and there is relay -r flag provided, we will validate the email, with @, and ensure local and domain part are not empty
            */
            if (email.length() >= 10 && email.substr(email.length() - 10) == "@localhost")
            {
              // // Check if the mailbox file exists
              // string mailbox_file = directory + "/" + email.substr(0, email.find('@')) + ".mbox";
              // if (!access(mailbox_file.c_str(), F_OK) == 0)
              // {
              //   // Mailbox does not exist, send a 550 error response
              //   sendResponse(client_socket, "550 Requested action not taken: mailbox unavailable\r\n");
              // }
              // else
              // {
              //   // Mailbox exists, send a 250 OK response
              //   rcptTo.insert(email.substr(0, email.find('@')));
              //   sendResponse(client_socket, "250 OK : Receiver ok\r\n");
              //   state = SMTPState::RcptToReceived;
              // }
              // Even if user dosen't exist thats ok 
              rcptTo.insert(email.substr(0, email.find('@')));
              sendResponse(client_socket, "250 OK : Receiver ok\r\n");
              state = SMTPState::RcptToReceived;

            }
            else if (relay)
            {
              printf("outside of domain\n");
              printf("email is : %s\n", email.c_str());
              // wow
              // Find '@' to split local part and domain
              size_t atPos = email.find('@');
              if (atPos == string::npos)
              {
                sendResponse(client_socket, "501 Syntax Error, No @ provided\r\n");
              }
              else
              {
                string localPart = mailFrom.substr(0, atPos - 1);
                string domain = mailFrom.substr(atPos + 1);
                if (localPart.empty() || domain.empty())
                {
                  sendResponse(client_socket, "501 Syntax Error, local or domain part empty");
                }
                else
                {
                  // We are appending to rcptToRelay, as these are emails that need to be relayed to a different file
                  rcptToRelay.insert(email);
                  // RFC821 - Page 35
                  sendResponse(client_socket, "251 User not local : will forward to mqueue file\r\n");
                  state = SMTPState::RcptToReceived;
                }
              }
            }
            else
            {
              // Email address does not end with "@localhost" and relay is false, send an error response
              sendResponse(client_socket, "553 Requested action not taken: mailbox name not allowed\r\n");
            }
          }
          else
          {
            sendResponse(client_socket, invalid501_response);
          }
        }
      }

      else if (caseInsensitiveStringCompare(buffer, "DATA\r\n"))
      {
        if (state != SMTPState::RcptToReceived)
        {
          badSequenceOfCommands(client_socket);
        }
        else
        {
          sendResponse(client_socket, "354 Start mail input; end with <CRLF>.<CRLF>\r\n");
          // Read the email content from the client until a line with a single dot (.) is encountered
          string email_content;
          while (true)
          {
            char buf[1024];
            int bytes_read = recv(client_socket, buf, sizeof(buf), 0);
            if (bytes_read <= 0)
            {
              break;
            }
            // Append the read data to email_content
            email_content.append(buf, bytes_read);
            // Check if the last 5 characters are \r\n.\r\n
            if (email_content.length() >= 5 &&
                email_content[email_content.length() - 5] == '\r' &&
                email_content[email_content.length() - 4] == '\n' &&
                email_content[email_content.length() - 3] == '.' &&
                email_content[email_content.length() - 2] == '\r' &&
                email_content[email_content.length() - 1] == '\n')
            {
              // Remove the last 5 characters (.\r\n)
              email_content.erase(email_content.length() - 3);
              break;
            }
          }
          // We need to return OK even if one of the recipients went through
          bool success = false;

          // Loop to send email to every recipient in rcptTo
          for (string recipient_email : rcptTo)
          {
            string mailbox_file = directory + "/" + recipient_email.substr(0, recipient_email.find('@')) + ".mbox";
            // Opens the file in append mode
            FILE *mbox_file = fopen(mailbox_file.c_str(), "a+");
            // If condition to check that file exists
            if (mbox_file != NULL)
            {
              // Acquire the file descriptor and then use to acquire lock
              fd = fileno(mbox_file);

              /*
              Blocking wait to acquire file lock - cannot be acquired if another process is using it,
              but can be acquired if another thread of same program is using the file
              */
              if (flock(fd, LOCK_EX) < 0)
              {
                // Close the file and write out an error that file lock couldn't be obtained
                cerr << "Failed to lock file " << endl;
                fclose(mbox_file);
              }
              else
              {
                /*
                 - Acquire mutex, only if another thread is not writing to same mailbox (blocking mutex lock)
                 - Write From line to file, and then write the content to file (file here is the rcpt's mailbox)
                 - release mutex
                 - Unlock file
                */
                // pthread_mutex_lock(&mailboxMutexes[directory + recipient_email]);
                time_t rawtime;
                struct tm *timeinfo;
                char date_str[80];
                time(&rawtime);
                timeinfo = localtime(&rawtime);
                strftime(date_str, sizeof(date_str), "%a %b %d %H:%M:%S %Y", timeinfo);
                // fprintf(mbox_file, "From %s %s\n%s", mailFrom.c_str(), date_str, email_content.c_str());
                // pthread_mutex_unlock(&mailboxMutexes[directory + recipient_email]);

                // Project Code ------
                // first we need to connect to the master node
                // given the email we need to get the username
                string username = recipient_email.substr(0, recipient_email.find('@'));
                int backend_socket_curr = connectToBackend(username);
                // now that we have the backend socket we want to PUT the email in there 
                //a,/emails/d/1714684198,content
                string encodedEmail = base64Encode(date_str + email_content);
                string command = "PUT " + username + ",/emails/" + mailFrom + "," + encodedEmail + "\r\n";
                sendToBackendSocket(backend_socket_curr , command);
                string response = readFromBackendSocket(backend_socket_curr);
                printf("response is %s\n",response.c_str());
                // Project Code ------
                // Release the lock
                // flock(fd, LOCK_UN);
                // fclose(mbox_file);
                success = true;
              }
            }
            else
            {
              // Error check if no user with the email exists - could not find mbox
              if (debugLog)
                cerr << "[" << client_socket << "] S: 451 Requested action Aborted : No User with that email\r\n";
            }
          }

          /*
          - Extra Credit : If relay flag is true, we do everything as above, including acquiring file lock and mutex lock except  :
           - We write all relayed (non-localhost) emails to mqueue file in the directory
           - We write two lines of metadata : "From <mailFrom@domain>", To "<anyone@anydomain>", and then the content (we don't add any headers)

          */
          if (relay)
          {
            for (string recipient_email : rcptToRelay)
            {
              string mqueue_file = directory + "/mqueue";
              FILE *mqueueFile = fopen(mqueue_file.c_str(), "a+");
              if (mqueueFile != NULL)
              {
                fd = fileno(mqueueFile);
                if (flock(fd, LOCK_EX) < 0)
                {
                  cerr << "Failed to lock file " << endl;
                  fclose(mqueueFile);
                }
                else
                {
                  pthread_mutex_lock(&mailboxMutexes[directory + "mqueue"]);
                  size_t pos = mailFrom.find('@');
                  mailFrom.replace(pos + 1, std::string::npos, "seas.upenn.edu");
                  printf("mailFrom is now %s\n", mailFrom.c_str());
                  fprintf(mqueueFile, "From <%s>\nTo <%s>\n%s", mailFrom.c_str(), recipient_email.c_str(), email_content.c_str());
                  pthread_mutex_unlock(&mailboxMutexes[directory + "mqueue"]);
                  flock(fd, LOCK_UN);
                  fclose(mqueueFile);
                  const char* command = "./smtpec mailtest";
                  int result = system(command);
                  success = true;
                }
              }
              else
              {
                if (debugLog)
                  cerr << "[" << client_socket << "] S: 451 Requested action Aborted : No User with that email\r\n";
              }
            }
          }

          // Return success even if one of the emails was written to the inbox properly
          if (success)
          {
            sendResponse(client_socket, "250 OK : Email Sent\r\n");
            state = SMTPState::HeloReceived;
            mailFrom.clear();
            rcptTo.clear();
            rcptToRelay.clear();
          }
          else
          {
            sendResponse(client_socket, "554 Data Transfer Failed, restart process again\r\n");
            state = SMTPState::HeloReceived;
            mailFrom.clear();
            rcptTo.clear();
            rcptToRelay.clear();
          }
        }
      }

      else if (caseInsensitiveStringCompare(buffer, "RSET\r\n"))
      {
        if (state == SMTPState::Initial)
        {
          badSequenceOfCommands(client_socket);
        }
        else
        {
          mailFrom.clear();
          rcptTo.clear();
          rcptToRelay.clear();
          state = SMTPState::HeloReceived;
        }
      }

      else if (caseInsensitiveStringCompare(buffer, "NOOP\r\n"))
      {
        if (state == SMTPState::Initial)
        {
          badSequenceOfCommands(client_socket);
        }
        else
        {
          // Send a success response to the client
          sendResponse(client_socket, "250 OK\r\n");
        }
      }

      else if (caseInsensitiveStringCompare(buffer, "QUIT\r\n"))
      {
        if (send(client_socket, quit_command.c_str(), strlen(quit_command.c_str()), MSG_NOSIGNAL) > 0)
        {
          if (debugLog)
            cerr << "[" << client_socket << "] S: " << quit_command;
          close(client_socket);
          if (debugLog)
          {
            cerr << "[" << client_socket << "] Connection closed" << endl;
          }
          pthread_exit(NULL);
        }
        else
        {
          // TODO : Error Block - Nothing to be done David's OH
        }
      }
      // Unknown command
      else
      {
        sendResponse(client_socket, unknown_command);
      }

      // Remove from the data which has already been processed, and start the buffer from +2 (\r\n) position of current end of line
      memmove(buffer, endOfLine + 2, strlen(endOfLine) + 2);
      total_bytes_read -= (endOfLine - buffer + 2);
      endOfLine = nullptr;
    }
  }

  // Handle the case where client closes connection unexpectedly - https://edstem.org/us/courses/53560/discussion/4398872
  if (bytes_read == 0)
  {
    if (debugLog)
      cerr << "[" << client_socket << "] Connection Closed" << endl;
    close(client_socket);
    pthread_exit(NULL);
  }
  // Close connection
  close(client_socket);
  pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
  // Global variable that is set to be the main function's threadID
  mainThreadId = pthread_self();
  signal(SIGINT, siginthandler);
  // Default value of port - 2500 else updated in while loop below
  int port = 2500;
  int c;
  // Picked
  struct sockaddr_in servaddr;

  // Parsing all arguments
  while ((c = getopt(argc, argv, "p:avr")) != -1)
  {
    switch (c)
    {
    case 'p':
      port = atoi(optarg);
      break;
    case 'a':
      cerr << "Pranshu Kumar (pranshuk)" << endl;
      exit(0);
    case 'v':
      debugLog = true;
      break;
    case 'r':
      relay = true;
      break;
    default:
      cerr << "Wrong command line argument - -a -v and -p supported" << endl;
      exit(1);
    }
  }

  // Check for the directory argument (after processing flags)
  if (optind >= argc)
  { // optind is the index of the next non-option argument
    cerr << "Usage: " << argv[0] << " <directory> [-v] [-p port] [-a]" << endl;
    exit(1);
  }

  directory = argv[optind];

  initMailboxMutexes(directory);

  // Establishing Server Socket - TCP Port
  serverSocketFd = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocketFd < 0)
  {
    cerr << "Cannot open socket (" << strerror(errno) << ")" << endl;
    exit(1);
  }

  // Setting Socket Options - Raunaq's OH
  int opt = 1;
  int ret = setsockopt(serverSocketFd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
  if (ret < 0)
  {
    cerr << "Error Setting Socket Options (" << strerror(errno) << ")" << endl;
    exit(1);
  }

  // Binding the socket - Code from lecture notes
  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htons(INADDR_ANY);
  servaddr.sin_port = htons(port);
  if (bind(serverSocketFd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
  {
    cerr << "Error Binding Socket (" << strerror(errno) << ")" << endl;
    exit(1);
  }

  // Checked in Naveen's OH - Listen for connections -> the second parameter of the function might be dicey still, so check on ed
  if (listen(serverSocketFd, 100) < 0)
  {
    cerr << "Error listening for connections" << endl;
    exit(1);
  }

  if (debugLog)
    cerr << "Server started on port " << port << endl;

  // Server continues to accept connections until SIGINT (Ctrl+C)
  while (true)
  {
    struct sockaddr_in clientaddr;
    socklen_t clientaddr_len = sizeof(clientaddr);

    clientSocketFd = (int *)malloc(sizeof(int));
    *clientSocketFd = accept(serverSocketFd, (struct sockaddr *)&clientaddr, &clientaddr_len);

    if (clientSocketFd < 0)
    {
      cerr << "Cannot open client socket (" << strerror(errno) << ")" << endl;
      // Don't exit if there is an error accpeting Socket since server still needs to continue
      continue;
    }
    else
    {

      // Print debug output if verbose mode is enabled
      if (debugLog)
      {
        cerr << "[" << *clientSocketFd << "] New connection" << endl;
      }
      // Create a new thread to handle the connection
      pthread_t thread_id;
      pthread_create(&thread_id, NULL, handle_connection, clientSocketFd);
      connections.push_back({*clientSocketFd,
                             thread_id});
    }
  }

  cleanup(mainThreadId);
  return 0;
}


