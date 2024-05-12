# Spring 2024 - CIS 5050 - Team 18 - Final Project

## Team Members
- Pranshu Kumar
- Rishi Ghia
- Zihao Deng
- Janavi Chadha

## How to Run Our PennCloud

**Note:** These setup instructions are specific to our demo. Detailed demo instructions can be found in `DEMO.md`.

### Setup Steps
1. **Navigate to Project Directory:**  
   `cd sp24-cis505-T18`

2. **Build the Project:**  
   Run `make`

3. **Start the Key-Value Store Master:**  
   Open a terminal and navigate to `sp24-cis5050-T18/backend/kvstore/`  
   Run `./master`

4. **Initialize Key-Value Stores:**
   - Terminal 1:  
     `cd sp24-cis5050-T18/backend/kvstore/`  
     Run `./kvstore -i 1 -p 2001`
   - Terminal 2:  
     `cd sp24-cis5050-T18/backend/kvstore/`  
     Run `./kvstore -i 1 -p 2002`
   - Terminal 3:  
     `cd sp24-cis5050-T18/backend/kvstore/`  
     Run `./kvstore -i 1 -p 2003`
   - Terminal 4:  
     `cd sp24-cis5050-T18/backend/kvstore/`  
     Run `./kvstore -i 2 -p 2004`
   - Terminal 5:  
     `cd sp24-cis5050-T18/backend/kvstore/`  
     Run `./kvstore -i 2 -p 2005`
   - Terminal 6:  
     `cd sp24-cis5050-T18/backend/kvstore/`  
     Run `./kvstore -i 2 -p 2006`
   - Terminal 7:  
     `cd sp24-cis5050-T18/backend/kvstore/`  
     Run `./kvstore -i 3 -p 2007`
   - Terminal 8:  
     `cd sp24-cis5050-T18/backend/kvstore/`  
     Run `./kvstore -i 3 -p 2008`
   - Terminal 9:  
     `cd sp24-cis5050-T18/backend/kvstore/`  
     Run `./kvstore -i 3 -p 2009`

5. **Start the SMTP Server:**  
   Open a terminal and navigate to `sp24-cis5050-T18/smtp`  
   Run `/smtpServer -p 2500`

6. **Initialize the Frontend Services:**
   - **Load Balancer:**  
     Open a terminal and navigate to `sp24-cis5050-T18/frontend`  
     Run `./loadbalancer`
   - **Admin Interface:**  
     Open a terminal and navigate to `sp24-cis5050-T18/frontend`  
     Run `./admin`

### Final Steps
1. **Access the Admin Console:**  
   Open a browser and navigate to `http://localhost:7000/admin`

2. **Enable Frontend Servers:**  
   Select "Enable" for at least one frontend server.

3. **Access the Frontend:**  
   Navigate to `http://localhost:7500` to be routed through the load balancer.

### Further Instructions
To redo the demo we ran with Dr.Phan you can find it in the file titled `DEMO.md`.
