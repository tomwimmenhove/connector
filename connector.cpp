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

std::vector<pollfd> connector::make_poll_vector()
{
	vector<pollfd> poll_vector;
	poll_vector.reserve(ces_size);
	for(auto it = ces.begin(); it != ces.end(); ++it)
	{
		pollfd pfd;

		pfd.fd = it->sockfd;
		pfd.events = 0;

		if (it->connected)
			pfd.events |= POLLIN;

		if (!it->connected || (it->negot && it->negot->has_write_data()))
			pfd.events |= POLLOUT;

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
		if (!it->connected && it->pfd->revents & POLLOUT)
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
				it->connected = true;

				/* Bring in the negotiator? */
				if (prov)
					it->negot = prov->provide(it->sockfd);

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

		/* Do we have shit to write? */
		if (it->connected && (it->negot && it->negot->has_write_data()))
		{
			auto data_vector = it->negot->pop_write_queue();
			ssize_t n = write(it->sockfd, data_vector.data(), data_vector.size());
			if (n <= 0)
			{
				write_to_file(*it);
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

			unsigned char buffer[4096];
			ssize_t n = read(it->sockfd, buffer, sizeof(buffer));

			if (n > 0)
			{
				if (it->negot)
				{
					string s = it->negot->crunch(buffer, n);
					it->str += escape(s);
				}
				else
				{
					it->str += escape(string((char*) buffer, n));
				}
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
	total_lines_cont = 0;
	cont_start = chrono::high_resolution_clock::now();
}

void connector::print_stats()
{
	cerr << "\033[1G"
	     << total_lines << " lines read, "
	     << total_connections << " total connections, "
	     << ces_size << " in progress";

	if (insize > 0 && running)
	{
		float perc = 100.0 * input.tellg() / insize;
		cerr << " -- " << std::setprecision(perc < 10 ? 3 : 4) << perc << '%';
	}

	cerr << "\033[K"
	     << flush;
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

	while ((input && running) || ces_size)
	{
		auto now = chrono::high_resolution_clock::now();

		/* re-calculate rate every second */
		if (now - last_cont >= chrono::milliseconds(1000))
		{
			cont();
			last_cont = now;
		}

#if true
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
			ces_size++;

			total_lines++;
			total_lines_cont++;
		}

		/* Setup an array with poll request events */
		auto poll_vector = make_poll_vector();

		for(;;)
		{
			auto poll_start = chrono::high_resolution_clock::now();

			if (ces_size == maxcon)
			{
				check_sockets(poll_vector, poll_start, 1000);
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
			check_sockets(poll_vector, poll_start,
				       	wait_ms >= 0 ? wait_ms : 0);

			/* If the wait time was less than a millisecond, we're done for now */
			if (wait_ms < 1)
				break;		
		}
	}
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
	output << ce.ip << ": " << ce.str << '\n';
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
