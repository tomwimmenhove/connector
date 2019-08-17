#include <unistd.h>
#include <iostream>
#include <signal.h>

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

int main(int argc, char** argv)
{
	int port = -1;
	size_t maxcon = 10;
	int ttl = 60;
	int conn_rate = 1;
	char* filename = nullptr;
	bool append = false;

	int opt;
	while ((opt = getopt(argc, argv, "p:m:l:r:f:ah")) != -1)
       	{
		switch (opt)
	       	{
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

			case 'h':
			default:
				cerr << "Usage: " << argv[0] << " [options]\n";
				cerr << "\t-p: Port number\n";
				cerr << "\t-f: Filename to store results in\n";
				cerr << "\t-a: Append, don't truncate\n";
				cerr << "\t-m: Maximum concurrent connections\n";
				cerr << "\t-l: Time to live (seconds)\n";
				cerr << "\t-r: Max connection rate (sockets/second)\n";
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

	c = new connector(cin, filename, append, port, maxcon, ttl, conn_rate);

	c->go();

	delete c;

	return 0;
}

