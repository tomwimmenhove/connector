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

	connector(int skip, int port, int maxcon, int ttl, int conn_rate, negotiator_provider* prov = nullptr);

	int newcon(const char* host, int port);
	void go();
	void die();
	void cont();

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

	void print_stats();
	void write_to_file(conn_entry& ce);
	static char* getip(int fd);
	static std::string escape(std::string s);
	std::vector<pollfd> make_poll_vector();
	void check_sockets(std::vector<pollfd>& poll_vector, std::chrono::time_point<std::chrono::high_resolution_clock> ts, int timeout);

	int skip;
	int port;
	size_t maxcon;
	int ttl;
	int conn_rate;
	negotiator_provider* prov;

	std::list<conn_entry> ces;

	std::atomic<bool> running;
	size_t ces_size = 0;
	int maxfd = -1;
	int total_lines = 0;
	int total_lines_cont = 0;
	int total_connections = 0;

	std::istream* input = &std::cin;
	std::ostream* output = &std::cout;

	std::chrono::time_point<std::chrono::high_resolution_clock> cont_start;
};

#endif /* CONNECTOR_H */

