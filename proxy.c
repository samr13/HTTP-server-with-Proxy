/* Author Shyam sundar Ramamoorthy <shra3971@colorado.edu
*/
#include <stdio.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <pthread.h>
#define CONNCTIONS 100
#define TIMEOUT 1;
fd_set set;
fd_set set1;
char path[60];
char defaultfile[25];
char defaultport[6]; 
int acc[CONNCTIONS];
int timer[CONNCTIONS];
struct timeval timeout;
struct timeval timeout1[CONNCTIONS];
int conn_estb=0;
int cache_expiry=0;
int prefetch=1;
pthread_t tid[CONNCTIONS];
pthread_mutex_t lock;
struct stat st = {0};  
char hash[50][32];
FILE* logfile;
char clientip[20];
int clientport;
struct {
    char extn[15];
    char filetype[15];
} wsconf[20];


/* Sets timeout value for socket*/
int set_timer (int sockfd)
{
    FD_SET (sockfd, &set);
    timeout.tv_sec = TIMEOUT;
    timeout.tv_usec = 0;
  
    return (select (getdtablesize(), &set, NULL, NULL, &timeout));
}

// once cache is expired after timer, proxy needs to contact server
int set_cache_expiry (int k,int expiry)
{
    FD_SET (k, &set1);
    timeout1[k].tv_sec = expiry;
    timeout1[k].tv_usec = 0;
  
    return (select (getdtablesize(), &set1, NULL, NULL, &timeout1[k]));
}

/* Creates a socket, listen at the port and returns socket descriptor*/
int getSocketId(int port) {
	int s=0,yes=1;
	if((s=socket(AF_INET,SOCK_STREAM,0))==-1) {
		perror("server socket");
	}
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	
	struct sockaddr_in addr, cli;
	addr.sin_addr.s_addr=inet_addr("192.168.0.1");
	addr.sin_port=htons(port);
	addr.sin_family=PF_INET;
	bzero(&addr.sin_zero,8);
	if (bind(s,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))==-1) {
		perror("bind error");
		exit(1);
	} else {
		printf ("binded to ipaddr: 192.168.0.1 & port: %d\n",port );
	}
     if((listen(s,CONNCTIONS))==-1) {
         perror ("listen error");
     }
	return s;
}

// connects from web proxy to server
int open_server_socket( char* hostname, int port )
{
    struct hostent *he;
    struct sockaddr_in sa_in;
    he = gethostbyname( hostname );
    int sa_len, sock_family, sock_type, sock_protocol;
    int sockfd;

    if ( he == (struct hostent*) 0 )
        printf("Unknown host." );
    sock_family = sa_in.sin_family = he->h_addrtype;
    sock_type = SOCK_STREAM;
    sock_protocol = 0;
    sa_len = sizeof(sa_in);
    (void) memmove( &sa_in.sin_addr, he->h_addr, he->h_length );
    sa_in.sin_port = htons( port );
    //sa_in.sin_addr.s_addr = inet_addr(he->h_addr);
    sockfd = socket( sock_family, sock_type, sock_protocol );
    if ( sockfd < 0 )
        printf( "Couldn't create socket." );
    
    if ( connect( sockfd, (struct sockaddr*) &sa_in, sa_len ) < 0 )
        printf ("Connection refused." );

    return sockfd;    
}

