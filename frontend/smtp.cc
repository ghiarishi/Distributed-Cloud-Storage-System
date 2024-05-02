#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <vector>
#include <map>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <iostream>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <dirent.h>
#include <filesystem>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <mutex>
#include <memory>

using namespace std;
class SMTPServer;
class SMTPServerThread;
#define HELO 0
#define MAILFROM 1
#define RCPTTO 2
#define DATA 3
#define QUIT 4
#define RSET 5
#define NOOP 6

#define INITALSTATE 7
#define HELOSTATE 8
#define MAILFROMSTATE 9
#define RCPTTOSTATE 10
#define DATASTATE 11

void *worker(void *arg);
void signalhandler(int sig_num);

class SMTPServer
{
public:
  set<int> commands = {HELO, MAILFROM, RCPTTO, DATA, QUIT, RSET, NOOP};
  string directory;
  vector<int> listOfConnections;
  vector<pthread_t> threadsVector;
  bool shutDown;
  bool debugOutput;
  // map<string, unique_ptr<mutex>> mutexMap;
  map<int, vector<char>>
      commandsToNames;
  SMTPServer()
  {
    shutDown = false;
    debugOutput = false;
    commandsToNames = {
        {HELO, {'H', 'E', 'L', 'O'}},
        {MAILFROM, {'M', 'A', 'I', 'L', ' ', 'F', 'R', 'O', 'M', ':'}},
        {RCPTTO, {'R', 'C', 'P', 'T', ' ', 'T', 'O', ':'}},
        {DATA, {'D', 'A', 'T', 'A'}},
        {QUIT, {'Q', 'U', 'I', 'T'}},
        {RSET, {'R', 'S', 'E', 'T'}},
        {NOOP, {'N', 'O', 'O', 'P'}},
    };
  }

  void writeMessage(const char *str, int comm_fd)
  {
    int bytes_written = write(comm_fd, str, strlen(str));
  }
  // given directory gets the file names in directory
  vector<string> filesInDirectory(string directoryName)
  {
    vector<string> fileNames;
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(directoryName.c_str())) != NULL)
    {
      while ((ent = readdir(dir)) != NULL)
      { // Check if it is a regular file
        fileNames.push_back(ent->d_name);
      }
      closedir(dir);
    }
    else
    {
      printf("Can't open directory %s", directoryName.c_str());
    }
    return fileNames;
  }
};

