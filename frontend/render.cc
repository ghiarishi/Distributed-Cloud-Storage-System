#include "render.h"
#include "readingHelper.h"

using namespace std;


// Get filename from the path
string getFileName(const string &path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos)
        return path.substr(pos + 1);
    return path;
}

vector<pair<string, int>> extractFiles(string username, string returnString, string directoryPath)
{
    vector<string> paths;
    istringstream iss(returnString);
    string line;
    vector<pair<string, int>> files;

    while (std::getline(iss, line))
    {
        // Assuming "/content/" is fixed part of the path
        size_t startPos = line.find("/content/");
        if (startPos != std::string::npos)
        {
            // Extract path starting from "/content/"
            std::string path = line.substr(startPos + 9);
            printf(" The path is |%s| directoryPath is |%s|\n", path.c_str(), directoryPath.c_str());
            startPos = path.find(directoryPath);
            string remainingText = path.substr(startPos + directoryPath.size());
            printf("The remainingText is %s\n", remainingText.c_str());
            if (directoryPath.size() == 0 && remainingText.find("/") == std::string::npos)
            {
                printf("we are adding path\n");
                paths.push_back(path);
            }
            else if (directoryPath.size() != 0 && remainingText.find("/") != std::string::npos &&
                     remainingText.find("/", remainingText.find("/") + 1) == std::string::npos)
            {
                printf("we are adding path\n");
                paths.push_back(path);
            }
        }
    }

    for (string currPath : paths)
    {
        string fileName = getFileName(currPath);
        int isFolder = (currPath.find('.') == std::string::npos);
        pair<string, int> filePair = make_pair(fileName, isFolder);
        files.push_back(filePair);
    }
    return files;
}

// retrieve files/folders in drive (0 for file, 1 for folder)
vector<pair<string, int>> get_drive(string username, int currentClientNumber, string dir_path)
{
    printf("dir path is : %s\n", dir_path.c_str());
    string command = "LIST " + username + ",/content/" + dir_path + "\r\n";
    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
    sendToBackendSocket(currentClientNumber, command, username);

    string response = readFromBackendSocket(currentClientNumber, username);
    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

    vector<pair<string, int>> files = extractFiles(username, response, dir_path);

    string target_entry = dir_path;
    files.erase(std::remove_if(files.begin(), files.end(), [&](const pair<string, int> &file)
                               { return file.first == target_entry; }),
                files.end());

    DEBUG ? printf("The files we have are  \n") : 0;
    for (const auto &pair : files)
    {
        std::cout << "(" << pair.first << ", " << pair.second << ")" << std::endl;
    }
    DEBUG ? printf("\n") : 0;

    return files;
}

// redirect to the user's menu page
string redirectReply()
{
    string response = "HTTP/1.1 302 Found\r\nLocation: /\r\n\r\n\r\n";
    return response;
}