// check md5sum of url and inserts in array. 
// check file creation time and see if timer exceeds, if expired, clear cache 
int md5Check (char url[2000],char file[1000]) {
    pthread_mutex_lock(&lock);
    int n, val, k,found=0;
    char md5out[32],str[2048];
    sprintf(str,"printf '%s' | md5sum", url); 
    FILE *md5pipe=popen(str, "r");
    fgets(md5out, 32, md5pipe);
    pclose(md5pipe);  
    for (k=0; k <= 49; k++) {
        if (strcmp(hash[k],md5out)==0) {
            stat(file, &st);
            int currenttime=(int)time(NULL);
            int filemodtime=(int)st.st_mtime;
            if ((currenttime-filemodtime)< cache_expiry) {
                found=1;
                printf("Cache found in proxy server \n");
            } else { 
                strcpy(hash[k],md5out);
                printf("Cache cleared due to timeout %d seconds..",cache_expiry);
            }
            break;
        } else if (strcmp(hash[k],"")==0) {
            strcpy(hash[k],md5out);
            //printf("Inserted URL to cache:%s %s %d\n", url, md5out,k);
            break;
        } else {
        }
    }
    pthread_mutex_unlock(&lock);
    return found;
    
}
/* wait for request and returns a response with timer implementation */
void* httpresponse (void* iter) {
	int len=0,count=0,f,flags,select_out,leng_recv=0,hash_key=0;
	char str[4096],buf[1024], httpreq[2048];
    int clientid=*(int*)iter;
  logfile=fopen("proxylogs.log","a");
  time_t rawtime;
  struct tm * timeinfo;
  time ( &rawtime );
  timeinfo = localtime ( &rawtime );
  char logcontent[1000];

    while (1) {
    set_timer(acc[clientid]);
       if (FD_ISSET(acc[clientid],&set) ) {
            memset(str,0,4096);
            leng_recv = recv(acc[clientid],str,4096, 0);
            str[leng_recv]='\0';
            strcpy(httpreq,str);
            if (leng_recv == 0) {;
	            shutdown(acc[clientid],SHUT_RDWR);
                close(acc[clientid]);
                FD_CLR(acc[clientid],&set);
                acc[clientid]=-1;
                return;
            } else if (leng_recv< 0) {	
               shutdown(acc[clientid],SHUT_RDWR);
               close(acc[clientid]);
               FD_CLR(acc[clientid],&set);
               acc[clientid]=-1;
               return;
            } else {
                char *filename,*httpversion,*httpreqmeth,*line,*keepalive,badrequest[1024];
                int port;
                char url[2000], host[1000], path[1000], file[1000],*hostline;
				memset(host,0,1000);
				memset(path,0,1000);
				
        	    httpreqmeth=strtok (str," ");
                if ( strncmp(httpreqmeth, "GET\0",4)==0 )
        	    {
        	        strcpy(url,strtok (NULL," "));
                    if ( sscanf( url, "http://%[^:/]:%d%s", host, &port, path ) == 3 ) {
                    } else if ( sscanf( url, "http://%[^/]%s", host, path ) == 2 ) {
                        port = 80;
                    } else if ( sscanf( url, "http://%[^:/]:%d", host, &port ) == 2 ) {
                        *path = '\0';
                    } else if ( sscanf( url, "http://%[^/]", host ) == 1 ){ 
                        port = 80;
                        *path = '\0';
                    } 


                    httpversion=strtok (NULL," \n");
                    if ( strncmp(httpversion, "HTTP/1.0",8)!=0 & strncmp(httpversion, "HTTP/1.1",8)!=0) {
                        printf("httpversion not 1.0 or 1.1\n");
                         sprintf (badrequest,"HTTP/1.0 400 Bad Request: Invalid HTTP‐Version:%s\r\n\r\n<html><body>Invalid HTTP‐Version:%s</body></html>\r\n\r\n",httpversion,httpversion);
                         send(acc[clientid], badrequest, strlen(badrequest),0);
                    }

					if (strcmp(host,"")==0) {		
					  hostline=strtok (NULL,"\t\n");
					  if (hostline!=NULL) {
					  if ( sscanf( hostline, "Host: %[^:]:%d", host,&port ) == 2){ 
						strcpy(path,url);
        			            } 
					  } 
					}
					if(strcmp(path,"/")==0) 
						strcpy(path,url);

				    if (stat(host, &st) == -1) 
    	 	   	        mkdir(host, 0700);
     	   	        
     	   	        strcpy(file,host);
        	        char* p;
                    p=strrchr(path,'/');
                    strcat(file,p);
                    if (path[strlen(path)-1]=='/') {    
                        strcat(file,"index.html");
                    }
       
                    if (md5Check(url,file)==0) {
                        printf("Cache not found\n"); 
                        printf("Get data from server: %s\n\n",path);   
                        int sfd=open_server_socket(host, port);
                        int bytes_read,total=0;
                        char buffer[1024];
                        sprintf(buffer, "%s %s %s\n\n",httpreqmeth,path,httpversion);
                        buffer[strlen(buffer)]='\0';
                        send(sfd, buffer, strlen(buffer), 0);
                        FILE *fd;
                        fd=fopen(file,"w+");
                        do {
                            bzero(buffer, sizeof(buffer));
                            bytes_read = recv(sfd, buffer, sizeof(buffer), 0);
     			    		total+=bytes_read;
                            buffer[bytes_read]='\0';
                            if ( bytes_read > 0 ) {
                                fwrite(buffer,sizeof(char),bytes_read,fd);
                                send(acc[clientid],buffer,bytes_read,0);
                            } else {
 								break;
			    			}

                        } while ( bytes_read > 0 );

                    	fclose(fd);
                    	
						if (total!=1) {
						  sprintf(logcontent,"%s   %s   %d  %s  %d  %d  %d\n",strtok(asctime(timeinfo),"\n"),clientip,(clientport),host,port,leng_recv,total);
						  fwrite(logcontent,sizeof(char),strlen(logcontent),logfile);
						}
                        if (prefetch)
                        if(fork()==0) {
                            int lSize;
                            char *content;
                            FILE *fp=fopen(file,"r+");
                            fseek( fp , 0L , SEEK_END);
                            lSize = ftell( fp );
                            rewind( fp );
                            content = calloc( 1, lSize+1 );
                            if( !content ) fclose(fp),fputs("memory alloc fails",stdout),exit(1);
                        
                            if( fread( content , lSize, 1 , fp)!=1 )
                              fclose(fp),free(content),fputs("entire read fails",stdout),exit(1);

                            while(1) {
                                content = strstr(content, "a href=\"");
                                char* p,*q;
                                p=strchr(content,'"')+1;
                                q=strchr(p,'"')+1;
                                int index;
                                index = (int)(q - p);
                                char arr[index];
                                char url1[index];
                                //strcpy(arr,"href=\"");
                                strncpy(arr,p,index);
                                arr[index-1]='\0';
                                strcpy(url1,arr);
                                int full_url=0;
                                printf("Prefetched Pages: %s...", arr);
                                if ( sscanf( arr, "http://%[^:/]/%s", host, path ) == 2 ) {
                                    strcpy(arr,path);
                                    full_url=1;
                                } 
                               
                                int sfd1=open_server_socket(host, 80);
                                char buffer[4096];
                                sprintf(buffer, "%s %s %s\n\n",httpreqmeth,arr,httpversion);
                                if (strncmp(arr,"#",1)==0) {
                                    content++;
                                    continue;
                                }
                                

                                buffer[strlen(buffer)]='\0';
                                send(sfd1, buffer, strlen(buffer), 0);
                                if (stat(host, &st) == -1) 
                                    mkdir(host, 0700);
                                strcpy(file,host);
                                strcat(file,"/");
                                char* p1;
                                p1=strrchr(arr,'/')+1;
                                strcat(file,p1);
                                printf("File written:%s\n",file);
                                md5Check(url1,file);
                                
                                FILE *fd1;
                                fd1=fopen(file,"w+");
                                do {
                                    bzero(buffer, sizeof(buffer));
                                    bytes_read = recv(sfd1, buffer, sizeof(buffer), 0);
                                    buffer[bytes_read]='\0';
                                    if ( bytes_read > 0 ) {
                                        fwrite(buffer,sizeof(char),bytes_read,fd1);
                                    }
                                } while ( bytes_read > 0 );
                                fclose(fd1);
                                close(sfd1);
                                if (!content) 
                                    break;
                                content++;
                            }

                        }
                    } else {              
                        hash_key=1;
						int total=0;
                        printf ("Get data from cache: %s\n\n", file);    
                        if ((f=open(file,O_RDONLY))!=-1) 
                            while ( (count=read(f, buf, 1024))>0 ) {
                                len=send(acc[clientid], buf, count,0);
				                total+=count;
			                 }
                            if(total!=1) {
                              sprintf(logcontent,"%s   %s   %d  From Cache %d  %d\n",strtok(asctime(timeinfo),"\n"),clientip,(clientport),leng_recv,total);
                              fwrite(logcontent,sizeof(char),strlen(logcontent),logfile);
                            }
                        close(f);
                    }
        	        
                    line=strtok(httpreq,"\n");
                    while((line=strtok(NULL,":"))!=NULL) {
                        if (strncmp(line,"Connection",10)==0) {
                           keepalive=strtok(NULL," \n");
                           if (strncmp(keepalive,"keep-alive",10)==0 || strncmp(keepalive,"Keep-alive",10)==0) {
                               timeout.tv_sec = TIMEOUT;
                           } else if (strcmp(keepalive,"close")==0 || strcmp(keepalive,"Close")==0) {
	                            shutdown(acc[clientid],SHUT_RDWR);
                                close(acc[clientid]);
                                FD_CLR(acc[clientid],&set);
                                printf("\n%dth client closed for: close in keep-alive\n",clientid);
                           } else{
                                printf("Not found keepalive\n");
                           }
                           break; 
                        } else {
                            line=strtok(NULL,"\n");
                        }
                    }
                } else {
                    sprintf (badrequest,"HTTP/1.0 400 Bad Request: Invalid Method:%s\r\n\r\n<html><body>INVALID METHOD:%s</body></html>\r\n\r\n",httpreqmeth,httpreqmeth);
                    send(acc[clientid], badrequest, strlen(badrequest),0);
                    printf("http bad request(not GET). Request method got:%s\n",httpreqmeth);
        	    }

                fflush(NULL);
            }
        } else {
	           shutdown(acc[clientid],SHUT_RDWR);
               close(acc[clientid]);
               FD_CLR(acc[clientid],&set);
               acc[clientid]=-1;
               return;
        }
    }
}