class SMTPServerThread
{
public:
  bool rcvdHelo;
  bool rcvdMailFrom;
  bool rcvdRcptTo;
  bool rcvdData;
  vector<vector<char>> currEmailMessage;
  vector<char> mailFrom;
  vector<vector<char>> mailTo;
  SMTPServer parentServer;
  SMTPServerThread()
  {
    rcvdHelo = false;
    rcvdMailFrom = false;
    rcvdRcptTo = false;
    rcvdData = false;
    currEmailMessage = {};
    mailFrom = {};
    mailTo = {};
  }
  // return the current state of the serverThread
  int currentState()
  {
    if (!rcvdHelo && !rcvdMailFrom && !rcvdRcptTo && !rcvdData)
    {
      return INITALSTATE;
    }
    if (rcvdHelo && !rcvdMailFrom && !rcvdRcptTo && !rcvdData)
    {
      return HELOSTATE;
    }
    if (rcvdHelo && rcvdMailFrom && !rcvdRcptTo && !rcvdData)
    {
      return MAILFROMSTATE;
    }
    if (rcvdHelo && rcvdMailFrom && rcvdRcptTo && !rcvdData)
    {
      return RCPTTOSTATE;
    }
    if (rcvdHelo && rcvdMailFrom && rcvdRcptTo && rcvdData)
    {
      return DATASTATE;
    }
    return -1;
  }
  // given a message determine the command
  int parseCommand(vector<char> fullMessage)
  {
    // fprintf(stderr,"Recived command %s\n",fullMessage.data());
    if (rcvdData || fullMessage.size() == 0)
    {
      return -1;
    }
    for (const auto &command : parentServer.commandsToNames)
    {
      bool rightCommand = false;
      for (int i = 0; i < command.second.size(); i++)
      {
        // Check if the uppercase version of charachter is same as current charachter
        if (toupper(fullMessage[i]) != command.second[i])
        {
          break;
        }
        // If I've reached the end of the word and haven't broken this is that command
        else if (i == command.second.size() - 1)
        {
          return command.first;
        }
      }
    }
    return -1;
  }
  // reset server values to HELO State
  void resetServer()
  {
    rcvdMailFrom = false;
    rcvdRcptTo = false;
    rcvdData = false;
    currEmailMessage = {};
    mailFrom = {};
    mailTo = {};
  }
  // given the command and a message extract the email username
  vector<char> getMailName(vector<char> fullMessage, int command)
  {
    vector<char> commandName = parentServer.commandsToNames[command];
    int startOfMessage = 0;
    bool foundStart = false;
    for (int i = 0; i < fullMessage.size(); i++)
    {
      for (int j = 0; j < commandName.size(); j++)
      {
        if (commandName[j] != fullMessage[i + j])
        {
          break;
        }
        if (j == commandName.size() - 1)
        {
          startOfMessage = i + commandName.size();
          foundStart = true;
          break;
        }
      }
      if (foundStart)
      {
        break;
      }
    }
    fullMessage.erase(fullMessage.begin(), fullMessage.begin() + startOfMessage);

    // now lets remove the < and > from the email
    vector<char> email;
    for (char c : fullMessage)
    {
      if (c != '<' && c != '>' && c != ' ')
      {
        email.push_back(c);
      }
    }
    return email;
  }
  // This method gets the current date and time and returns as a string
  string getCurrentDate()
  {
    time_t timeNow = time(nullptr);
    tm *time_struct = localtime(&timeNow);
    char buffer[80];
    strftime(buffer, 80, "%a %b %d %H:%M:%S %Y", time_struct);
    return string(buffer);
  }
  // This method returns the first line of the email
  string getFirstLine(int comm_fd)
  {
    string mailFromString(mailFrom.begin(), mailFrom.end());
    string currentDate = getCurrentDate();
    return "From <" + mailFromString + "> <" + currentDate + ">\n";
  }
  // Helper method to print bad sequence
  void printBadSequence(int comm_fd)
  {
    // in this case we didn't recive helo and have never gotten helo - we need to err
    string insertString = "503 Bad sequence of commands\r\n";
    parentServer.writeMessage(insertString.data(), comm_fd);
    if (parentServer.debugOutput)
    {
      fprintf(stderr, "[%d] S: %s \n", comm_fd, insertString.data());
    }
  }
  // This method writes the email to the file
  void writeEmail(int comm_fd)
  {
    printf("in write email and we have %ld line to write out to %ld recipients\n", currEmailMessage.size(), mailTo.size());
    // write out all of the lines to the file located at directory/mailTo
    for (vector<char> currRecepient : mailTo)
    {
      // open file for writing
      string currRecepientString(currRecepient.begin(), currRecepient.end());
      string mailFile = parentServer.directory + "/" + currRecepientString + ".mbox";
      printf("We are writing out to :  %s \n", mailFile.c_str());
      // parentServer.mutexMap[currRecepientString + ".mbox"]->lock();;
      mutex mtx;
      mtx.lock();
      int file = open(mailFile.c_str(), O_WRONLY | O_APPEND);
      if (file == -1)
      {
        fprintf(stderr, "Error opening file for writing.\n");
        exit(1);
      }

      if (flock(file, LOCK_EX) == -1)
      {
        perror("Error locking file");
        close(file);
        // parentServer.mutexMap[currRecepientString + ".mbox"].get()->unlock();
        exit(1);
      }
      // first write out firstLine
      string firstLine = getFirstLine(comm_fd);
      size_t bytesWritten = write(file, firstLine.data(), firstLine.size());
      for (vector<char> currLine : currEmailMessage)
      {
        string messageString(currLine.begin(), currLine.end());
        printf("The current line is |%s|\n", messageString.c_str());
        if (messageString != "\n")
        {
          printf("The line wasn't an enter key\n");
          currLine.push_back('\n');
        }
        size_t bytesWritten = write(file, currLine.data(), currLine.size());
      }
      flock(file, LOCK_UN); // Release the lock
      mtx.unlock();
      close(file);
      printf("finished writing data\n");
    }
  }
  // Helped method to find if given string is in given list
  bool stringInList(string var, vector<string> list)
  {
    bool toRet = false;
    for (string currString : list)
    {
      if (currString == var)
      {
        toRet = true;
      }
      if (toRet)
      {
        break;
      }
    }
    return toRet;
  }
  // Helper Method to for debug printing
  void debugPrint(vector<char> messageToPrint, int comm_fd, bool server)
  {
    if (parentServer.debugOutput && !server)
    {
      string str(messageToPrint.begin(), messageToPrint.end());
      const char *strPtr = str.c_str();
      fprintf(stderr, "[%d] C: %s \n", comm_fd, strPtr);
    }
    else if (parentServer.debugOutput && server)
    {
      string str(messageToPrint.begin(), messageToPrint.end());
      const char *strPtr = str.c_str();
      fprintf(stderr, "[%d] S: %s \n", comm_fd, strPtr);
    }
  }
  // This is a helper method for writing to output
  void writeToOutput(const char *toWrite, int comm_fd)
  {
    int bytes_written = write(comm_fd, toWrite, strlen(toWrite));
    if (bytes_written < 0)
    {
      fprintf(stderr, "Error writing to file descriptor\n");
      exit(1);
    }
  }
  // This method writes out the result of the given command to comm_fd
  void executeCommand(vector<char> fullMessage, int comm_fd)
  {
    debugPrint(fullMessage, comm_fd, false);
    int commandVal = parseCommand(fullMessage);
    if (rcvdData)
    {
      string insertString;
      // in this case we are in the data command and should just read in line by line and add to our emailMessage
      string messageString(fullMessage.begin(), fullMessage.end());
      printf("      messageString = %s\n", messageString.c_str());
      if (messageString == ".")
      {
        // im this case its the end of the email and we are no longer in email
        writeEmail(comm_fd);
        insertString = "250 OK\r\n";
        write(comm_fd, insertString.data(), insertString.size());
        if (parentServer.debugOutput)
        {
          fprintf(stderr, "[%d] S: %s \n", comm_fd, insertString.data());
        }
        resetServer();
      }
      else
      {
        if (messageString.size() == 0)
        {
          currEmailMessage.push_back({});
        }
        else
        {
          currEmailMessage.push_back(fullMessage);
        }
      }
    }
    else
    {
      switch (commandVal)
      {
      case HELO:
        handleHELO(fullMessage, comm_fd);
        break;
      case MAILFROM:
        handleMAILFROM(fullMessage, comm_fd);
        break;
      case RCPTTO:
        handleRCPTTO(fullMessage, comm_fd);
        break;
      case DATA:
        handleDATA(fullMessage, comm_fd);
        break;
      case QUIT:
        handleQUIT(comm_fd);
        break;
      case RSET:
        handleRSET(comm_fd);
        break;
      case NOOP:
        handleNOOP(comm_fd);
        break;
      case -1:
        writeToOutput("500 syntax error, command unrecognized\r\n", comm_fd);
        break;
      default:
        exit(1);
      }
    }
  }

