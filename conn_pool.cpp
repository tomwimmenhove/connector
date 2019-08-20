#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <chrono>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "conn_pool.h"

using namespace std;

conn_pool::conn_pool()
	: poller(conn_poller<conn_entry>(this))
{ }

void conn_pool::check_timeouts(std::chrono::time_point<std::chrono::high_resolution_clock> ts, int ttl)
{
	std::list<conn_entry>::iterator it = ces.begin();
	while (it != ces.end())
	{
		/* Time to die? */
		if (ts - it->ts >= chrono::seconds(ttl))
		{
			auto& ce = *it;

			if (it->connected)
				new_banner(ce.ip, ce.str);
			poller.remove(&ce);
			close(it->sockfd);

			ces.erase(it++);
			ces_size--;
			continue;
		}

		++it;
	}
}

int conn_pool::get_fd(conn_entry* ce)
{
	return ce->sockfd;
}

uint32_t conn_pool::get_req_events(conn_entry* ce)
{
	uint32_t events = 0;

	if (ce->connected)
		events |= EPOLLIN;

	if (!ce->connected || (ce->negot && ce->negot->has_write_data()))
		events |= EPOLLOUT;

	return events;
}

bool conn_pool::read_event(conn_entry* ce)
{
	unsigned char buffer[4096];
	ssize_t n = read(ce->sockfd, buffer, sizeof(buffer));

	if (n > 0)
	{
		if (ce->negot)
		{
			ce->str += ce->negot->crunch(buffer, n);
		}
		else
		{
			for(auto i = 0; i < n; i++)
			{
				char ch = (char) buffer[i];
				if (ch) 
					ce->str += ch;
			}
		}
	}
	else if (ce->connected)
	{
		new_banner(ce->ip, ce->str);
		close(ce->sockfd);

		ces.erase(ce->it);
		ces_size--;

		return false;
	}

	return true;
}

bool conn_pool::write_event(conn_entry* ce)
{
	if (!ce->connected)
	{
		socklen_t optlen = sizeof(int);
		int optval = -1;
		ce->ip = getip(ce->sockfd);
		if (getsockopt(ce->sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == -1)
		{
			cerr << "getsockopt() ip=" << ce->ip.c_str() << ", fd=" << ce->sockfd << ": " << strerror(errno) << '\n';

			poller.remove(ce);
			ces.erase(ce->it);
			ces_size--;

			return false;
		}

		if (optval == 0)
		{
			total_connections++;
			ce->connected = true;

			/* Bring in the negotiator? */
			if (prov)
				ce->negot = prov->provide(ce->sockfd);

			return true;
		}
		else
		{
			poller.remove(ce);
			close(ce->sockfd);
			ces.erase(ce->it);
			ces_size--;

			return false;
		}
	}


	/* Do we have shit to write? */
	if (ce->connected && (ce->negot && ce->negot->has_write_data()))
	{
		auto data_vector = ce->negot->pop_write_queue();
		ssize_t n = write(ce->sockfd, data_vector.data(), data_vector.size());
		if (n <= 0)
		{
			new_banner(ce->ip, ce->str);
			poller.remove(ce);
			close(ce->sockfd);

			ces.erase(ce->it);
			ces_size--;

			return false;
		}
	}

	return true;
}

char* conn_pool::getip(int fd)
{
	struct sockaddr_in addr;
	socklen_t addr_size = sizeof(addr);
	getpeername(fd, (struct sockaddr*) &addr, &addr_size);
	return inet_ntoa(addr.sin_addr);
}

void conn_pool::add_fd(int fd)
{
	conn_entry ce;
	ce.sockfd = fd;
	ce.ts =  chrono::high_resolution_clock::now();
	ce.connected = false;

	ces.push_back(ce);

	auto& back = ces.back();

	/* Keep an iterater to ourself */
	back.it = std::prev(ces.end());

	/* Add the connection to the kernel's list of interest */
	poller.add(&back);

	ces_size++;
}

void conn_pool::check_sockets(int timeout)
{
	/* Anything left? */
	if (!ces_size)
		return;

	if (ces_size > events.size())
		events.resize(ces_size);

	if (!poller.poll(ces_size, timeout))
	{
		cerr << "poller: " << strerror(errno) << '\n';
		exit(1);
	}
}

