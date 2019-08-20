#ifndef CONN_POOL_H
#define CONN_POOL_H

#include <list>
#include <vector>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "negotiator.h"
#include "conn_poller.h"

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

class conn_pool : private poll_event_handler<conn_entry>
{
public:
	conn_pool();

	int get_total_connections() { return total_connections; }
	size_t get_queue_size() { return ces_size; }

	void set_new_banner(std::function<void(std::string host, std::string banner)> new_banner) { this->new_banner = new_banner; }
	std::function<void(std::string host, std::string banner)> get_new_banner() { return new_banner; }

	void set_prov(std::shared_ptr<negotiator_provider> prov) { this->prov = prov; }
	std::shared_ptr<negotiator_provider> get_prov() { return prov; }

	void check_sockets(int timeout);

	void add_fd(int fd);

	void check_timeouts(std::chrono::time_point<std::chrono::high_resolution_clock> ts, int ttl);

private:
	int get_fd(conn_entry* ce) override;
	uint32_t get_req_events(conn_entry* ce) override;
	bool read_event(conn_entry* ce) override;
	bool write_event(conn_entry* ce) override;

	char* getip(int fd);

	std::function<void(std::string host, std::string banner)> new_banner;
	int total_connections = 0;
	std::shared_ptr<negotiator_provider> prov = nullptr;
	std::vector<epoll_event> events;

	conn_poller<conn_entry> poller;

	std::list<conn_entry> ces;
	size_t ces_size = 0;
	int maxfd = -1;
};


#endif /* CONN_POOL_H */