  // The following are helper methods to handle particular commands
  void handleHELO(const vector<char> &fullMessage, int comm_fd)
  {
    string insertString;
    if (currentState() == INITALSTATE || currentState() == HELOSTATE)
    {
      rcvdHelo = true;
      insertString = "250 localhost\r\n";
    }
    else
    {
      insertString = "503 Bad sequence of commands\r\n";
    }
    write(comm_fd, insertString.data(), insertString.size());
    if (parentServer.debugOutput)
    {
      fprintf(stderr, "[%d] S: %s \n", comm_fd, insertString.data());
    }
  }
  void handleMAILFROM(const vector<char> &fullMessage, int comm_fd)
  {
    if (currentState() == HELOSTATE)
    {
      printf("Command MAILFROM recived \n");
      rcvdMailFrom = true;
      mailFrom = getMailName(fullMessage, MAILFROM);
      string insertString = "250 OK\r\n";
      write(comm_fd, insertString.data(), insertString.size());
      if (parentServer.debugOutput)
      {
        fprintf(stderr, "[%d] S: %s \n", comm_fd, insertString.data());
      }
    }
    else
    {
      printBadSequence(comm_fd);
    }
  }
  void handleRCPTTO(const vector<char> &fullMessage, int comm_fd)
  {
    printf("Command RCPT TO recived \n");
    if (currentState() == MAILFROMSTATE || currentState() == RCPTTOSTATE)
    {
      rcvdRcptTo = true;
      vector<char> mailName = getMailName(fullMessage, RCPTTO);
      string fullEmail(mailName.begin(), mailName.end());
      size_t pos = fullEmail.find('@');
      string emailName = fullEmail.substr(0, pos);
      vector<string> fileNames = parentServer.filesInDirectory(parentServer.directory);
      bool inDirectory = stringInList(emailName + ".mbox", fileNames);
      string substringName = fullEmail.substr(pos + 1).data();
      string insertString;
      printf("inDirectory val %d", inDirectory);
      if (strcmp(substringName.data(), "localhost") == 0 && inDirectory)
      {
        vector<char> emailNameChar(fullEmail.begin(), fullEmail.begin() + pos);
        mailTo.push_back(emailNameChar);
        insertString = "250 OK\r\n";
      }
      else
      {
        insertString = "550 \r\n";
      }
      write(comm_fd, insertString.data(), insertString.size());
      if (parentServer.debugOutput)
      {
        fprintf(stderr, "[%d] S: %s \n", comm_fd, insertString.data());
      }
    }
    else
    {
      printBadSequence(comm_fd);
    }
  }
  void handleDATA(const vector<char> &fullMessage, int comm_fd)
  {
    printf("Command DATA recived \n");
    if (currentState() == RCPTTOSTATE)
    {
      rcvdData = true;
      const char *str = "354 ";
      string message = str + parentServer.directory;
      message = message + "\r\n";
      int bytes_written = write(comm_fd, message.c_str(), message.size());
      if (parentServer.debugOutput)
      {
        fprintf(stderr, "[%d] S: %s \n", comm_fd, message.data());
      }
    }
    else
    {
      printBadSequence(comm_fd);
    }
  }
  void handleQUIT(int comm_fd)
  {
    printf("Command QUIT recived \n");
    if (parentServer.debugOutput)
    {
      fprintf(stderr, "[%d] S: 221 closing transmission channel! \r\n", comm_fd);
    }

    int index = -1;
    for (int i = 0; i < parentServer.listOfConnections.size(); i++)
    {
      if (parentServer.listOfConnections[i] == comm_fd)
      {
        index = i;
        break;
      }
    }
    parentServer.listOfConnections.erase(parentServer.listOfConnections.begin() + index);
    if (parentServer.debugOutput)
    {
      fprintf(stderr, "[%d] Connection closed \r\n", comm_fd);
    }
    const char *str = ("221 " + parentServer.directory + "\r\n").c_str();
    writeToOutput(str, comm_fd);
    close(comm_fd);
    pthread_exit(NULL);
  }
  void handleRSET(int comm_fd)
  {
    printf("Command RSET recived");
    if (currentState() == MAILFROMSTATE || currentState() == RCPTTOSTATE)
    {
      resetServer();
      string insertString = "250 localhost\r\n";
      write(comm_fd, insertString.data(), insertString.size());
      if (parentServer.debugOutput)
      {
        fprintf(stderr, "[%d] S: %s \n", comm_fd, insertString.data());
      }
    }
    else
    {
      printBadSequence(comm_fd);
    }
  }
  void handleNOOP(int comm_fd)
  {
    printf("Command NOOP recived");
    string insertString = "OK\r\n";
    write(comm_fd, insertString.data(), insertString.size());
    if (parentServer.debugOutput)
    {
      fprintf(stderr, "[%d] S: %s \n", comm_fd, insertString.data());
    }
  }
};
// create instance of server class
SMTPServer server;

