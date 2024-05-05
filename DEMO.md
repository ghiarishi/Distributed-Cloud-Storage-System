# Instructions for Demo

## Setup Servers
 - Show Config.txt and explain the master nodes and the use of ports
 - Startup Master Node
 - Startup All Backend Servers (make use of -i and -p to start the backend servers)
 - Startup the front-end server and the load balancer

## Working Demo
 - Create a User (Create a user with username, such that hashes land in two different replica groups)
   - _This shows the **PUT** command works_
 - Change the Password
   - _This shows the **CPUT** command works_
 - Log in with the user (do this 3 times, in 3 different browsers - so that each of them connects to a different backend server)
   - _This shows the **GET** command works_
   - _This shows a **round-robin load balancing** on the backend works_
 - Upload a large file (10MB) - let's say A.pdf
 - Disable server (say server 2) in the replica group (say replica group 1). Upload another file B.jpg.
 - Disable all servers in replica group (same replica group 1).
 - Restart server 2 and login with another user such that request will be given to server 2 in replica group 1. Request for the file B.jpg.
   - _This shows **replication** works_
   - _This shows the system's **fault tolerance** works_
   - _This shows system is consistent_
 - 
