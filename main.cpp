#include <unistd.h>
#include <iostream>

#include "connector.h"

using namespace std;

int main(int argc, char** argv)
{
	int port = -1;
	size_t maxcon = 10;
	int ttl = 60;
	int poll_timeout = 1000;
	char* filename = nullptr;

	int opt;
	while ((opt = getopt(argc, argv, "p:m:l:t:f:h")) != -1)
       	{
		switch (opt)
	       	{
			case 'p':
				port = atoi(optarg);
				break;
			case 'm':
				maxcon = atoi(optarg);
				break;
			case 'l':
				ttl = atoi(optarg);
				break;
			case 't':
				poll_timeout = atoi(optarg);
				break;
			case 'f':
				filename = optarg;
				break;

			case 'h':
			default:
				cerr << "Usage: " << argv[0] << " [options]\n";
				cerr << "\t-p: Port number\n";
				cerr << "\t-f: Filename to store results in\n";
				cerr << "\t-m: Maximum concurrent connections\n";
				cerr << "\t-l: Time to live (seconds)\n";
				cerr << "\t-t: poll() Timeout (milliseconds)\n";
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

	connector c(cin, filename, port, maxcon, ttl, poll_timeout);

	c.go();	

	return 0;
}

