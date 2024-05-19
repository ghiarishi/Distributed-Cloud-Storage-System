# Distributed Cloud Storage System

## Contributors
- Rishi Ghia
- Pranshu Kumar
- Zihao Deng
- Janavi Chadha

## How to Run the Distributed Cloud Storage System

**Note:** These setup instructions are tailored for our demo. You can find comprehensive demo instructions in `DEMO.md`.

### Setup Steps
1. **Navigate to Project Directory:**  
   `cd Distributed-Cloud-Storage-System`

2. **Build the Project:**  
   Run `make`

3. **Start the Key-Value Store Master:**  
   In a terminal, navigate to `Distributed-Cloud-Storage-System/backend/kvstore/`  
   Run `./master`

4. **Initialize Key-Value Stores:**
   - Terminal 1:  
     `cd Distributed-Cloud-Storage-System/backend/kvstore/`  
     Run `./kvstore -i 1 -p 2001`
   - Terminal 2:  
     `cd Distributed-Cloud-Storage-System/backend/kvstore/`  
     Run `./kvstore -i 1 -p 2002`
   - Terminal 3:  
     `cd Distributed-Cloud-Storage-System/backend/kvstore/`  
     Run `./kvstore -i 1 -p 2003`
   - Terminal 4:  
     `cd Distributed-Cloud-Storage-System/backend/kvstore/`  
     Run `./kvstore -i 2 -p 2004`
   - Terminal 5:  
     `cd Distributed-Cloud-Storage-System/backend/kvstore/`  
     Run `./kvstore -i 2 -p 2005`
   - Terminal 6:  
     `cd Distributed-Cloud-Storage-System/backend/kvstore/`  
     Run `./kvstore -i 2 -p 2006`
   - Terminal 7:  
     `cd Distributed-Cloud-Storage-System/backend/kvstore/`  
     Run `./kvstore -i 3 -p 2007`
   - Terminal 8:  
     `cd Distributed-Cloud-Storage-System/backend/kvstore/`  
     Run `./kvstore -i 3 -p 2008`
   - Terminal 9:  
     `cd Distributed-Cloud-Storage-System/backend/kvstore/`  
     Run `./kvstore -i 3 -p 2009`

5. **Start the SMTP Server:**  
   In a terminal, navigate to `Distributed-Cloud-Storage-System/smtp`  
   Run `/smtpServer -p 2500`

6. **Initialize the Frontend Services:**
   - **Load Balancer:**  
     In a terminal, navigate to `Distributed-Cloud-Storage-System/frontend`  
     Run `./loadbalancer`
   - **Admin Interface:**  
     In a terminal, navigate to `Distributed-Cloud-Storage-System/frontend`  
     Run `./admin`

### Final Steps
1. **Access the Admin Console:**  
   Open a browser and navigate to `http://localhost:7000/admin`

2. **Enable Frontend Servers:**  
   Select "Enable" for at least one frontend server.

3. **Access the Frontend:**  
   Navigate to `http://localhost:7500` to route through the load balancer.

### Further Instructions
To replicate the demo presented to Dr. Phan, see `DEMO.md`.

### Image Descriptions
All images are located in the `images` folder.

1. **Admin Console:**  
   The Admin Console allows administrators to enable/disable servers and view data stored in each backend node. See `Admin.png` and `AdminData.jpg`.

2. **Login Page:**  
   Two images illustrate the login pages, where users can log in or sign up if they don't have an account. See `loginPage.png` and `signupPage.png`.

3. **Home Page:**  
   `homePage.png` depicts the home page that users see after logging in.

4. **Email Page:**  
   `emailExample.png` shows the email composition page, and `inbox.png` shows the inbox view.

5. **Drive Page:**  
   `drivePage.png` shows the drive page after some files have been uploaded. Clicking folder links opens the folder view.
