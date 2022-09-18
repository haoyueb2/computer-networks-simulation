/*
** server.c -- a stream socket server demo
*/
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <unistd.h>
#include <cstdio>
#include <sys/stat.h>
using namespace std;

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

#define PORT "3490"  // the port users will be connecting to

#define MAX_MESSAGE_SIZE 100000

#define BACKLOG 10	 // how many pending connections queue will hold

void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

vector<string> split(string str, string deliminator, int times = -1) {
    vector<string> ret;
    int counter = 0;
    int ptr = 0;
    int next_ptr = 0;
    while (true) {
        next_ptr = str.find(deliminator, ptr);
        if (next_ptr == string::npos) {
            break;
        }
        ret.push_back(str.substr(ptr, next_ptr - ptr));
        ptr = next_ptr + 1;
        counter += 1;
        if (counter == times) {
            break;
        }
    }
    if (ptr != str.size()) {
        ret.push_back(str.substr(ptr, str.size() - ptr));
    }
    return ret;
}

string read_file(string file_dir) {
    char buf[file_dir.size() + 1];
    strcpy(buf, file_dir.c_str());
    buf[file_dir.size()] = '\0';
    string file_content = "";
    FILE* fp = fopen(buf, "r");
	if (!fp) {
		return file_content;
	}
    struct stat file_info;
    stat(file_dir.c_str(), &file_info);
    file_content.resize(file_info.st_size);
    fread(const_cast<char*>(file_content.data()), file_info.st_size, 1, fp);
    return file_content;
}

string get_http_response(string request) {
    vector<string> infos = split(request, "\r\n");
    string response = "";
    string header = infos[0];
    auto request_line_infos = split(header, " ");
    response += "HTTP/1.1 ";
    if (request_line_infos.size() != 3 || request_line_infos[0] != "GET") {
        response += "400 Bad Request\r\n\r\n";
        return response;
    }
    string file_dir = request_line_infos[1].substr(1, request_line_infos[1].size()-1);
    string file_content = read_file(file_dir);
    if (file_content.size() == 0) {
        response += "404 Not Found\r\n\r\n";
        return response;
    }
    response += "200 OK\r\n\r\n";
    response += file_content;
    return response;
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
	string port;
	if (argc != 2) {
	    fprintf(stderr,"usage: server port\n");
	    exit(1);
	}
	port = argv[1];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, port.c_str(), &hints, &servinfo)) != 0) {
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

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo); // all done with this structure

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
            char buf[MAX_MESSAGE_SIZE];
            if (recv(new_fd, buf, MAX_MESSAGE_SIZE, 0)) {
                perror("send");
            }
            string response = get_http_response(string(buf));
            char char_resp[response.size() + 1];
            strcpy(char_resp, response.c_str());
            char_resp[response.size()] = '\0';
			if (send(new_fd, char_resp, response.size(), 0) == -1)
				perror("send");
			close(new_fd);
			exit(0);
		}
		close(new_fd);  // parent doesn't need this
	}

	return 0;
}
