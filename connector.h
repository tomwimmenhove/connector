#ifndef CONNECTOR_H
#define CONNECTOR_H

#include <list>
#include <vector>
#include <fstream>
#include <atomic>
#include <chrono>
#include <poll.h>
#include <memory>

#include "negotiator.h"

class connector
{
public:
	connector(std::istream& input, std::ostream& output, int port);

	void run();
	void die();
	void cont();

	void set_skip(int skip) { this->skip = skip; }
	int get_skip() { return skip; }

	void set_maxcon(size_t maxcon) { this->maxcon = maxcon; }
	size_t get_maxcon() { return maxcon; }

	void set_ttl(int ttl) { this->ttl = ttl; }
	int get_ttl() { return ttl; }

	void set_conn_rate(int conn_rate) { this->conn_rate = conn_rate; }
	int get_conn_rate() { return conn_rate; }

	void set_prov(std::shared_ptr<negotiator_provider> prov) { this->prov = prov; }
	std::shared_ptr<negotiator_provider> get_prov() { return prov; }

private:
	struct conn_entry
	{
		int sockfd;
		std::chrono::time_point<std::chrono::high_resolution_clock> ts;
		bool connected;
		pollfd* pfd;
		std::string ip;
		std::string str;

		std::shared_ptr<negotiator> negot;
	};

	int newcon(const char* host, int port);
	void print_stats();
	void write_to_file(conn_entry& ce);
	static char* getip(int fd);
	static std::string escape(std::string s);
	std::vector<pollfd> make_poll_vector();
	void check_sockets(std::vector<pollfd>& poll_vector, std::chrono::time_point<std::chrono::high_resolution_clock> ts, int timeout);

	std::istream& input;
	std::ostream& output;
	std::streampos insize;

	int port;
	int skip = 0;
	size_t maxcon = 10;
	int ttl = 60;
	int conn_rate = 1;
	std::shared_ptr<negotiator_provider> prov = nullptr;

	std::list<conn_entry> ces;

	std::atomic<bool> running;
	size_t ces_size = 0;
	int maxfd = -1;
	int total_lines = 0;
	int total_lines_cont = 0;
	int total_connections = 0;

	std::chrono::time_point<std::chrono::high_resolution_clock> cont_start;
};

#endif /* CONNECTOR_H */