int main(int argc, char *argv[])
{
  signal(SIGINT, signalhandler);
  int c;
  int portno = 2500;

  while ((c = getopt(argc, argv, "p:av")) != -1)
  {
    switch (c)
    {
    case 'p':
      portno = atoi(optarg);
      break;
    case 'a':
      fprintf(stderr, "Janavi Chadha\n");
      fprintf(stderr, "JanaviC\n");
      exit(1);
    case 'v':
      server.debugOutput = true;
      break;
    case '?':
      fprintf(stderr, "error unexpected input  \n");
      exit(1);
    }
  }
  // if given directory set directory otw err.
  if (optind < argc)
  {
    server.directory = argv[optind];
  }
  else
  {
    fprintf(stderr, "input faulty input format ./smtp <directory> \n");
    exit(1);
  }

  // Create a new socket
  int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
  struct sockaddr_in servaddr;
  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htons(INADDR_ANY);
  servaddr.sin_port = htons(portno);
  bind(listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr));

  // connect to the server
  listen(listen_fd, 10);
  int opt = 1;
  int ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

  // While we are not shut down add more connections
  while (!server.shutDown)
  {
    struct sockaddr_in clientaddr;
    socklen_t clientaddrlen = sizeof(clientaddr);
    int comm_fd = accept(listen_fd, (struct sockaddr *)&clientaddr, &clientaddrlen);
    if (server.debugOutput)
    {
      fprintf(stderr, "[%d] New connection \n", comm_fd);
    }
    const char *str = "220 localhost ";
    string message = str + server.directory;
    message = message + "\r\n";
    int bytes_written = write(comm_fd, message.c_str(), message.size());
    if (server.debugOutput)
    {
      fprintf(stderr, "%s", message.c_str());
    }
    if (bytes_written < 0)
    {
      fprintf(stderr, "Error writing to file descriptor\n");
      exit(1);
    }
    // add curr connection to list
    server.listOfConnections.push_back(comm_fd);
    // Create pthread & send it to do work
    pthread_t thread;
    server.threadsVector.push_back(thread);
    pthread_create(&thread, NULL, worker, &comm_fd);
  }
  return 0;
}

