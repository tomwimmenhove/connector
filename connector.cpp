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

#include "connector.h"

using namespace std;

connector::connector(istream& in_stream, int skip, string filename, bool append, int port, int maxcon, int ttl, int conn_rate)
	: in_stream(in_stream), skip(skip), append(append), port(port), maxcon(maxcon), ttl(ttl), conn_rate(conn_rate)
{
	results_stream.open(filename, ofstream::out | (append ? ofstream::app : ofstream::trunc));
}

connector::~connector()
{
	results_stream.close();
}

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

std::vector<pollfd> connector::make_poll_vector()
{
	vector<pollfd> poll_vector;
	poll_vector.reserve(ces_size);
	for(auto it = ces.begin(); it != ces.end(); ++it)
	{
		pollfd pfd;

		pfd.fd = it->sockfd;
		pfd.events = it->connected ? POLLIN : POLLOUT;
		pfd.revents = 0;

		poll_vector.push_back(pfd);

		it->pfd = &poll_vector.back();
	}

	return poll_vector;
}

void connector::check_sockets(vector<pollfd>& poll_vector, std::chrono::time_point<std::chrono::high_resolution_clock> ts, int timeout)
{
	/* Wait for all the things */
	int pret = poll(poll_vector.data(), ces_size, timeout);
	if (pret == -1)
	{
		if (errno == EINTR)
			return;

		perror("\npoll()");
		exit(1);
	}

	std::list<conn_entry>::iterator it = ces.begin();
	while (it != ces.end())
	{
		/* Time to die? */
		if (ts - it->ts >= chrono::seconds(ttl))
		{
			if (it->connected)
				write_to_file(*it);
			close(it->sockfd);

			ces.erase(it++);
			ces_size--;
			continue;
		}

		/* Connected? */
		if (it->pfd->revents & POLLOUT)
		{
			it->pfd->events &= ~POLLOUT; // Because we may poll again
			it->pfd->revents &= ~POLLOUT; 

			socklen_t optlen = sizeof(int);
			int optval = -1;
			it->ip = getip(it->sockfd);
			if (getsockopt(it->sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == -1)
			{
				cerr << '\n';
				perror(it->ip.c_str());

				ces.erase(it++);
				ces_size--;
				continue;
			}

			if (optval == 0)
			{
				total_connections++;
				total_connections_cont++;
				it->connected = true;

				++it;
				continue;
			}
			else
			{
				close(it->sockfd);
				ces.erase(it++);
				ces_size--;
				continue;
			}
		}

		/* data? */
		if (it->connected && (it->pfd->revents & POLLIN))
		{
			it->pfd->events &= ~POLLIN; // XXX: Why? Without it poll() keeps triggering on this event.

			char buffer[4096];
			ssize_t n = read(it->sockfd, buffer, sizeof(buffer));
			if (n > 0)
			{
				it->str += escape(string(buffer, n));
			}
			else
			{
				//if (n == -1)
				//	perror("\nread()");

				write_to_file(*it);
				close(it->sockfd);

				ces.erase(it++);
				ces_size--;

				continue;
			}
		}

		++it;
	}
}

void connector::die()
{
	cerr << "\nKilled, waiting for the connections in the queue to close...\n";
	running = false;
}

void connector::cont()
{
	total_connections_cont = 0;
	cont_start = chrono::high_resolution_clock::now();
}

void connector::print_stats()
{
	cout << "\033[1G"
		<< total_lines << " lines read, "
		<< total_connections << " total connections, "
		<< ces_size << " in progress\033[K"
		<< flush;
}

void connector::go()
{
	string s;
	running = true;

	cont_start = chrono::high_resolution_clock::now();
	auto last_stat = cont_start - chrono::milliseconds(250);

	if (skip)
	{
		while (skip-- && getline(in_stream, s))
			;

		if (!in_stream)
			return;
	}

	while ((in_stream && running) || ces_size)
	{
#if true
		auto now = chrono::high_resolution_clock::now();
		if (now - last_stat >= chrono::milliseconds(250))
		{
			print_stats();
			last_stat = now;
		}
#endif

		if (running && ces_size < maxcon && getline(in_stream, s))
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
			ces_size++;

			total_lines++;
		}

		/* Setup an array with poll request events */
		auto poll_vector = make_poll_vector();

		for(;;)
		{
			/* How long have we been running? */
			auto poll_start = chrono::high_resolution_clock::now();
			auto runtime = poll_start - cont_start;

			/* At the requested connection rate, what would the optimal runtime be? */
			chrono::duration<double> strife_time((double) total_connections_cont / conn_rate);

			/* How long do we have to poll? */
			auto wait = strife_time - runtime;
			auto wait_ms = chrono::duration_cast<chrono::milliseconds>(wait).count();

			/* Finally, do the polling */
			check_sockets(poll_vector, poll_start,
				       	wait_ms >= 0 ? wait_ms : 0);

			/* If the wait time was less than a millisecond, we're done for now */
			if (wait_ms < 1)
				break;		
		}
	}
	print_stats();
}

void connector::write_to_file(conn_entry& ce)
{
	results_stream << ce.ip << ": " << ce.str << '\n';
	results_stream.flush();
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
