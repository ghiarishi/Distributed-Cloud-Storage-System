using namespace std;


#include "emailHelper.h"

std::tuple<std::string, std::string> extractSubjectAndMessage(const std::string &email) {

    printf("The email is %s\n", email.c_str());
    // Markers for headers
    const std::string subjectMarker = "Subject:";
    const std::string endOfHeaders = "\n"; // Assuming subject ends with a newline

    // Find the position of "Subject:"
    size_t startPos = email.find(subjectMarker);
    if (startPos == std::string::npos) {
        return {"", ""}; // Subject marker not found
    }
    startPos += subjectMarker.length(); // Move past "Subject:"

    // Find the end of the subject line
    size_t endPos = email.find("\n", startPos);
    if (endPos == std::string::npos) {
        return {"", ""}; // Newline after subject not found
    }

    // Extract the subject, trimming any leading and trailing whitespace
    std::string subject = email.substr(startPos, endPos - startPos);
    size_t subjectStart = subject.find_first_not_of(" ");
    size_t subjectEnd = subject.find_last_not_of(" ");
    subject = subject.substr(subjectStart, subjectEnd - subjectStart + 1);

    printf("the subject is %s\n" , subject.c_str());
    

    // Message starts immediately after the newline character at the end of the subject
    size_t messageStart = endPos + 1; // Move past the newline character

    // Extract the message
    std::string message = email.substr(messageStart);

    printf("the message is %s\n" , message.c_str());

    return {subject, message};
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