// render the login webpage
string renderLoginPage(string sid, string errorMessage)
{
    string content = "";
    content += "<html>\n";
    content += "<head><title>Login Page</title></head>\n";
    content += "<body style='font-family: Arial, sans-serif; background-color: #f0f0f0; text-align: center; padding-top: 50px;'>\n";
    content += "<h1 style='color: #333;'>PennCloud Login</h1>\n";
    content += "<div style='background-color: white; padding: 20px; margin: auto; width: 300px; box-shadow: 0 4px 8px rgba(0,0,0,0.1);'>\n";
    content += "<h2>Log in</h2>\n";
    if (!errorMessage.empty())
    {
        content += "<p style='color: red;'>" + errorMessage + "</p>\n";
    }
    content += "<form action=\"/menu\" method=\"post\">\n";
    content += "Username: <input type=\"text\" name=\"username\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "Password: <input type=\"password\" name=\"password\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "<input type=\"submit\" value=\"Submit\" style='width: 100%; padding: 10px; background-color: #4CAF50; color: white; border: none; cursor: pointer;'>\n";
    content += "</form>\n";
    content += "</div>\n";
    content += "<p><a href=\"#\" onclick='toggleDisplay(\"signup\", \"changepass\")' style='color: blue; cursor: pointer;'>Sign Up</a></p>\n";
    content += "<p><a href=\"#\" onclick='toggleDisplay(\"changepass\", \"signup\")' style='color: blue; cursor: pointer;'>Change Password</a></p>\n";
    content += "<div id='signup' style='display: none; background-color: white; padding: 20px; margin: auto; width: 300px; box-shadow: 0 4px 8px rgba(0,0,0,0.1);'>\n";
    content += "<h2>Sign Up</h2>\n";
    content += "<form action=\"/signup\" method=\"post\">\n";
    content += "Username: <input type=\"text\" name=\"username\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "Password: <input type=\"password\" name=\"password\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "<input type=\"submit\" value=\"Submit\" style='width: 100%; padding: 10px; background-color: #4CAF50; color: white; border: none; cursor: pointer;'>\n";
    content += "</form>\n";
    content += "</div>\n";
    content += "<div id='changepass' style='display: none; background-color: white; padding: 20px; margin: auto; width: 300px; box-shadow: 0 4px 8px rgba(0,0,0,0.1);'>\n";
    content += "<h2>Change Password</h2>\n";
    content += "<form action=\"/newpass\" method=\"post\">\n";
    content += "Username: <input type=\"text\" name=\"username\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "Old Password: <input type=\"text\" name=\"oldpass\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "New Password: <input type=\"text\" name=\"newpass\" style='margin-bottom: 10px; width: 95%;'><br>\n";
    content += "<input type=\"submit\" value=\"Submit\" style='width: 100%; padding: 10px; background-color: #4CAF50; color: white; border: none; cursor: pointer;'>\n";
    content += "</form>\n";
    content += "</div>\n";
    content += "<script>\n";
    content += "function toggleDisplay(showId, hideId) {\n";
    content += "  var showElement = document.getElementById(showId);\n";
    content += "  var hideElement = document.getElementById(hideId);\n";
    content += "  if (showElement.style.display === 'none') {\n";
    content += "    showElement.style.display = 'block';\n";
    content += "    hideElement.style.display = 'none';\n";
    content += "  } else {\n";
    content += "    showElement.style.display = 'none';\n";
    content += "  }\n";
    content += "}\n";
    content += "</script>\n";
    content += "</body>\n";
    content += "</html>\n";

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\n" +
                    "Set-Cookie: sid=" + sid +
                    "\r\n\r\n";
    string reply = header + content;

    return reply;
}

// render menu page
string renderMenuPage(string username)
{
    string content = "";
    content += "<html>\n";
    content += "<head><title>Menu</title></head>\n";
    content += "<body style='font-family: Arial, sans-serif; background-color: #f0f0f0; text-align: center; padding-top: 50px;'>\n";
    content += "<h1 style='color: #333;'>Welcome, " + username + "!</h1>\n";
    content += "<div style='background-color: white; padding: 20px; margin: auto; width: 300px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); text-align: center;'>\n";
    content += "<ul style='list-style: none; padding: 0;'>\n";
    content += "<li style='margin: 10px 0;'><button onclick=\"window.location.href='/mailbox'\" style='width: 90%; padding: 10px; background-color: #4CAF50; color: white; border: none; cursor: pointer;'>Mailbox</button></li>\n";
    content += "<li style='margin: 10px 0;'><button onclick=\"window.location.href='/drive'\" style='width: 90%; padding: 10px; background-color: #4CAF50; color: white; border: none; cursor: pointer;'>Drive</button></li>\n";
    content += "</ul>\n";
    content += "</div>\n";
    content += "<button onclick=\"window.location.href='/'\" style='margin-top: 20px; padding: 10px; width: 300px; background-color: #f44336; color: white; border: none; cursor: pointer;'>Sign Out</button>\n";
    content += "</body>\n";
    content += "</html>\n";

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\n\r\n";
    string reply = header + content;

    return reply;
}

