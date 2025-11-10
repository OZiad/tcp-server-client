#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <iostream>

constexpr const char* PORT = "3490";
constexpr int MAX_DATA_SIZE = 100;


void* get_in_addr(struct sockaddr* sa)
{
	if (sa->sa_family == AF_INET) {
		return &((reinterpret_cast<sockaddr_in*>(sa))->sin_addr);
	}

	return &((reinterpret_cast<sockaddr_in6*>(sa))->sin6_addr);
}

int main(int argc, char* argv[])
{
	int sockfd{-1}, numbytes{-1};
	char buf[MAX_DATA_SIZE]{};
	struct addrinfo hints{}, *server_info = nullptr, *p = nullptr;
	char s[INET6_ADDRSTRLEN];

	if (argc != 2) {
		fprintf(stderr, "usage: client hostname\n");
		exit(1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	int rv = getaddrinfo(argv[1], PORT, &hints, &server_info);
	if (rv != 0) {
		std::cerr << "getaddrinfo: " << gai_strerror(rv) << "\n";
		return 1;
	}

	// loop through all the results and connect to the first we can
	for (p = server_info; p != nullptr; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		inet_ntop(p->ai_family, get_in_addr((struct sockaddr*) p->ai_addr), s, sizeof s);
		printf("client: attempting connection to %s\n", s);

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("client: connect");
			::close(sockfd);
			continue;
		}

		break;
	}

	if (p == nullptr) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr*) p->ai_addr), s, sizeof s);
	printf("client: connected to %s\n", s);

	freeaddrinfo(server_info);// all done with this structure

	if ((numbytes = recv(sockfd, buf, MAX_DATA_SIZE - 1, 0)) == -1) {
		perror("recv");
		exit(1);
	}
	buf[numbytes] = '\0';
	printf("client: received '%s'\n", buf);

	::close(sockfd);

	return 0;
}