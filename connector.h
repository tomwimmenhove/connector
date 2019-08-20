#ifndef CONNECTOR_H
#define CONNECTOR_H

#include <fstream>
#include <atomic>
#include <chrono>
#include <memory>
#include <sys/epoll.h>
#include <cstdint>
#include <errno.h>

#include "negotiator.h"
#include "conn_poller.h"
#include "conn_pool.h"

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

	inline void set_prov(std::shared_ptr<negotiator_provider> prov) { pool.set_prov(prov); }
	inline std::shared_ptr<negotiator_provider> get_prov() { return pool.get_prov(); }

private:
	conn_pool pool;

	int newcon(const char* host, int port);
	void print_stats();
	void write_to_file(std::string host, std::string banner);
	static std::string escape(std::string s);
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

	std::atomic<bool> running;
	int total_lines = 0;
	int total_lines_cont = 0;

	std::chrono::time_point<std::chrono::high_resolution_clock> cont_start;
};

#endif /* CONNECTOR_H */

