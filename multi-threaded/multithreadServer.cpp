#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <queue>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

constexpr const char* SERVER_PORT = "54000";
constexpr size_t MAX_MSG_SIZE = 4096;
constexpr int BACKLOG = 10;
constexpr int THREAD_POOL_SIZE = 4;

struct ClientTask {
	int socket;
	sockaddr_storage address;
};

std::queue<ClientTask> task_queue;
std::mutex queue_mutex;
std::condition_variable condition;
bool stop_server = false;

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

std::string addr_to_string(const sockaddr_storage& addr)
{
	char host[NI_MAXHOST], serv[NI_MAXSERV];
	int rv = getnameinfo(reinterpret_cast<const sockaddr*>(&addr),
						 (addr.ss_family == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6),
						 host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
	return (rv == 0) ? (std::string(host) + ":" + serv) : "unknown";
}

void process_client(ClientTask task)
{
	std::string peer = addr_to_string(task.address);
	std::cout << "worker " << std::this_thread::get_id() << " handling " << peer << "\n";

	while (true) {
		uint32_t len_net = 0;
		if (recv_all(task.socket, &len_net, sizeof(len_net)) <= 0) break;

		uint32_t len = ntohl(len_net);
		if (len == 0 || len > MAX_MSG_SIZE) break;

		std::string msg(len, '\0');
		if (recv_all(task.socket, msg.data(), len) <= 0) {
			break;
		}

		uint32_t resp_len_net = htonl(len);
		if (send_all(task.socket, &resp_len_net, sizeof(resp_len_net)) <= 0 || send_all(task.socket, msg.data(), len) <= 0) {
			break;
		}
	}

	close(task.socket);
	std::cout << "connection closed for " << peer << "\n";
}

void worker_thread()
{
	while (true) {
		ClientTask task;
		{
			std::unique_lock<std::mutex> lock(queue_mutex);
			condition.wait(lock, []() { return !task_queue.empty() || stop_server; });

			if (stop_server && task_queue.empty()) return;

			task = task_queue.front();
			task_queue.pop();
		}
		process_client(task);
	}
}

int main()
{
	addrinfo hints{}, *server_info;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo(nullptr, SERVER_PORT, &hints, &server_info) != 0) return 1;

	int sockfd = -1;
	for (addrinfo* p = server_info; p != nullptr; p = p->ai_next) {
		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sockfd < 0) continue;
		int opt = 1;
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == 0) break;
		close(sockfd);
		sockfd = -1;
	}
	freeaddrinfo(server_info);

	if (sockfd == -1 || listen(sockfd, BACKLOG) == -1) return 1;

	std::vector<std::thread> pool;
	for (int i = 0; i < THREAD_POOL_SIZE; ++i) {
		pool.emplace_back(worker_thread);
	}

	std::cout << "server listening with pool of " << THREAD_POOL_SIZE << " threads\n";

	while (true) {
		sockaddr_storage client_addr{};
		socklen_t addr_len = sizeof(client_addr);
		int client_sock = accept(sockfd, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);

		if (client_sock < 0) {
			continue;
		}

		{
			std::lock_guard<std::mutex> lock(queue_mutex);
			task_queue.push({client_sock, client_addr});
		}

		condition.notify_one();
	}

	return 0;
}
