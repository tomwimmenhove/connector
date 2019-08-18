#include <unistd.h>
#include <iostream>
#include <signal.h>
#include <string.h>
#include <memory>

#include "telnet.h"
#include "connector.h"

using namespace std;

static shared_ptr<connector> c;

static void sigint_handler(int)
{
	c->die();

	/* Uninstall the handler, die next time */
	struct sigaction sa;
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	sigaction(SIGINT, &sa, nullptr);
}

static void sigcont_handler(int)
{
	cerr << "Cont'd\n";
	c->cont();
}

static shared_ptr<negotiator_provider> get_negot(char* s)
{
	if (strcmp(s, "telnet") == 0)
		return make_shared<telnet_provider>();
	else
	{
		cerr << "The only valid negotiator for now is \"telnet\"\n";
		exit(1);
	}
}

int main(int argc, char** argv)
{
	int port = -1;
	size_t maxcon = 10;
	int ttl = 60;
	int conn_rate = 1;
	char* in_filename = nullptr;
	char* out_filename = nullptr;
	bool append = false;
	int skip = 0;
	bool to_terminal = false;
	std::shared_ptr<negotiator_provider> prov = nullptr;

	int opt;
	while ((opt = getopt(argc, argv, "s:p:m:l:r:i:o:n:aht")) != -1)
       	{
		switch (opt)
	       	{
			case 't':
				to_terminal = true;
				break;
			case 's':
				skip = atoi(optarg);
				break;
			case 'a':
				append = true;
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'm':
				maxcon = atoi(optarg);
				break;
			case 'l':
				ttl = atoi(optarg);
				break;
			case 'r':
				conn_rate = atoi(optarg);
				break;
			case 'i':
				in_filename = optarg;
				break;
			case 'o':
				out_filename = optarg;
				break;
			case 'n':
				prov = get_negot(optarg);
				break;

			case 'h':
			default:
				cerr << "Usage: " << argv[0] << " [options]\n";
				cerr << "\t-o: Set output file to write results (banners) to (instead of stdout)\n";
				cerr << "\t-i: Set input file to read IP addresses from (instead of stdin)\n";
				cerr << "\t-s: Skip n lines from standard input\n";
				cerr << "\t-t: Print banners directly to the terminal\n";
				cerr << "\t-p: Port number\n";
				cerr << "\t-a: Append, don't truncate\n";
				cerr << "\t-m: Maximum concurrent connections\n";
				cerr << "\t-l: Time to live (seconds)\n";
				cerr << "\t-r: Max connection rate (sockets/second)\n";
				cerr << "\t-n: Use a negotiator. Use -n help for a list\n";
				return 1;
		}
	}

	if (port == -1)
	{
		cerr << "No port specified\n";
		return 1;
	}

	ostream* out_stream;
	ofstream out_file;
	if (out_filename)
	{
		out_file.open(out_filename, ofstream::out | (append ? ofstream::app : ofstream::trunc));
		if (out_file.fail())
		{
			cerr << "Could not open " << out_filename << ": " << strerror(errno) << '\n';
			return 1;
		}
		out_stream = &out_file;
	}
	else
	{
		out_stream = &cout;
	}

	istream* in_stream;
	ifstream in_file;
	if (in_filename)
	{
		in_file.open(in_filename, ofstream::in);
		if (in_file.fail())
		{
			cerr << "Could not open " << in_filename << ": " << strerror(errno) << '\n';
			exit(1);
		}
		in_stream = &in_file;
	}
	else
	{
		in_stream = &cin;
	}

	/* Instatiate the 'connector' */
	c = make_shared<connector>(*in_stream, *out_stream, port);

	/* Set parameters */
	c->set_skip(skip);
	c->set_maxcon(maxcon);
	c->set_ttl(ttl);
	c->set_conn_rate(conn_rate);
	c->set_prov(prov);
	c->set_to_terminal(to_terminal);

	/* Catch signals */
	struct sigaction sa;
	sa.sa_handler = sigint_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	sigaction(SIGINT, &sa, nullptr);

	sa.sa_handler = sigcont_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	sigaction(SIGCONT, &sa, nullptr);

	/* Finally, run. */
	c->run();

	return 0;
}

