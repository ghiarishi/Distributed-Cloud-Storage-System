# Instructions for Demo

## Setup Servers
 - Show Config.txt and explain the master nodes and the use of ports
 - Startup Master Node
 - Startup All Backend Servers (make use of -i and -p to start the backend servers)
 - Startup the front-end server and the load balancer

## Information
 - Emails
	 - Within and Outside of PennCloud
	 - Replying to Emails
	 - Deleting Emails
 - File Uploads
	 - Largest File Upload supported (50MB)
	 - Uploading, Downloading and Renaming Files
	 - Moving folders (& nested folders) from A to B


## Working Demo
 - Create a User (Create a user with username, such that hashes land in two different replica groups)
	 - _This shows the **PUT** command works_
		 - User - Anna & Password - qwerty!
		 - User - Laila & Password - asdfgh@
		 - User - Zihao & Password - zxcvbn#
	
 - Change the Password
   - _This shows the **CPUT** command works_
	   - For user Anna and old password qwerty! to new password poiuyt$
 
 - Login the user with wrong and changed password
	 - This will show that the **changed password** functionality and CPUT command works.
	 - This will show that the authentication works properly with wrong passwords
		 - User : Anna , Password : qwerty! -> should fail
		 - User : Anna, Password: poiuyt$ -> should succeed
	
 - Log in with the same user (do this 3 times, in 3 different browsers - so that each of them connects to a different backend server)
   - _This shows the **GET** command works_
   - _This shows a **round-robin load balancing** on the backend works_
  
 - Upload a large file (10MB) - let's say A.pdf
	 - Upload the large file to /folderLevel0/folderLeverl1/folderLevel2/a.pdf
		 - This will show that **folders** can be made
 - Disable server (say server 2) in the replica group (say replica group 1). Upload another file B.jpg.
	 - Upload the large file to /folderLevel0/folderLeverl1/b.jpg

 - Disable all servers in replica group (same replica group 1).
 - Restart server 2 and login with another user such that request will be given to server 2 in replica group 1. Request for the file B.jpg.
   - _This shows **replication** works_
   - _This shows the system's **fault tolerance** works_
   - _This shows system is consistent_
   
 - Now restart servers and rename a file , and download a file and show that you are able to get a file
	 - Rename /folderLevel0/folderLeverl1/b.jpg to /folderLevel0/folderLeverl1/editedName.jpg
		 - *This shows that **GET** command works*
		 - *This shows that the **renaming** functionality works*
 - Move folderLevel1 from /folderLevel0 to a new folder /folderLevelNew
	 - This will move both b.jpg and a.pdf to a new folder
		 - *This will show **nested** moving of folder works*

- Send an email from anna@seas.upenn.edu to ghiar@seas.upenn.edu
	- *Shows that mails can be sent **outside** of PennCloud*

- Send an email from Anna to Zihao and reply to that email
	- *This will show that sending emails **within** of PennCloud works and **replying** to emails work*