string renderDrivePage(string username, int currentClientNumber, string dir_path )
{

    vector<pair<string, int>> files = get_drive(username, currentClientNumber, dir_path);
    printf("directory path is : %s\n", dir_path.c_str());
    printf("username is : %s\n", username.c_str());

    string content = "";
    content += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
    content += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    content += "<title>PennCloud Drive</title>";
    content += "<style>body { font-family: Arial, sans-serif; } ul { list-style-type: none; } li { margin-bottom: 10px; }</style></head><body>";
    content += "<h1>PennCloud Drive</h1>";

    content += "<h2>Create Folder</h2>";
    content += "<button onclick=\"document.getElementById('create-form').style.display='block'\">Create Folder</button>";
    content += "<div id='create-form' style='display:none;'>";
    content += "<form action='/create-folder' method='post'>";
    content += "<label for='folder-name'>Folder Name:</label>";
    content += "<input type='text' id='folder-name' name='folderName' required>";
    content += "<button type='submit'>Create</button>";
    content += "</form></div>";

    content += "<h2>Upload File</h2>";
    content += "<form action='/upload-file' method='post' enctype='multipart/form-data'>";
    content += "<input type='file' name='fileToUpload' required>";
    content += "<button type='submit'>Upload File</button>";
    content += "</form>";

    content += "<h2>Content</h2>";
    content += "<ul>";

    for (const pair<string, int> p : files)
    {
        string name = p.first;
        int isdir = p.second;
        if (isdir)
        {
            if (dir_path == "")
            {
                content += "<li><a href='/drive/" + name + "'>" + name + "</a>";
            }
            else
            {
                content += "<li><a href='/drive/" + dir_path + "/" + name + "'>" + name + "</a>";
            }
            content += "<form action='/rename' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<input type='text' name='newName' placeholder='New name'>";
            content += "<button type='submit'>Rename</button>";
            content += "</form>";

            content += "<form action='/move' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<input type='text' name='newPath' placeholder='New path'>";
            content += "<button type='submit'>Move</button>";
            content += "</form>";

            content += "<form action='/delete' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<button type='submit'>Delete</button>";
            content += "</form>";
        }
        else
        {
            content += "<li>" + name;
            content += "<form action='/rename' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<input type='text' name='newName' placeholder='New name'>";
            content += "<button type='submit'>Rename</button>";
            content += "</form>";

            content += "<form action='/move' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<input type='text' name='newPath' placeholder='New path'>";
            content += "<button type='submit'>Move</button>";
            content += "</form>";

            content += "<form action='/delete' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<button type='submit'>Delete</button>";
            content += "</form>";

            content += "<form action='/download' method='post' style='display:inline;'>";
            content += "<input type='hidden' name='fileName' value='" + name + "'>";
            content += "<button type='submit'>Download</button>";
            content += "</form>";
        }
    }
    content += "</body></html>";

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\n\r\n";
    string reply = header + content;

    return reply;
}

void deleteEmail(string username, string item, int currentClientNumber)
{
    printf("in deleteEmail \n");
    printf("item is %s \n", item.c_str());

    // wow
    //get everything post delete
    string prefix = "delete/";
    size_t pos = item.find(prefix);
    string result = item.substr(pos + prefix.length());
    string command = "DELETE " + result + "\r\n";
    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
    sendToBackendSocket(currentClientNumber, command, username);
    string response = readFromBackendSocket(currentClientNumber, username);
    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;
}

string getEmailContent(string emailID, int currentClientNumber, string username)
{
    string command = "GET " + emailID + "\r\n";
    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
    sendToBackendSocket(currentClientNumber, command, username);

    string response = readFromBackendSocket(currentClientNumber, username);
    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

    size_t pos = response.find("+OK");
    string encodedMessage = response.substr(pos + 4);
    printf("encodedMessages is %s\n", encodedMessage.c_str());

    return encodedMessage;
}

