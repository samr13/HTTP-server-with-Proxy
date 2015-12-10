Server VM1: (10.0.0.2) 
ws.conf is needed so that port server, files are mentioned here
create a folder www and put all files like index.html, images inside it
cc server.c -o server 
./server


Proxy VM2: (192.168.0.1)
set ip-forward. Here eth2 is connected to client and eth1 to server
cc proxy.c -o proxy -lpthread
./proxy 10001 30 no
First argument is PORT number
Second argument is timeout for cache
Third argument is to enable prefetch
DNAT & SNAT is configured in code so proxy's existence is hidden from both server & proxy
proxy writes a log file tracking server & client information


Client VM3: (192.168.0.2)
Open a browser and load 10.0.0.2:14000
Proxy gets request from server & saves in proxy.
After sometime (but within timer expiry), load same page which gets data from cache.
If timer expired, proxy gets request from server & saves in proxy.



