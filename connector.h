#ifndef CONNECTOR_H
#define CONNECTOR_H

#include <list>
#include <vector>
#include <fstream>
#include <atomic>
#include <chrono>
#include <memory>
#include <sys/epoll.h>
#include <cstdint>

#include "negotiator.h"

class connector
{
public:
	connector(std::istream& input, std::ostream& output, int port);

	void run();
	void die();
	void cont();

	inline void set_to_terminal(bool to_terminal) { this->to_terminal = to_terminal; }
	inline bool get_to_terminal() { return to_terminal; }

	inline void set_skip(int skip) { this->skip = skip; }
	inline int get_skip() { return skip; }

	inline void set_maxcon(size_t maxcon) { this->maxcon = maxcon; }
	inline size_t get_maxcon() { return maxcon; }

	inline void set_ttl(int ttl) { this->ttl = ttl; }
	inline int get_ttl() { return ttl; }

	inline void set_conn_rate(int conn_rate) { this->conn_rate = conn_rate; }
	inline int get_conn_rate() { return conn_rate; }

	inline void set_prov(std::shared_ptr<negotiator_provider> prov) { this->prov = prov; }
	inline std::shared_ptr<negotiator_provider> get_prov() { return prov; }

private:
	struct conn_entry
	{
		int sockfd;
		std::chrono::time_point<std::chrono::high_resolution_clock> ts;
		bool connected;
		std::string ip;
		std::string str;

		std::shared_ptr<negotiator> negot;

		/* Keep an iterator to ourself, in order to do fast removal of entries from the list */
		std::list<conn_entry>::iterator it;
	};

	void check_timeouts(std::chrono::time_point<std::chrono::high_resolution_clock> ts);
	int newcon(const char* host, int port);
	void print_stats();
	void write_to_file(conn_entry& ce);
	static char* getip(int fd);
	static std::string escape(std::string s);
	void check_sockets(int timeout);
	void epoll_conn(conn_entry& ce, int op);

	std::istream& input;
	std::ostream& output;
	std::streampos insize;

	int port;
	bool to_terminal = false;
	int skip = 0;
	size_t maxcon = 10;
	int ttl = 60;
	int conn_rate = 1;
	std::shared_ptr<negotiator_provider> prov = nullptr;

	std::vector<epoll_event> events;
	std::list<conn_entry> ces;

	std::atomic<bool> running;
	size_t ces_size = 0;
	int maxfd = -1;
	int total_lines = 0;
	int total_lines_cont = 0;
	int total_connections = 0;

	int epollfd;

	std::chrono::time_point<std::chrono::high_resolution_clock> cont_start;
};

#endif /* CONNECTOR_H */

