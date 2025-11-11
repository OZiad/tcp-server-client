#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

constexpr const char* PORT = "9034";

/**
 * Convert socket to IP address string.
 * addr: struct sockaddr_in or struct sockaddr_in6
 */
const char*
inet_ntop2(const void* addr, char* buf, size_t size)
{
	if (size == 0) return nullptr;

	const void* src = nullptr;
	auto* sas = reinterpret_cast<const sockaddr_storage*>(addr);
	int family = sas->ss_family;

	switch (family) {
		case AF_INET: {
			auto sa4 = reinterpret_cast<const sockaddr_in*>(addr);
			src = &(sa4->sin_addr);
			break;
		}
		case AF_INET6: {
			auto sa6 = reinterpret_cast<const sockaddr_in6*>(addr);
			src = &(sa6->sin6_addr);
			break;
		}
		default:
			return nullptr;
	}

	return ::inet_ntop(sas->ss_family, src, buf, static_cast<socklen_t>(size));
}

/**
 * Return a listening socket.
 */
int get_listener_socket()
{
	int listener;
	int yes = 1;
	int rv;

	addrinfo hints{}, *ai, *p;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((rv = getaddrinfo(nullptr, PORT, &hints, &ai)) != 0) {
		fprintf(stderr, "pollserver: %s\n", gai_strerror(rv));
		exit(1);
	}

	for (p = ai; p != nullptr; p = p->ai_next) {
		listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (listener < 0) {
			continue;
		}

		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

		if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
			close(listener);
			continue;
		}

		break;
	}

	if (p == nullptr) {
		return -1;
	}

	freeaddrinfo(ai);

	if (listen(listener, 10) == -1) {
		return -1;
	}

	return listener;
}

/**
 * Add a new file descriptor to the set.
 */
void add_to_pfds(pollfd** pfds, int newfd, int* fd_count, int* fd_size)
{
	if (*fd_count == *fd_size) {
		*fd_size *= 2;
		*pfds = static_cast<pollfd*>(realloc(*pfds, sizeof(**pfds) * (*fd_size)));
	}

	(*pfds)[*fd_count].fd = newfd;
	(*pfds)[*fd_count].events = POLLIN;// Check ready-to-read
	(*pfds)[*fd_count].revents = 0;

	(*fd_count)++;
}

/**
 * Remove a file descriptor at a given index from the set.
 */
void del_from_pfds(pollfd pfds[], int i, int* fd_count)
{
	// Copy the one from the end over this one
	pfds[i] = pfds[*fd_count - 1];

	(*fd_count)--;
}

/**
 * Handle incoming connections.
 */
void handle_new_connection(int listener, int* fd_count, int* fd_size, pollfd** pfds)
{
	sockaddr_storage remote_addr{};// Client address
	socklen_t addrlen;
	int newfd;// Newly accept()ed socket descriptor
	char remoteIP[INET6_ADDRSTRLEN];

	addrlen = sizeof remote_addr;
	newfd = accept(listener, (sockaddr*) &remote_addr, &addrlen);

	if (newfd == -1) {
		perror("accept");
	} else {
		add_to_pfds(pfds, newfd, fd_count, fd_size);

		printf("pollserver: new connection from %s on socket %d\n",
			   inet_ntop2(&remote_addr, remoteIP, sizeof remoteIP),
			   newfd);
	}
}

/**
 * Handle regular client data or client hangups.
 */
void handle_client_data(int listener, int* fd_count, pollfd* pfds, int* pfd_i)
{
	char buf[256];// Buffer for client data

	int nbytes = recv(pfds[*pfd_i].fd, buf, sizeof buf, 0);

	int sender_fd = pfds[*pfd_i].fd;

	if (nbytes <= 0) {// Got error or connection closed by client
		if (nbytes == 0) {
			// Connection closed
			printf("pollserver: socket %d hung up\n", sender_fd);
		} else {
			perror("recv");
		}

		close(pfds[*pfd_i].fd);// Bye!

		del_from_pfds(pfds, *pfd_i, fd_count);

		// reexamine the slot we just deleted
		(*pfd_i)--;

	} else {// We got some good data from a client
		printf("pollserver: recv from fd %d: %.*s", sender_fd,
			   nbytes, buf);
		// Send to everyone!
		for (int j = 0; j < *fd_count; j++) {
			int dest_fd = pfds[j].fd;

			// Except the listener and ourselves
			if (dest_fd != listener && dest_fd != sender_fd) {
				if (send(dest_fd, buf, nbytes, 0) == -1) {
					perror("send");
				}
			}
		}
	}
}

/**
 * Process all existing connections.
 */
void process_connections(int listener, int* fd_count, int* fd_size, pollfd** pfds)
{
	for (int i = 0; i < *fd_count; i++) {

		// Check if someone's ready to read
		if ((*pfds)[i].revents & (POLLIN | POLLHUP)) {
			// We got one!!

			if ((*pfds)[i].fd == listener) {
				// If we're the listener, it's a new connection
				handle_new_connection(listener, fd_count, fd_size,
									  pfds);
			} else {
				// Otherwise we're just a regular client
				handle_client_data(listener, fd_count, *pfds, &i);
			}
		}
	}
}

/**
 * Main: create a listener and connection set, loop forever
 * processing connections.
 */
int main(void)
{
	int listener;// Listening socket descriptor

	// Start off with room for 5 connections
	// (We'll realloc as necessary)
	int fd_size = 5;
	int fd_count = 0;
	pollfd* pfds = static_cast<pollfd*>(malloc(sizeof *pfds * fd_size));

	// Set up and get a listening socket
	listener = get_listener_socket();

	if (listener == -1) {
		fprintf(stderr, "error getting listening socket\n");
		exit(1);
	}

	// Add the listener to set;
	// Report ready to read on incoming connection
	pfds[0].fd = listener;
	pfds[0].events = POLLIN;

	fd_count = 1;// For the listener

	puts("pollserver: waiting for connections...");

	// Main loop
	for (;;) {
		int poll_count = poll(pfds, fd_count, -1);

		if (poll_count == -1) {
			perror("poll");
			exit(1);
		}

		// Run through connections looking for data to read
		process_connections(listener, &fd_count, &fd_size, &pfds);
	}

	free(pfds);
}