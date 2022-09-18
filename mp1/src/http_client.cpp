/*
** client.c -- a stream socket client demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <string>
#include <fstream>

// #define PORT "3490" // the port client will be connecting to 

#define MAXDATASIZE 10000000 // max number of bytes we can get at once 

using namespace std;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
	int sockfd, numbytes;  
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
	string url, protocol, host, port, file;
	size_t protocolEnd, hostEnd, portEnd;

	if (argc != 2) {
	    fprintf(stderr,"usage: client hostname\n");
	    exit(1);
	}

	url = argv[1];
	protocolEnd = url.find("://");
	protocol = url.substr(0, protocolEnd);
	hostEnd = url.find(":", protocolEnd+3);
	if (hostEnd != string::npos) {
		host = url.substr(protocolEnd+3, hostEnd - (protocolEnd+3));
		portEnd = url.find("/", hostEnd+1);
		port = url.substr(hostEnd+1, portEnd - (hostEnd+1));
	} else {
		hostEnd = url.find("/", protocolEnd+3);
		host = url.substr(protocolEnd+3, hostEnd - (protocolEnd+3));
		portEnd = hostEnd;
		port= "80";
	}
	// file path should start with /
	file = url.substr(portEnd, url.size());

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(host.c_str(), port.c_str(), &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);
	printf("client: connecting to %s\n", s);

	freeaddrinfo(servinfo); // all done with this structure

	string httpRequest = "";
	httpRequest.append("GET ").append(file).append(" HTTP/1.1\r\n")
		.append("User-Agent: Wget/1.12 (linux-gnu)\r\n")
		.append("Host: ").append(host).append(":").append(port).append("\r\n")
		.append("Connection: Keep-Alive\r\n\r\n");

	if ((numbytes = send(sockfd, httpRequest.c_str(), httpRequest.size(), 0)) == -1) {
		perror("send");
		exit(1);
	}

	while(1) {
		if ((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) == -1) {
	    	perror("recv");
	    	// exit(1);
			continue;
		}
		buf[numbytes] = '\0';
		string received = buf;
		size_t headerEnd = received.find("\r\n\r\n");
		string header, document;
		if(headerEnd != string::npos) {
			header = received.substr(0, headerEnd);
			document = received.substr(headerEnd+4, received.size()-header.size()-4);
		} else {
			header = buf;
			document = "";
		}
		printf("client: received:\n '%s'\n",header.c_str());
		ofstream output;
		output.open("output");
		if(output.is_open()) {
			output << document;
			output.close();			
		} else {
			perror("cannot open output");
		}
		break;
	}


	close(sockfd);

	return 0;
}

