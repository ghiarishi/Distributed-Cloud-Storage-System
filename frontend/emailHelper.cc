using namespace std;


#include "emailHelper.h"

std::tuple<std::string, std::string> extractSubjectAndMessage(const std::string &email)
{
    std::string subject, message;

    // Find the position of "Subject: "
    size_t subjectPos = email.find("Subject: ");
    if (subjectPos != std::string::npos)
    {
        // Extract the subject starting from the position after "Subject: "
        subject = email.substr(subjectPos + 9); // 9 is the length of "Subject: "

        // Find the position of the next occurrence of "\n" after the subject
        size_t newlinePos = subject.find("\n");
        if (newlinePos != std::string::npos)
        {
            // Extract the message starting from the position after the newline
            message = subject.substr(newlinePos + 1);
            // Remove the trailing newline if present
            if (!message.empty() && message.back() == '\n')
            {
                message.pop_back();
            }
            // Trim any leading whitespace from the message
            message.erase(0, message.find_first_not_of(" \t\n\r\f\v"));
        }
    }

    return std::make_tuple(subject, message);
}

string extractPath(const string &path)
{
    string key = "drive/";
    size_t pos = path.find(key);

    if (pos != string::npos)
    {
        return path.substr(pos + key.length());
    }
    else
    {
        // Return an empty string if "/drive/" is not found
        return "";
    }
}

