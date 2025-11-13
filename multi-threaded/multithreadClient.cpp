#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>
#include <thread>

constexpr const char* SERVER_PORT = "54000";
constexpr size_t MAX_MSG_SIZE = 4096;

ssize_t send_all(int sock, const void* buf, size_t len)
{
	const char* data = static_cast<const char*>(buf);
	size_t total = 0;

	while (total < len) {
		ssize_t n = send(sock, data + total, len - total, 0);
		if (n <= 0) return n;
		total += static_cast<size_t>(n);
	}
	return static_cast<ssize_t>(total);
}

ssize_t recv_all(int sock, void* buf, size_t len)
{
	char* data = static_cast<char*>(buf);
	size_t total = 0;

	while (total < len) {
		ssize_t n = recv(sock, data + total, len - total, 0);
		if (n <= 0) return n;
		total += static_cast<size_t>(n);
	}
	return static_cast<ssize_t>(total);
}

void receive_loop(int sock)
{
	while (true) {
		uint32_t len_net = 0;
		ssize_t n = recv_all(sock, &len_net, sizeof(len_net));
		if (n <= 0) {
			std::cout << "[*] Server closed connection or error\n";
			break;
		}

		uint32_t len = ntohl(len_net);
		if (len == 0 || len > MAX_MSG_SIZE) {
			std::cout << "[!] Invalid length from server: " << len << "\n";
			break;
		}

		std::string msg(len, '\0');
		n = recv_all(sock, msg.data(), len);
		if (n <= 0) {
			std::cout << "[*] Server closed connection while receiving body\n";
			break;
		}

		std::cout << "[Srv]: " << msg << "\n";
	}
}

int main(int argc, char* argv[])
{
	const char* server_host = "127.0.0.1";
	if (argc >= 2) {
		server_host = argv[1];
	}

	addrinfo hints{};
	hints.ai_family = AF_UNSPEC;    // IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;// TCP

	addrinfo* res = nullptr;
	int rv = getaddrinfo(server_host, SERVER_PORT, &hints, &res);
	if (rv != 0) {
		std::cerr << "getaddrinfo: " << gai_strerror(rv) << "\n";
		return 1;
	}

	int sockfd = -1;
	addrinfo* p = nullptr;

	for (p = res; p != nullptr; p = p->ai_next) {
		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sockfd < 0) {
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			sockfd = -1;
			continue;
		}

		// success
		break;
	}

	freeaddrinfo(res);

	if (sockfd < 0) {
		std::cerr << "Failed to connect to " << server_host << " on port " << SERVER_PORT << "\n";
		return 1;
	}

	std::cout << "[*] Connected to " << server_host << ":" << SERVER_PORT << "\n";
	std::cout << "Type messages and press Enter. Type /quit to exit.\n";

	// Receive thread for bidirectional comms
	std::thread recv_thread(receive_loop, sockfd);

	// Main thread: read stdin and send messages
	std::string line;
	while (std::getline(std::cin, line)) {
		if (line == "/quit") break;

		auto len = static_cast<uint32_t>(line.size());
		uint32_t len_net = htonl(len);

		// send length prefix + message body
		if (send_all(sockfd, &len_net, sizeof(len_net)) <= 0 ||
			send_all(sockfd, line.data(), len) <= 0) {
			std::perror("send");
			break;
		}
	}

	shutdown(sockfd, SHUT_WR);

	if (recv_thread.joinable()) {
		recv_thread.join();
	}

	close(sockfd);
	std::cout << "[*] Client exiting\n";
	return 0;
}
