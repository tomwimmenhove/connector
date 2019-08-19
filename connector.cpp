#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/select.h>
#include <fstream>
#include <string.h>
#include <fcntl.h>
#include <iomanip>

#include "connector.h"
#include "telnet.h"

using namespace std;

connector::connector(istream& input, ostream& output, int port)
	: input(input), output(output), port(port)
{ }

int connector::newcon(const char* host, int port)
{
	int sockfd;
	struct sockaddr_in addr;

	sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (sockfd == -1)
	{
		perror("\nsocket()");
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, host, &addr.sin_addr) == -1)
	{
		perror("\ninet_pton()");
		return -1;
	}

	if (connect(sockfd, (struct sockaddr*) &addr, sizeof(struct sockaddr)) == -1 &&
			errno != EINPROGRESS)
	{
		perror("\nconnect()");
		return -1;
	}

	return sockfd;
}

void connector::check_timeouts(std::chrono::time_point<std::chrono::high_resolution_clock> ts)
{
	std::list<conn_entry>::iterator it = ces.begin();
	while (it != ces.end())
	{
		/* Time to die? */
		if (ts - it->ts >= chrono::seconds(ttl))
		{
			if (it->connected)
				write_to_file(*it);
			epoll_conn(*it, EPOLL_CTL_DEL);
			close(it->sockfd);

			ces.erase(it++);
			ces_size--;
			continue;
		}

		++it;
	}
}

