#include <unistd.h>
#include <iostream>
#include <signal.h>
#include <string.h>

#include "telnet.h"
#include "connector.h"

using namespace std;

static connector* c;

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

static negotiator_provider* get_negot(char* s)
{
	if (strcmp(s, "telnet") == 0)
		return new telnet_provider();
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
	char* filename = nullptr;
	bool append = false;
	int skip = 0;
	negotiator_provider* prov = nullptr;

	int opt;
	while ((opt = getopt(argc, argv, "s:p:m:l:r:f:n:ah")) != -1)
       	{
		switch (opt)
	       	{
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
			case 'f':
				filename = optarg;
				break;
			case 'n':
				prov = get_negot(optarg);
				break;

			case 'h':
			default:
				cerr << "Usage: " << argv[0] << " [options]\n";
				cerr << "\t-s: Skip n lines from standard input\n";
				cerr << "\t-p: Port number\n";
				cerr << "\t-f: Filename to store results in\n";
				cerr << "\t-a: Append, don't truncate\n";
				cerr << "\t-m: Maximum concurrent connections\n";
				cerr << "\t-l: Time to live (seconds)\n";
				cerr << "\t-r: Max connection rate (sockets/second)\n";
				cerr << "\t-n: Use a negotiator. Use -n help for a list\n";
				exit(1);
		}
	}

	if (port == -1)
	{
		cerr << "No port specified\n";
		exit(1);
	}

	if (filename == nullptr)
	{
		cerr << "No filename specified\n";
		exit(1);
	}

	struct sigaction sa;
	sa.sa_handler = sigint_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	sigaction(SIGINT, &sa, nullptr);

	sa.sa_handler = sigcont_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	sigaction(SIGCONT, &sa, nullptr);

	c = new connector(cin, skip, filename, append, port, maxcon, ttl, conn_rate, prov);

	c->go();

	delete c;
	if (prov)
		delete prov;

	return 0;
}

