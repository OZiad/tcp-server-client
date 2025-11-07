#include <arpa/inet.h>
#include <cerrno>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

constexpr const char* PORT = "3490";
constexpr int BACKLOG = 10;


void sigchld_handler(int)
{
	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;
	while (waitpid(-1, nullptr, WNOHANG) > 0) {}
	errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void* get_in_addr(struct sockaddr* sa)
{
	if (sa->sa_family == AF_INET) {
		return &((reinterpret_cast<sockaddr_in*>(sa))->sin_addr);
	}

	return &((reinterpret_cast<sockaddr_in6*>(sa))->sin6_addr);
}

int main()
{
	// listen on sock_fd, new connection on new_fd
	int sockfd{-1}, new_fd{-1};
	struct addrinfo hints{}, *server_info = nullptr, *p = nullptr;
	struct sockaddr_storage their_addr{};
	socklen_t sin_size{};
	struct sigaction sa{};
	int yes = 1;
	char s[INET6_ADDRSTRLEN]{};

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if (int rv = getaddrinfo(nullptr, PORT, &hints, &server_info) != 0) {
		std::cerr << "getaddrinfo: " << gai_strerror(rv) << "\n";
		return 1;
	}

	// loop through all the results and bind to the first we can
	for (p = server_info; p != nullptr; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
			perror("setsockopt");
			::close(sockfd);
			freeaddrinfo(server_info);
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			::close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	freeaddrinfo(server_info);

	if (p == nullptr) {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler;// reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, nullptr) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while (true) {
		sin_size = sizeof their_addr;
		new_fd = ::accept(sockfd, (struct sockaddr*) &their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
				  get_in_addr((struct sockaddr*) &their_addr),
				  s, sizeof s);
		printf("server: got connection from %s\n", s);

		if (!fork()) {
			::close(sockfd);// child doesn't need the listener
			if (send(new_fd, "Hello, world!", 13, 0) == -1) {
				perror("send");
			}
			::close(new_fd);
			exit(0);
		}
		::close(new_fd);// parent doesn't need this
	}

	return 0;
}