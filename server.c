#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define MAXDATASIZE 10000
#define BACKLOG 10	 // how many pending connections queue will hold
#define GET_METHOD 0
#define POST_METHOD 1

int requestType;
char requestFile[100];
char contents[10000];
char header[1000];
char table[10000];

void readFile(char *filename){
	FILE *fp;
	char str[1000];
	
	if (strcmp(filename, "/") == 0)
		strcpy(filename, "/index.html");

		fp = fopen(&filename[1], "r");
	
	if(fp != NULL){
		if (strstr(filename, ".css") != NULL)
			strcpy(header, "HTTP/1.1 200 OK\r\nContent-Type: text/css; charset=UTF-8\r\n\r\n");
		else if (strstr(filename, ".js") != NULL)
			strcpy(header, "HTTP/1.1 200 OK\r\nContent-Type: text/javascript; charset=UTF-8\r\n\r\n");
		else if (strstr(filename, ".html") != NULL)
			strcpy(header, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n");

		while(fgets(str, sizeof(str), fp) != NULL)
				strcat(contents, str);

		fclose(fp);
	} else if (errno == ENOENT){
		strcpy(header, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n");
		strcpy(contents, "<!DOCTYPE html><html><head><title>Page Not Found</title></head><body><h1>404 Not Found</h1></body></html>\r\n");
	} else if (errno == EACCES){
		strcpy(header, "HTTP/1.1 403 Forbidden\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n");
		strcpy(contents, "<!DOCTYPE html><html><head><title>Forbidden</title></head><body><h1>403 Forbidden</h1></body></html>\r\n");
	}
}

void sigchld_handler(int s)
{
	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void analyzeRequest(char *request){
	int i, j;
	char *pch;
	char temp[100];
	char *ch1, *ch2;
	char keys[100][100];
	char values[100][100];
	
	pch = strtok (request, " \t\n");
	for (i=0; i<3; i++){
		if (i == 0){
			if (strcmp("GET", pch) == 0) requestType = GET_METHOD;
			else if (strcmp("POST", pch) == 0) requestType = POST_METHOD;
		} else if (i == 1){
			strcpy(requestFile, pch);
		}
		if (i<2) pch = strtok (NULL, " \t\n");
	}

	pch = strtok(NULL, "\t\n");
	for(i=0; pch != NULL; i++){		
		strcpy(temp, pch);
		strtok_r(temp, ":", &ch1);
		strcpy(keys[i], temp);
		strcat(keys[i], "\0");
		strcpy(values[i], ch1);
		strcat(values[i], "\0");
		pch = strtok (NULL, "\t\n");
	}

	strcpy(table, "<html><head></head><body><table>");
	printf("<table>\n");
	for (j=0; j<i-1; j++){
		printf("<tr>\n");
		strcat(table, "<tr>");
		printf("%s %s\n", keys[j], values[j]);
		printf("<td>%s</td> <td>%s</td>\n", keys[j], values[j]);
		strcat(table, "<td>");
		strcat(table, keys[j]);
		strcat(table, "</td><td>");
		strcat(table, values[j]);
		strcat(table, "</td>");
		printf("</tr>\n");
		strcat(table, "</tr>");
	}
	printf("</table>\n");
	strcat(table, "</table></body></html>");

	// printf("%s\n", table);
}

int main(int argc, char *argv[])
{
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;
	char req[MAXDATASIZE];
	int numbytes;
	char response[10000];

	if (argc != 2) {
	    fprintf(stderr,"usage: %s <port>\n", argv[0]);
	    exit(1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("server: got connection from %s\n", s);

		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener
			
			if ((numbytes = recv(new_fd, req, MAXDATASIZE-1, 0)) == -1) {
				perror("recv");
				exit(1);
			}
			
			req[numbytes] = '\0';

			printf("server: received:\n%s\n", req);
			
			analyzeRequest(req);
			
			if (requestType == GET_METHOD){
				readFile(requestFile);
				strcat(response, header);
				strcat(response, contents);
				strcat(response, table);
				printf("\nResponse:\n%s\n", response);
				if (send(new_fd, response, strlen(response), 0) == -1)
					perror("send");

				close(new_fd);
				exit(0);
			} else if (requestType == POST_METHOD){

			} else {
				exit(0);
			}
		}
		close(new_fd);  // parent doesn't need this
	}

	return 0;
}