void *worker(void *arg)
{
  SMTPServerThread serverThread;
  serverThread.parentServer = server;
  int comm_fd = *(int *)arg;
  int start = 0;
  int end = 0;
  vector<char> fullMessage;
  string message;
  // null terminate buffer
  char buf[1001];
  memset(buf, '\0', sizeof(buf));

  while (!server.shutDown)
  {
    // read in message
    int n = read(comm_fd, &buf[end], 1000);
    // start is the beginning and end is how many we read in
    start = 0;
    end = end + n;
    // in this case we didn't read in anything and buffer is empty so we are done
    if (n == 0 && buf[0] == '\0')
    {
      break;
    }
    if (n < 0)
    {
      fprintf(stderr, "Read failed");
      exit(1);
    }
    // loop through what we just read in to see if there is \r\n
    for (int i = 0; i < end; i++)
    {
      // if we've come to a CRLF
      if (buf[i] == '\r' && buf[i + 1] == '\n')
      {
        // the current message spans from start to i - lets put that in a vector
        vector<char> toAdd(buf + start, buf + i);
        fullMessage = toAdd;
        // now the start of the next message will be i + 2
        start = i + 2;
        if (fullMessage.size() == 0)
        {
          serverThread.executeCommand(fullMessage, comm_fd);
        }
      }
      // if there is a message in fullMessage

      if (fullMessage.size() > 0)
      {
        serverThread.executeCommand(fullMessage, comm_fd);
        fullMessage = {};
      }
    }

    // Now we want to clear out buffer from 0 to start
    copy(buf + start, buf + sizeof(buf), buf);
    fill(buf + sizeof(buf) - start, buf + sizeof(buf), '\0');

    // set end to be at the last populated location
    for (int i = 0; i < sizeof(buf); i++)
    {
      if (buf[i] == '\0')
      {
        end = i;
        break;
      }
    }
  }
  if (server.shutDown)
  {
    server.writeMessage("-ERR Server shutting down\r\n", comm_fd);
    close(comm_fd);
  }
  // Just in case we leave close the connection and exit pthread
  close(comm_fd);
  int index = -1;
  for (int i = 0; i < server.listOfConnections.size(); i++)
  {
    if (server.listOfConnections[i] == comm_fd)
    {
      index = i;
      break;
    }
  }
  server.listOfConnections.erase(server.listOfConnections.begin() + index);
  pthread_exit(NULL);
}

// This is the signal handler
void signalhandler(int sig_num)
{
  server.shutDown = true;
  for (int connection_fd : server.listOfConnections)
  {
    // set connection to be non-blocking? QUESTION : Is this how we do it?
    fcntl(connection_fd, F_SETFL, O_NONBLOCK);
    // goodbye message
    server.writeMessage("-ERR Server shutting down\r\n", connection_fd);
    // close connection
    close(connection_fd);
  }

  for (pthread_t currThread : server.threadsVector)
  {
    pthread_detach(currThread);
  }

  exit(1);
}