vector<email> extractEmails(string username, string returnString)
{
    vector<email> emails;
    istringstream iss(returnString);
    string line;

    printf(" the return string is %s\n", returnString.c_str());
    while (getline(iss, line))
    {
        if (line.empty())
            continue; // Skip empty lines
        if (line.substr(0, 8) != "/emails/")
            continue; // Skip lines that do not start with '/emails/'

        email e;

        // Find positions of separators
        size_t pos1 = line.find('/', 8);
        size_t pos2 = line.find(',', pos1 + 1);

        // Extract sender, epochTime, and content
        e.from = line.substr(8, pos1 - 8);
        e.epochTime = line.substr(pos1 + 1, pos2 - pos1 - 1);
        e.id = username + ",/" + line.substr(pos2 + 2, line.size() - pos2 - 2); // Adjust indices to skip ", and "
        printf("the ID is %s\n", e.id.c_str());

        // Add email to the vector
        emails.push_back(e);
    }

    return emails;
}
// retrieve emails in mailbox
vector<email> get_mailbox(string username, int currentClientNumber)
{
    string command = "LIST " + username + ",/emails\r\n";
    DEBUG ? printf("Sending to backend: %s\nBackend sock: %d\n", command.c_str(), backend_socks[currentClientNumber].socket) : 0;
    sendToBackendSocket(currentClientNumber, command, username);

    string response = readFromBackendSocket(currentClientNumber, username);
    DEBUG ? printf("Response: %s \n", response.c_str()) : 0;

    vector<email> emails = extractEmails(username, response);

    return emails;
}

string renderMailboxPage(string username, int currentClientNumber)
{
    vector<email> emails = get_mailbox(username, currentClientNumber);

    // Start building the page content
    string content = "";
    content += "<html><head><title>Mailbox</title></head><body>";
    content += "<h1>PennCloud Mailbox</h1>";
    content += "<p>Click to view or send an email.</p>";
    content += "<ul>";
    content += "<li><a href='/mailbox/send'>Send an Email</a></li>";
    content += "</ul>";

    // Email list section
    content += "<table border='1' style='width: 100%;'>";
    content += "<tr><th>From</th><th>Time</th><th>Actions</th></tr>";
    for (email currEmail : emails)
    {
        string timeDecoded = base64DecodeString(currEmail.epochTime);
        string toDisplayCurr = currEmail.from + " (" + timeDecoded + ")";
        string viewLink = "<a href='/mailbox/" + currEmail.id + "'>View</a>";
        string deleteLink = "<a href='/mailbox/delete/" + currEmail.id + "'>Delete</a>";

        // Render each email in a table row with actions
        content += "<tr>";
        content += "<td>" + toDisplayCurr + "</td>";
        content += "<td>" + timeDecoded + "</td>";
        content += "<td>" + viewLink + " | " + deleteLink + "</td>";
        content += "</tr>";
    }
    content += "</table>";
    content += "</body></html>";

    // Construct the full HTTP response
    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\nConnection: keep-alive\r\n\r\n";
    string reply = header + content;

    return reply;
}

