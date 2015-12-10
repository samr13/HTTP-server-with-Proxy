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
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <string.h>

#define CONNCTIONS 50
#define TIMEOUT 1
fd_set set;
char path[60];
char defaultfile[25];
char defaultport[6]; 
int acc[CONNCTIONS];
int timer[CONNCTIONS];
struct timeval timeout;
int conn_estb=0;
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

/* Creates a socket, listen at the port and returns socket descriptor*/
int getSocketId(int port) {
	int s=0,yes=1;
	if((s=socket(AF_INET,SOCK_STREAM,0))==-1) {
		perror("server socket");
	}
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	
struct sockaddr_in addr, cli;
	addr.sin_addr.s_addr=inet_addr("10.0.0.2");
	addr.sin_port=htons(port);
	addr.sin_family=PF_INET;
	bzero(&addr.sin_zero,8);
	if (bind(s,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))==-1) {
		perror("bind error");
		exit(1);
	} else {
		printf ("binded to ipaddr: 10.0.0.2 & port: %d\n",port );
	}

     if((listen(s,CONNCTIONS))==-1) {
         perror ("listen error");
     }
	
	return s;
}

/* parse given ws.conf and writes in a global structure*/
int parseWSconf () {
     char buff[1000],pat[60];
     FILE* fd;
     fd=fopen("ws.conf","r");
     char *line,*pa;
     char* lin=NULL;
     int i=0,j=0;
     size_t len=0;
     ssize_t read;
     int port;
     if(fd==NULL) {
         printf ("configuration file not found");
     }
     while ((read=getline(&lin,&len,fd))!=-1) {
         if(lin==NULL) {
            break;
         }
         strcpy(wsconf[i].extn,strtok(lin," "));
         strcpy(wsconf[i].filetype,strtok(NULL,"\n"));
         printf ("\nwsconf: %s, %s",wsconf[i].extn,wsconf[i].filetype);
         if (strcmp(wsconf[i].extn,"DocumentRoot")==0) {
             strcpy(pat,wsconf[i].filetype);
             if (pat[0]=='"') {
                pa=&pat[1];
                pa=strtok(pa,"\"");
                strcpy(path,pa);
                printf("path, pa:%s==%s",path,pa);
             }
         }
         if (strcmp(wsconf[i].extn,"DirectoryIndex")==0) {
             strcpy(defaultfile,wsconf[i].filetype);
         }
         if (strcmp(wsconf[i].extn,"Listen")==0) {
             strcpy(defaultport,wsconf[i].filetype);
             port=atoi(defaultport);
         }
         i++;
     }
    return port;
}

/* parse get request,implements 501,200 and returns header*/
char* parseHeader(char* filename,char* head,int clientid) {
    int i,fi,count=0,content_len,invalid_uri=0; 
    char contType[50],buff[1024],*line,file[2];
    FILE* fp;
    for (i=0; wsconf[i].extn!=0;i++) {
        char *ext=&filename[strlen(filename)-strlen(wsconf[i].extn)];
        if(strcmp(wsconf[i].extn,ext)==0) {
            strcpy(contType,wsconf[i].filetype);
            break;
        }
    }
    

    char header[512];
   if (invalid_uri==1) {
        printf ("Invalid URI");
        sprintf(header,"HTTP/1.1 400 Bad Request:%s\r\n\r\n",filename);
        char *invaliduri="<html><body>400 Bad Request: Invalid URI:</body></html>\r\n\r\n";
        send(acc[clientid], header, strlen(header),0);
        send(acc[clientid],invaliduri,strlen(invaliduri),0);
        send(acc[clientid],filename,strlen(filename),0);
    } else if (strcmp(contType,"")==0) {
        printf ("Not implemented");
        sprintf(header,"HTTP/1.1 501 Not Implemented:%s\r\n\r\n",filename);
        char *notimpl="<html><body>501 Not Implemented</body></html>\r\n\r\n";
        send(acc[clientid], header, strlen(header),0);
        send(acc[clientid],notimpl,strlen(notimpl),0);
        send(acc[clientid],filename,strlen(filename),0);
    } else {

     char* lin=NULL;
     size_t len=0;
     char ch;

        if (fp=fopen(filename,"r")) {
            while ((ch=fgetc(fp))!=-1) { content_len+=1; }
            fclose(fp);
            sprintf(header,"HTTP/1.1 200 OK\r\ncharset=UTF-8\r\nContent-Type: %s\r\n\r\n",contType,content_len); 
        } 
        if (strcmp(contType,"image/jpg")==0 || strcmp(contType,"image/jpeg")==0) {
            sprintf(header,"HTTP/1.1 200 OK\r\ncharset=UTF-8\r\nContent-Type: %s\r\n\r\n",contType);
        }
		send(acc[clientid], header, strlen(header),0);
    }
    if (access(filename,F_OK)==-1) {
		int len=strlen(header);
                    if(len==0) {
                        strcpy(header,"HTTP/1.1 404 Not Found\r\n\r\n");
                        send(acc[clientid], header, strlen(header),0);
                        char *notfound="<html><body>404 not found</body></html>\r\n\r\n";
                        send(acc[clientid],notfound,strlen(notfound),0);
        }
	}
	    head=header;
	    return head;
}

