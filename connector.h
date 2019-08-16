#include <list>
#include <vector>
#include <fstream>
#include <poll.h>

class connector
{
public:

	connector(std::istream& in_stream, std::string filename, int port, int maxcon, int ttl, int poll_timeout);
	~connector();

	int newcon(const char* host, int port);
	void go();

private:
	struct conn_entry
	{
		int sockfd;
		time_t ts;
		bool connected;
		pollfd* pfd;
		std::string ip;
		std::string str;
	};

	void write_to_file(conn_entry& ce);
	static char* getip(int fd);
	static std::string escape(std::string s);

	std::istream& in_stream;
	int port;
	size_t maxcon;
	int ttl;
	int poll_timeout;

	std::ofstream results_stream;
	std::list<conn_entry> ces;

	size_t ces_size = 0;
	int maxfd = -1;
	int total_lines = 0;
	int total_connections = 0;
};
