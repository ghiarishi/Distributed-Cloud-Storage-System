# Demo Instructions

## Setup Servers
1. **Configuration Overview:**  
   - Review `Config.txt` and explain the master nodes and their port usage.

2. **Start the Master Node:**  
   - Initialize the master node.

3. **Start All Backend Servers:**  
   - Use the `-i` and `-p` flags to start the backend servers with their designated indices and ports.

4. **Start the Frontend Server and Load Balancer:**  
   - Initialize the frontend server and load balancer to distribute requests.

## Feature Information
1. **Emails:**
   - **Within and Outside of PennCloud:** Sending emails to internal and external recipients.
   - **Replying:** How to respond to emails.
   - **Deleting:** Removing emails from the mailbox.

2. **File Uploads:**
   - **Max Upload Size:** The largest supported upload is 50 MB.
   - **File Management:**  
     - Uploading, downloading, and renaming files.
     - Moving folders (including nested folders) between directories.

## Working Demo
1. **Create Users:**
   - Demonstrate the **PUT** command by creating users whose hashes fall into different replica groups:  
     - Anna: `qwerty!`  
     - Laila: `asdfgh@`  
     - Zihao: `zxcvbn#`

2. **Change Password:**
   - Demonstrate the **CPUT** command by changing Anna's password from `qwerty!` to `poiuyt$`.

3. **Login Verification:**
   - Verify the changed password and **CPUT** command functionality by attempting to log in Anna:  
     - `qwerty!`: Should fail.  
     - `poiuyt$`: Should succeed.  
   - Demonstrate proper authentication with incorrect passwords.

4. **Multiple Browser Logins:**
   - Show **GET** command functionality and **round-robin load balancing** by logging in Anna on three different browsers to connect to different backend servers.

5. **Large File Upload:**
   - Upload a 10 MB file (`A.pdf`) to the nested path `/folderLevel0/folderLevel1/folderLevel2/a.pdf` to show folder creation.

6. **Server Disabling and Upload:**
   - Disable server 2 in replica group 1.
   - Upload `B.jpg` to `/folderLevel0/folderLevel1/b.jpg` to validate continued functionality.

7. **Server Recovery and Fault Tolerance:**
   - Disable all servers in replica group 1, then restart server 2.
   - Log in as another user to ensure that `B.jpg` is still accessible.  
   - Demonstrates system **replication**, **fault tolerance**, and **consistency**.

8. **Rename and Download Files:**
   - Rename `b.jpg` to `/folderLevel0/folderLevel1/editedName.jpg`.  
     - Shows **GET** command and **renaming** functionality.

9. **Move Nested Folder:**
   - Move `folderLevel1` from `/folderLevel0` to `/folderLevelNew`.  
   - Demonstrates moving nested folders works.

10. **External Email:**
    - Send an email from `anna@seas.upenn.edu` to `ghiar@seas.upenn.edu`.  
    - Shows emails can be sent **outside** of PennCloud.

11. **Internal Email and Reply:**
    - Send an email from Anna to Zihao and reply.  
    - Demonstrates that internal emailing and **replying** work correctly.

