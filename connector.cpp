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
{
	pool.set_new_banner(bind(&connector::write_to_file, this, placeholders::_1, placeholders::_2));
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
	     << pool.get_total_connections() << " total connections, "
	     << pool.get_queue_size() << " in progress";

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

	while ((input && running) || pool.get_queue_size())
	{
		auto now = chrono::high_resolution_clock::now();

		if (now - last_cont >= chrono::milliseconds(1000))
		{
			/* re-calculate rate every second */
			cont();
			last_cont = now;

			/* Check for connections that are past their time to live */
			pool.check_timeouts(now, ttl);
		}

#if 1
		if (now - last_stat >= chrono::milliseconds(250))
		{
			print_stats();
			last_stat = now;
		}
#endif

		if (running && pool.get_queue_size() < maxcon && getline(input, s))
		{
			int sockfd = newcon(s.c_str(), port);
			if (sockfd == -1)
				continue;

			pool.add_fd(sockfd);

			total_lines++;
			total_lines_cont++;
		}

		for(;pool.get_queue_size() > 0;)
		{
			auto poll_start = chrono::high_resolution_clock::now();

			if (pool.get_queue_size() == maxcon)
			{
				pool.check_sockets(1000);
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
			pool.check_sockets(wait_ms >= 0 ? wait_ms : 0);

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

void connector::write_to_file(string host, string banner)
{
	if (to_terminal)
		output << ("\033[1G\033[K");

	output << host << ": " << escape(banner) << '\n';

	if (to_terminal)
		print_stats();
	else
		output.flush();
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