// render the email content page for an email (item)
string renderEmailPage(string username, string item, int currentClientNumber)
{
    string content;
    printf("item is %s\n", item.c_str());
    if (item == "send")
    {
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
    else
    {
        string encodedMessage = getEmailContent(item, currentClientNumber, username);
        vector<char> decodedEmailVector = base64Decode(encodedMessage);
        string decodedEmail(decodedEmailVector.begin(), decodedEmailVector.end());
        printf("encoded email is %s\n", encodedMessage.c_str());
        printf("decoded email is %s\n", decodedEmail.c_str());
        // Split the decoded email into lines
        vector<string> emailLines;
        stringstream ss(decodedEmail);
        string line;
        while (getline(ss, line, '\n'))
        {
            emailLines.push_back(line);
        }

        string sender, subject, body;
        // Extract sender, subject, and body from emailLines
        for (const string &emailLine : emailLines)
        {
            if (emailLine.find("From:") == 0)
            {
                // Extract sender's name (part before '@' and after '<')
                size_t start = emailLine.find("<");
                size_t end = emailLine.find("@");
                if (start != string::npos && end != string::npos)
                {
                    sender = emailLine.substr(start + 1, end - start - 1);
                }
            }
            else if (emailLine.find("Subject:") == 0)
            {
                subject = emailLine.substr(9); // Extract subject
            }
            else
            {
                // Assume everything else is part of the email body
                body += emailLine + "<br>";
            }
        }
        vector<char> itemVec(item.begin(), item.end());
        string itemEncoded = base64Encode(itemVec);
        content += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
        content += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        content += "<title>Email Viewer</title>";
        content += "<style>body { font-family: Arial, sans-serif; }";
        content += "#email-content { background-color: #f8f8f8; padding: 20px; margin-bottom: 20px; }";
        content += "textarea { width: 100%; height: 150px; }</style></head><body>";
        content += "<h1>PennCloud Email</h1>";
        content += "<div id='email-content'>";
        content += "<p><strong>From:</strong> " + sender + "</p>";
        content += "<p><strong>Subject:</strong> " + subject + "</p>";
        content += "<p><strong>Message:</strong> " + body + "</p></div>";
        content += "<h2>Forward</h2>";
        content += "<form action='/forward-email' method='POST'>";
        content += "<input type='hidden' name='email_id' value='" + itemEncoded + "'>"; // Include original email content
        content += "<p><strong>To:</strong> <input type='email' name='to' required></p>";
        content += "<button type='submit'>forward</button></form></body></html>";
        content += "<h2>Write a Reply</h2>";
        content += "<form action='/send-email' method='POST'>";
        content += "<p><strong>To:</strong> <input type='email' name='to' required></p>";
        content += "<p><strong>Subject:</strong> <input type='text' name='subject' value='Re: " + subject + "' required></p>";
        content += "<p><strong>Message:</strong></p><textarea name='message' required></textarea>";
        content += "<button type='submit'>Send</button></form></body></html>";
    }

    string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                    to_string(content.length()) + "\r\nConnection : keep-alive" + "\r\n\r\n";
    string reply = header + content;

    return reply;
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
                    to_string(content.length()) + "\r\nConnection : keep-alive" + "\r\n\r\n";
    string reply = header + content;

    return reply;
}

string generateReply(int reply_code, string username, string item , string sid , int currentClientNumber)
{
    if (reply_code == LOGIN)
    {
        return renderLoginPage(sid);
    }
    else if (reply_code == SIGNUP)
    {
        return renderLoginPage(sid);
    }
    else if (reply_code == NEWPASS)
    {
        return renderLoginPage(sid);
    }
    else if (reply_code == REDIRECT)
    {
        return redirectReply();
    }
    else if (reply_code == MENU)
    {
        return renderMenuPage(username);
    }
    else if (reply_code == DRIVE)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == MAILBOX)
    {
        return renderMailboxPage(username, currentClientNumber);
    }
    else if (reply_code == EMAIL)
    {
        // if the reply_code starts with delete we actually render the mailbox Krimo
        if (item.rfind("delete", 0) == 0)
        {
            deleteEmail(username, item, currentClientNumber);
            return renderMailboxPage(username, currentClientNumber);
        }
        return renderEmailPage(username, item, currentClientNumber);
    }
    else if (reply_code == SENDEMAIL)
    {
        return renderMailboxPage(username, currentClientNumber);
    }
    else if (reply_code == FORWARD)
    {
        return renderMailboxPage(username, currentClientNumber);
    }
    else if (reply_code == DOWNLOAD)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == RENAME)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == MOVE)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == DELETE)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == NEWDIR)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }
    else if (reply_code == UPLOAD)
    {
        return renderDrivePage(username, currentClientNumber, item);
    }

    string reply = renderErrorPage(reply_code);
    return reply;
}