int main(int argc, char *argv[]) {

	int siz_sock=sizeof(struct sockaddr_in);
	int clientid=0,resp=1,num_conn=0;
    long z;
	int i,j=0,k=0,port;

    memset(&path,0,15);
    memset(&defaultfile,0,15);
    for (i=0;i<=CONNCTIONS;i++) {acc[i]=-1;timer[i]=-1;}
    if (argc!=0 && argv[1]!=NULL && argv[2]!=NULL && argv[3]!=NULL) {
        prefetch=atoi(argv[3]);
        port=atoi(argv[1]);
        cache_expiry=atoi(argv[2]);        
    } else if (argc!=0 && argv[1]!=NULL && argv[2]!=NULL) {
		port=atoi(argv[1]);
        cache_expiry=atoi(argv[2]);        
	} else if (argc!=0 && argv[1]!=NULL ) {
        port=atoi(argv[1]);
    }else {
        printf("Usage ./proxy <port>\n");
        exit(0);
    }
    acc[0]=getSocketId(port);

    char dnat[200];
    sprintf(dnat," iptables -t nat -A PREROUTING -p tcp -i eth2 -j DNAT --to 192.168.0.1:%d",port);
    //Pass data coming from client through eth2 to proxy ip, port
    system(dnat);

    FD_ZERO (&set);
	
    if (pthread_mutex_init(&lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }
    printf ("Server Socket id:%d\n",acc[0]);


    while (1) {
        if ((acc[++conn_estb]=accept(acc[0],(struct sockaddr*)NULL,&siz_sock))==-1) {
	      printf("accept failed");
	    } else {
	    	struct sockaddr_in addr;
	    	socklen_t addr_size = sizeof(struct sockaddr_in);
	    	int res = getpeername(acc[conn_estb], (struct sockaddr *)&addr, &addr_size);
		    strcpy(clientip, inet_ntoa(addr.sin_addr));
		    clientport= addr.sin_port;

		    char snat[200];
		    sprintf(snat," iptables -t nat -A POSTROUTING -p tcp -o eth1 -j SNAT --to-source %s:%d",clientip,clientport);
		    // change  source ip, port from proxy to client so that server doesn't know existence of proxy 
		    // in server, packet has client ip & port instead of proxy ip & port
	    	system(snat);
	
			pthread_create(&(tid[conn_estb]), NULL, &httpresponse, &conn_estb);

        }

        if (conn_estb==1) {
        	// do prefetching links for initial connection
            if (prefetch)
                sleep(3);
            pthread_join(tid[conn_estb], NULL);
            printf("\n");
        } else {
            sleep(1);
            pthread_join(tid[conn_estb], NULL);
        }
        //remove rule once thread finished since client port changes for each connection
    	system(" iptables -t nat -D POSTROUTING 1");
    	fclose(logfile);
        pthread_mutex_destroy(&lock);
	}
	// delete rule once proxy closes connection
    system(" iptables -t nat -D PREROUTING 1");
}
