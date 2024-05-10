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