/* wait for request and returns a response with timer implementation */
int httpresponse (int clientid) {
	int len=0,count=0,f,flags,select_out,leng_recv=0;
	char str[2048],buf[1024],file[50];
    while (1) {
    set_timer(acc[clientid]);
       if (FD_ISSET(acc[clientid],&set) ) {
            memset(str,0,2048);
            // read data from socket
            leng_recv = recv(acc[clientid],str,2048, 0);
            str[leng_recv]='\0';
            if (leng_recv == 0) {
	            shutdown(acc[clientid],SHUT_RDWR);
                close(acc[clientid]);
                FD_CLR(acc[clientid],&set);
                acc[clientid]=-1;
                printf("\n%dth client closed without data\n",clientid);
            } else if (leng_recv < 0 && errno == EAGAIN) {
              printf ("\nno data");
              return;
            } else if (leng_recv< 0) {
              perror("Connection error");
              abort();
            } else {
           
                char *filename,*httpversion,*httpreqmeth,*line,*keepalive;
                char *badrequest;
        	    httpreqmeth=strtok (str," ");
                if ( strcmp(httpreqmeth, "GET\0")==0 )
        	    {
        	        filename=strtok (NULL," ");
        	        httpversion=strtok (NULL," \n");
        	        while((line=strtok(NULL,":"))!=NULL) {
                        if (strncmp(line,"Connection",10)==0) {
                           keepalive=strtok(NULL," \n");
                           printf ("got as %s\n", keepalive);
                           if (strncmp(keepalive,"keep-alive",10)==0 || strncmp(keepalive,"Keep-alive",10)==0) {
                               printf ("\nCurrent timeout value%d\n",(int)timeout.tv_sec);
                               timeout.tv_sec = TIMEOUT;
                               printf ("\nTimer reset to %d for %dth client\n",(int)timeout.tv_sec,clientid);
                           } else if (strcmp(keepalive,"close")==0 || strcmp(keepalive,"Close")==0) {
	                            shutdown(acc[clientid],SHUT_RDWR);
                                close(acc[clientid]);
                                FD_CLR(acc[clientid],&set);
                                printf("\n%dth client closed for: close in keep-alive\n",clientid);
                           }
                           break; 
                        } else {
                            line=strtok(NULL,"\n");
                        }
                    }
                }
        
                if (strncmp(httpreqmeth,"GET",3)!=0) { 
                    sprintf (badrequest,"HTTP/1.0 400 Bad Request: Invalid Method:%s\r\n\r\n<html><body>INVALID METHOD:%s</body></html>\r\n\r\n",httpreqmeth,httpreqmeth);
                    send(acc[clientid], badrequest, strlen(badrequest),0);
                }
        
                if ( strncmp(httpversion, "HTTP/1.0", 8)!=0 && strncmp( httpversion, "HTTP/1.1", 8)!=0 ) {
                     sprintf (badrequest,"HTTP/1.0 400 Bad Request: Invalid HTTP‐Version:%s\r\n\r\n<html><body>Invalid HTTP‐Version:%s</body></html>\r\n\r\n",httpversion,httpversion);
                     send(acc[clientid], badrequest, strlen(badrequest),0);
                }
        	    
                strcpy(file,path);
        	    if (strcmp(filename,"/\0")==0) {
                    strcat(file,"/");
                    strcat(file,defaultfile);
        	    } else {
                    strcat(file,filename);
                }
        	    char *header;
                header=parseHeader(file,header,clientid);
                len=strlen(header);
             
                printf ("\nFile under process: %s \n", file);	
        	    if ((f=open(file,O_RDONLY))!=-1) {
        	     	while ( (count=read(f, buf, 1024))>0 ) {
        	     		len=send(acc[clientid], buf, count,0);
						memset(buf,0,1024);
                 		}
                
                	if (keepalive==NULL) {
                        shutdown(acc[clientid],SHUT_RDWR);
                        close(acc[clientid]);
                        FD_CLR(acc[clientid],&set);
                        printf("\n%dth client closed : Connection keepalive not found\n",clientid);
                        break;
                	}
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

void sigchld_handler(int s)
{
    int saved_errno = errno;
    while(waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

int main(int argc, char *argv[]) {

	int siz_sock=sizeof(struct sockaddr_in);
	int clientid=0,resp=1,num_conn=0;
    long z;
	int i,j=0,k=0;
    memset(&path,0,15);
    memset(&defaultfile,0,15);
    int port=parseWSconf();
    for (i=0;i<=CONNCTIONS;i++) {acc[i]=-1;timer[i]=-1;}

    if (argc!=0 && argv[1]!=NULL) {
		port=atoi(argv[1]);
        if (port<=1000) {
            printf("Use ports above 1000");
        }	
	}
    printf("post %d",port);
    acc[0]=getSocketId(port);
    FD_ZERO (&set);
    printf ("Server Socket id:%d\n",acc[0]);
	
    struct sigaction sa;
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    while (1) {
        if ((acc[++conn_estb]=accept(acc[0],(struct sockaddr*)NULL,&siz_sock))==-1) {
	      printf("accept failed");
	    } else {
	    	// Once server gets connection from client, create a child process
            int f=fork();
            if ( f==0 ) { 
                    resp=httpresponse(conn_estb);
            }
         }

	}
}