void connector::check_sockets(int timeout)
{
	/* Anything left? */
	if (!ces_size)
		return;

	if (maxcon > events.size())
		events.resize(maxcon);

	int n_poll = epoll_wait(epollfd, events.data(), ces_size, timeout);
	if (n_poll == -1)
	{
		auto e = errno;
		if (e == EINTR)
			return;

		cerr << "epoll_wait(): " << strerror(e) << '\n';
		exit(1);
	}

	for (int i = 0; i < n_poll; i++)
	{
		epoll_event& event = events[i];

		conn_entry* ce = (conn_entry*) event.data.ptr;

		/* Connected? */
		if (!ce->connected && event.events & EPOLLOUT)
		{
			socklen_t optlen = sizeof(int);
			int optval = -1;
			ce->ip = getip(ce->sockfd);
			if (getsockopt(ce->sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == -1)
			{
				cerr << "getsockopt() ip=" << ce->ip.c_str() << ", fd=" << ce->sockfd << ": " << strerror(errno) << '\n';

				epoll_conn(*ce, EPOLL_CTL_DEL);
				ces.erase(ce->it);
				ces_size--;
				continue;
			}

			if (optval == 0)
			{
				total_connections++;
				ce->connected = true;

				/* Bring in the negotiator? */
				if (prov)
					ce->negot = prov->provide(ce->sockfd);

				epoll_conn(*ce, EPOLL_CTL_MOD);
				continue;
			}
			else
			{
				epoll_conn(*ce, EPOLL_CTL_DEL);
				close(ce->sockfd);
				ces.erase(ce->it);
				ces_size--;
				continue;
			}
		}

		/* Do we have shit to write? */
		if (ce->connected && event.events & EPOLLOUT && (ce->negot && ce->negot->has_write_data()))
		{
			auto data_vector = ce->negot->pop_write_queue();
			ssize_t n = write(ce->sockfd, data_vector.data(), data_vector.size());
			if (n <= 0)
			{
				write_to_file(*ce);
				epoll_conn(*ce, EPOLL_CTL_DEL);
				close(ce->sockfd);

				ces.erase(ce->it);
				ces_size--;

				continue;
			}
		}

		/* data? */
		if (ce->connected && (event.events & EPOLLIN))
		{
			unsigned char buffer[4096];
			ssize_t n = read(ce->sockfd, buffer, sizeof(buffer));

			if (n > 0)
			{
				if (ce->negot)
				{
					string s = ce->negot->crunch(buffer, n);
					ce->str += escape(s);
				}
				else
				{
					ce->str += escape(string((char*) buffer, n));
				}
			}
			else
			{
				//if (n == -1)
				//	perror("\nread()");

				write_to_file(*ce);
				epoll_conn(*ce, EPOLL_CTL_DEL);
				close(ce->sockfd);

				ces.erase(ce->it);
				ces_size--;

				continue;
			}
		}

		epoll_conn(*ce, EPOLL_CTL_MOD);
	}
}

void connector::die()
{
	cerr << "\nKilled, waiting for the connections in the queue to close...\n";
	running = false;
}

void connector::cont()
{
	total_lines_cont = 0;
	cont_start = chrono::high_resolution_clock::now();
}

void connector::print_stats()
{
	cerr << "\033[1G"
	     << total_lines << " lines read, "
	     << total_connections << " total connections, "
	     << ces_size << " in progress";

	if (insize > 0 && running && input)
	{
		float perc = 100.0 * input.tellg() / insize;
		cerr << " -- " << std::setprecision(perc < 10 ? 3 : 4) << perc << '%';
	}
	else if (!running)
		cerr << " -- closing...";

	cerr << "\033[K"
	     << flush;
}

void connector::epoll_conn(conn_entry& ce, int op)
{
	struct epoll_event ev;

	uint32_t events = 0;

	if (op != EPOLL_CTL_DEL)
	{
		if (ce.connected)
			events |= EPOLLIN;

		if (!ce.connected || (ce.negot && ce.negot->has_write_data()))
			events |= EPOLLOUT;
	}

	ev.events = events;
	ev.data.ptr = (void*) &ce;

	if (epoll_ctl(epollfd, op, ce.sockfd, &ev) == -1)
	{
		perror("\nepoll_ctl");
		exit(1);
	}
}

void connector::run()
{
	running = true;

	cont_start = chrono::high_resolution_clock::now();
	auto last_stat = cont_start - chrono::milliseconds(250);
	auto last_cont = cont_start;

	input.seekg (0, ios::end);
	if (!input.fail())
	{
		insize = input.tellg();
		input.seekg (0, ios::beg);
	}
	else
	{
		insize = -1;
		input.clear();
	}

	string s;
	if (skip)
	{
		cerr << "Skipping...";
		auto i = skip;
		while (i-- && getline(input, s))
			;
		cerr << '\n';

		if (!input)
			return;
	}

	epollfd = epoll_create1(0);
	if (epollfd == -1) {
		perror("epoll_create1");
		exit(1);
	}

	while ((input && running) || ces_size)
	{
		auto now = chrono::high_resolution_clock::now();

		if (now - last_cont >= chrono::milliseconds(1000))
		{
			/* re-calculate rate every second */
			cont();
			last_cont = now;

			/* Check for connections that are past their time to live */
			check_timeouts(now);
		}

#if 1
		if (now - last_stat >= chrono::milliseconds(250))
		{
			print_stats();
			last_stat = now;
		}
#endif

		if (running && ces_size < maxcon && getline(input, s))
		{
			auto ts = chrono::high_resolution_clock::now();
			int sockfd = newcon(s.c_str(), port);
			if (sockfd == -1)
				continue;

			if (sockfd > maxfd)
				maxfd = sockfd;

			conn_entry ce;
			ce.sockfd = sockfd;
			ce.ts = ts;
			ce.connected = false;

			ces.push_back(ce);

			auto& back = ces.back();

			/* Keep an iterater to ourself */
			back.it = std::prev(ces.end());

			/* Add the connection to the kernel's list of interest */
			epoll_conn(back, EPOLL_CTL_ADD);

			ces_size++;
			total_lines++;
			total_lines_cont++;
		}

		for(;ces_size > 0;)
		{
			auto poll_start = chrono::high_resolution_clock::now();

			if (ces_size == maxcon)
			{
				check_sockets(1000);
				break;
			}

			/* How long have we been running? */
			auto runtime = poll_start - cont_start;

			/* At the requested connection rate, what would the optimal runtime be? */
			chrono::duration<double> strife_time((double) total_lines_cont / conn_rate);

			/* How long do we have to poll? */
			auto wait = strife_time - runtime;
			auto wait_ms = chrono::duration_cast<chrono::milliseconds>(wait).count();

			/* Finally, do the polling */
			check_sockets(wait_ms >= 0 ? wait_ms : 0);

			/* If the wait time was less than a millisecond, we're done for now */
			if (wait_ms < 1)
				break;		
		}
	}

	close(epollfd);

	print_stats();

	cerr << '\n';

	if (!running)
	{
		cerr << "To continue the scan where we left off, "
			"add these command-line options: -a -s "
		       	<< (skip + total_lines) << '\n';
	}
}

void connector::write_to_file(conn_entry& ce)
{
	if (to_terminal)
		output << "\033[1G\033[K";

	output << ce.ip << ": " << ce.str << '\n';

	if (to_terminal)
		print_stats();
	else
		output.flush();
}

char* connector::getip(int fd)
{
	struct sockaddr_in addr;
	socklen_t addr_size = sizeof(addr);
	getpeername(fd, (struct sockaddr*) &addr, &addr_size);
	return inet_ntoa(addr.sin_addr);
}

string connector::escape(string s)
{
	string out;

	for (char& ch: s)
	{
		if (isprint(ch))
		{
			out += ch;
		}
		else
		{
			switch (ch)
			{
				case '\\': out+= "\\\\"; break;
				case '\a': out+= "\\a"; break;
				case '\b': out+= "\\b"; break;
				case '\f': out+= "\\f"; break;
				case '\n': out+= "\\n"; break;
				case '\r': out+= "\\r"; break;
				case '\v': out+= "\\v"; break;

				/* Chars to print as-is */
				case '\t':
					   out+= ch;
					   break;
				default:
					   out += "\\x";
					   const char* hex = "0123456789abcdef";
					   out += hex[(unsigned char) ch >> 4];
					   out += hex[ch & 0x0f];
					   break;
			}
		}
	}

	return out;
}
