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

template <class T>
class poll_event_handler
{
public: 
	virtual ~poll_event_handler() { }

	virtual int get_fd(T* data) = 0;
	virtual uint32_t get_req_events(T* data) = 0;
	virtual bool read_event(T* data) = 0;
	virtual bool write_event(T* data) = 0;
};

template <class T>
class conn_poller
{
public:
	conn_poller(poll_event_handler<T>* handler)
		: handler(handler)
	{ }

	bool remove(int epollfd, T* data)
	{
		return epoll_ctl(epollfd, EPOLL_CTL_DEL, handler->get_fd(data), nullptr) != -1;
	}

	bool add(int epollfd, T* data)
	{
		struct epoll_event ev;

		ev.events = handler->get_req_events(data);
		ev.data.ptr = (void*) data;

		return epoll_ctl(epollfd, EPOLL_CTL_ADD, handler->get_fd(data), &ev) != -1;
	}

	bool poll(int epollfd, int max_events, int timeout)
	{
		if (max_events > (int) events.size())
			events.resize(max_events);

		int n_poll = epoll_wait(epollfd, events.data(), max_events, timeout);
		if (n_poll == -1)
		{
			if (errno == EINTR)
				return true;

			return false;
		}

		for (int i = 0; i < n_poll; i++)
		{
			epoll_event& event = events[i];
			T* data = (T*) event.data.ptr;

			bool success = true;
			if (event.events & EPOLLIN)
				success &= handler->read_event(data);

			if (success && event.events & EPOLLOUT)
				success &= handler->write_event(data);

			if (success)
			{
				event.events = handler->get_req_events(data);
				if (epoll_ctl(epollfd, EPOLL_CTL_MOD, handler->get_fd(data), &event) == -1)
					return false;
			}
		}

		return true;
	}

private:
	poll_event_handler<T>* handler;
	std::vector<epoll_event> events;
};

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

class connector : private poll_event_handler<conn_entry>
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
	/* Implementation of poll_event_handler interface */
	int get_fd(conn_entry* ce) override;
	uint32_t get_req_events(conn_entry* ce) override;
	bool read_event(conn_entry* ce) override;
	bool write_event(conn_entry* ce) override;

	conn_poller<conn_entry> poller;

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

