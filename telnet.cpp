#include <unistd.h>

#include "telnet.h"

using namespace std;

/* XXX: BEGIN - Stolen from http://l3net.wordpress.com/2012/12/09/a-simple-telnet-client */
#define DO 0xfd
#define WONT 0xfc
#define WILL 0xfb
#define DONT 0xfe
#define CMD 0xff
#define CMD_ECHO 1
#define CMD_WINDOW_SIZE 31
/* XXX: END - Stolen */

telnet_negotiator::telnet_negotiator(int sockfd)
	: sockfd(sockfd)
{ }

string telnet_negotiator::crunch(unsigned char* buffer, size_t n)
{
	string s;

	for (size_t i = 0; i < n; i++)
	{
		unsigned char ch = buffer[i];

		switch(st)
		{
			case state::normal:
				if (ch == CMD)
				{
					st = state::cmd1;
					break;
				}

				s += ch;

				break;
			case state::cmd1:
				/* CMD received. Next is DO/DONT/etc... */
				cmd = ch;
				st = state::cmd2;
				break;
			case state::cmd2:
				/* XXX: BEGIN - Stolen from http://l3net.wordpress.com/2012/12/09/a-simple-telnet-client */
				if (cmd == DO && ch == CMD_WINDOW_SIZE)
				{
					write_queue.push(vector<unsigned char> 
							{255, 251, 31});

					write_queue.push(vector<unsigned char> 
							{255, 250, 31, 0, 80, 0, 24, 255, 240});
				}
				else
				{
					// XXX: This seem weird at all?
					if (cmd == DO)
						cmd = WONT;
					else if (cmd == WILL)
						cmd = DO;

					write_queue.push(vector<unsigned char> { 0xff, cmd, ch, });
				}
				/* XXX: END - Stolen */

				/* Back to normal */
				st = state::normal;

				break;
		}

	}

	return s;
}

bool telnet_negotiator::has_write_data()
{
	return !write_queue.empty();
}

std::vector<unsigned char> telnet_negotiator::pop_write_queue()
{
	auto top = write_queue.front();
	write_queue.pop();

	return top;
}

std::shared_ptr<negotiator> telnet_provider::provide(int sockfd)
{
	return make_shared<telnet_negotiator>(sockfd);
}
