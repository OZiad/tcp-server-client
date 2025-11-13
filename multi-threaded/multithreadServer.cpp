#include <iostream>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <thread>

constexpr const char* SERVER_PORT = "54000";
constexpr size_t MAX_MSG_SIZE = 4096;
constexpr int BACKLOG = 10;

/**
 * Send exactly len bytes
 */
ssize_t send_all(int sock, const void* buf, size_t len)
{
	const char* data = static_cast<const char*>(buf);
	size_t total = 0;

	while (total < len) {
		ssize_t n = send(sock, data + total, len - total, 0);
		if (n <= 0) return n;// error or closed
		total += static_cast<size_t>(n);
	}
	return static_cast<ssize_t>(total);
}

/**
 * Receive exactly len bytes (or 0 if peer closed)
 */
ssize_t recv_all(int sock, void* buf, size_t len)
{
	char* data = static_cast<char*>(buf);
	size_t total = 0;

	while (total < len) {
		ssize_t n = recv(sock, data + total, len - total, 0);
		if (n <= 0) return n;// error or closed
		total += static_cast<size_t>(n);
	}
	return static_cast<ssize_t>(total);
}

/**
 * Convert sockaddr to "ip:port" string for logging
 */
std::string addr_to_string(const sockaddr_storage& addr)
{
	char host[NI_MAXHOST];
	char serv[NI_MAXSERV];

	int rv = getnameinfo(
		reinterpret_cast<const sockaddr*>(&addr),
		(addr.ss_family == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6),
		host, sizeof(host),
		serv, sizeof(serv),
		NI_NUMERICHOST | NI_NUMERICSERV);

	if (rv != 0) {
		return "unknown";
	}
	return std::string(host) + ":" + serv;
}

void handle_client(int client_sock, sockaddr_storage client_addr)
{
	std::string peer = addr_to_string(client_addr);
	std::cout << "[+] New connection from " << peer << "\n";

	while (true) {
		// Read 4-byte length prefix
		uint32_t len_net = 0;
		ssize_t n = recv_all(client_sock, &len_net, sizeof(len_net));

		if (n == 0) {
			std::cout << "[*] Client " << peer << " disconnected\n";
			break;
		}

		if (n < 0) {
			std::perror("recv length");
			break;
		}

		uint32_t len = ntohl(len_net);
		if (len == 0 || len > MAX_MSG_SIZE) {
			std::cout << "[!] Invalid message length (" << len << ") from " << peer << ", closing connection\n";
			break;
		}

		std::string msg(len, '\0');
		n = recv_all(client_sock, msg.data(), len);
		if (n <= 0) {
			std::perror("recv body");
			break;
		}

		std::cout << "[Rcv] " << peer << " -> \"" << msg << "\"\n";

		// Echo back
		uint32_t resp_len_net = htonl(len);
		if (send_all(client_sock, &resp_len_net, sizeof(resp_len_net)) <= 0 ||
			send_all(client_sock, msg.data(), len) <= 0) {
			std::perror("send");
			break;
		}

		std::cout << "[Snd] Echoed back to " << peer << "\n";
	}

	close(client_sock);
	std::cout << "[*] Closed connection for " << peer << "\n";
}

/**
 * receive client req -> let thread handle it (send data)
 * continue listening on main thread.
 */
int main()
{
	addrinfo hints{}, *server_info;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	int rv = getaddrinfo(nullptr, SERVER_PORT, &hints, &server_info);
	if (rv != 0) {
		std::cerr << "getaddrinfo: " << gai_strerror(rv) << "\n";
		return 1;
	}

	int sockfd = -1;
	addrinfo* p = nullptr;

	for (p = server_info; p != nullptr; p = server_info->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
			std::cerr << "server: socket" << "\n";
			continue;
		}

		int opt = 1;
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
			close(sockfd);
			sockfd = -1;
		}
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			sockfd = -1;
			continue;
		}

		// success
		break;
	}

	freeaddrinfo(server_info);
	if (p == nullptr) {
		std::cerr << "server : failed to bind" << "\n";
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		std::cerr << "listen failed" << "\n";
		exit(1);
	}
	std::cout << "[*] Server listening on port " << SERVER_PORT << "\n";

	//	one thread per client
	while (true) {
		sockaddr_storage client_addr{};
		socklen_t addr_len = sizeof(client_addr);

		int client_sock = accept(
			sockfd,
			reinterpret_cast<sockaddr*>(&client_addr),
			&addr_len);

		if (client_sock < 0) {
			std::perror("accept");
			continue;
		}

		std::thread t(handle_client, client_sock, client_addr);
		t.detach();
	}

	close(sockfd);
	return 0;
}