#include <list>
#include <vector>
#include <fstream>
#include <atomic>
#include <poll.h>

class connector
{
public:

	connector(std::istream& in_stream, std::string filename, bool append, int port, int maxcon, int ttl, int conn_rate);
	~connector();

	int newcon(const char* host, int port);
	void go();
	void die();

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
	std::vector<pollfd> make_poll_vector();
	void check_sockets(std::vector<pollfd>& poll_vector, time_t ts, int timeout);

	std::istream& in_stream;
	bool append;
	int port;
	size_t maxcon;
	int ttl;
	int conn_rate;

	std::ofstream results_stream;
	std::list<conn_entry> ces;

	std::atomic<bool> running;
	size_t ces_size = 0;
	int maxfd = -1;
	int total_lines = 0;
	int total_connections = 0;
};
