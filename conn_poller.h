#ifndef CONN_POLLER_H
#define CONN_POLLER_H

#include <vector>
#include <sys/epoll.h>
#include <cstdint>
#include <errno.h>

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
	{
		epollfd = epoll_create1(0);
		if (epollfd == -1)
	       	{
			throw errno;
		}
       	}

	~conn_poller()
	{
		close(epollfd);
	}

	bool remove(T* data)
	{
		return epoll_ctl(epollfd, EPOLL_CTL_DEL, handler->get_fd(data), nullptr) != -1;
	}

	bool add(T* data)
	{
		struct epoll_event ev;

		ev.events = handler->get_req_events(data);
		ev.data.ptr = (void*) data;

		return epoll_ctl(epollfd, EPOLL_CTL_ADD, handler->get_fd(data), &ev) != -1;
	}

	bool poll(int max_events, int timeout)
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
	int epollfd;
	poll_event_handler<T>* handler;
	std::vector<epoll_event> events;
};
#endif /* CONN_POLLER_H */

