#include <iostream>
#include <fstream>
#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h>
#include <sys/wait.h> 
#include <fcntl.h>
#include <sys/sendfile.h>

using namespace std;

#define PORT 8081
#define MAX_CLIENTS 10
#define MAX_BUF_LEN 1024
#define MAX_METHOD_LEN 10
#define MAX_PATH_LEN 800
#define MAX_HTTPSTR_LEN 12
#define MAX_ASSET_LEN 16

unsigned int client_count;

void sigchld_handler(int signo)
{
    int status;
    pid_t pid;

    while( (pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
		client_count--;
    	printf("%i exited with %i\n", pid, WEXITSTATUS(status));
    }

    return;
}

long int find_file_size(char file_name[]) 
{     
    FILE* fp = fopen(file_name, "r");

    if (fp == NULL) { 
        printf("File Not Found!\n"); 
        return -1; 
    }

    fseek(fp, 0L, SEEK_END); 
    long int res = ftell(fp); 
    fclose(fp); 
    return res; 
} 

int parse_first_http(const char* http_req, const int req_len, char* asset)
{
	char method[MAX_METHOD_LEN];
	char path[MAX_PATH_LEN];
	char http[MAX_HTTPSTR_LEN];

	int i = 0;
	int j = 0;
	
	if(http_req == NULL)
		return -1;

	printf("REQUEST-LEN: %d\n",req_len); 

	while( http_req[i] != ' ' )
	{
		if(http_req == NULL)
			return -2;

		if(j >= MAX_METHOD_LEN - 1)
			return -3;

		if(i == req_len)
			return -4;

		method[j++] = http_req[i++];
	}

	method[j] = '\0';

	printf("METHOD: %s\n", method);	
	if(strcmp(method, "GET") != 0)
		return -11;

	j = 0;
	i++;

	while( http_req[i] != ' ' && (http_req != NULL)) 
	{
		if(j >= MAX_PATH_LEN-1)
			return -5;

		if(i == req_len)
			return -6;

		path[j++] = http_req[i++];
	}

	path[j] = '\0';

	printf("PATH: %s\n",path);
	i++;
	j = 0;

	while( http_req[i] != ' ' && http_req[i] != '\r' && (http_req != NULL)) 	
	{
		if(j >= MAX_HTTPSTR_LEN-1)
			return -7;

		if(i == req_len)
			return -8;

		http[j++] = http_req[i++];
	}

	http[j] = '\0';
	printf("HTTP-STR: %s\n",http);
	if((strcmp(http,"HTTP/1.0") != 0) && (strcmp(http,"HTTP/1.1") != 0))
		return -12;
    
	if(strlen(path) == 1){
		return 1;
	}
	else if(strlen(path) > 1){
        strcpy(asset, path+1);
        return 2;
    }
}

int handle_client(int connfd)
{
	char buffer[MAX_BUF_LEN + 1];
    char asset[MAX_ASSET_LEN];

	int n = 0;
	int req_type = 0;
    int fd_asset;

	const char sendok[1024] = 
	"HTTP/1.0 200 OK\r\n"
	"Server: SimpleWebserver\r\n"
	"Content-Type: text/html\r\n\n";

	n = recv(connfd, buffer, MAX_BUF_LEN, 0);

	if(n <= 0)
	{
		perror("recv() error..");
		close(connfd);
		return 0;
	}
	buffer[n] = '\0';
	
	printf("HTTP-REQUEST: %s\n", buffer);

	req_type = parse_first_http(buffer, n, asset);	
	if(req_type == 1)
	{
        printf("single line request\n");        
		send(connfd, sendok, strlen(sendok), 0);
		printf("HTTP-OK SENT\n");
		char def_index[] = {"index.html"};

		fd_asset = open("index.html", O_RDONLY);   
		long int file_size = find_file_size(def_index);
		if(sendfile(connfd, fd_asset, NULL, file_size) < 0){
			perror("send() error..");
			close(fd_asset);
		}
		close(fd_asset);
		close(connfd);
	}
    else if(req_type == 2){
        printf("it's a asset request\n");
        long file_size = find_file_size(asset);
		fd_asset = open(asset, O_RDONLY);
        send(connfd, sendok, strlen(sendok), 0);
        sendfile(connfd, fd_asset, NULL, file_size);
        close(fd_asset);
		printf("HTTP-OK SENT ASSET\n");
    }
	else
	{
		fprintf(stderr,"parse_first_http() error: %d\n",req_type);
		close(connfd);
		return -1;
	}
    
	return 0;
}


int main(int argc, char const *argv[]) 
{   
    client_count = 0;

    struct sockaddr_in server_address, client_address;     
    struct sigaction sa;

    socklen_t sin_len = sizeof(client_address);

    pid_t child_pid;    

    int server_fd, client_fd;
    int opt = 1;    


    server_fd = socket(AF_INET, SOCK_STREAM, 0);     
    if (server_fd < 0){ 
        perror("socket failed"); 
        exit(EXIT_FAILURE); 
    } 
           
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(int)) < 0) 
    { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    } 

    server_address.sin_family = AF_INET; 
    server_address.sin_addr.s_addr = INADDR_ANY; 
    server_address.sin_port = htons(PORT); 

    if (bind(server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) 
    { 
        perror("bind failed"); 
        close(server_fd);
        exit(EXIT_FAILURE); 
    } 
    if (listen(server_fd, 3) < 0) 
    { 
        perror("listen"); 
        exit(EXIT_FAILURE); 
    } 

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigaction(SIGCHLD, &sa, NULL);

    while (1)
    {
        if(client_count < MAX_CLIENTS){
            client_fd = accept(server_fd, (struct sockaddr *) &client_address, &sin_len);

            if(client_fd < 0){
                if (errno == EINTR)
					continue;

				perror("accept() error..");
				close(server_fd);
				exit(EXIT_FAILURE);
            }
            client_count++;
            child_pid = fork();

            if(child_pid < 0)
			{
				perror("fork() failed");
				exit(EXIT_FAILURE);
			}

			if(child_pid  == 0)
			{
				close(server_fd);
				exit(handle_client(client_fd));
			}
			
			printf("\nClient PID %i\n",child_pid);

			close(client_fd);            
        }        
    }
    return 0; 
} 