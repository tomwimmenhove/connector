#ifndef TELNET_H
#define TELNET_H

#include <memory>
#include <string>

#include "negotiator.h"

class telnet_negotiator : public negotiator
{
public:
	telnet_negotiator(int sockfd);

	~telnet_negotiator() override { }

	std::string crunch(unsigned char* buffer, size_t n) override;

private:
	enum class state
	{
		normal,
		cmd1,
		cmd2,
	};

	int sockfd;
	unsigned char cmd;
	state st = state::normal;
};

class telnet_provider: public negotiator_provider
{
public: 
	~telnet_provider() override { }

	std::shared_ptr<negotiator> provide(int sockfd) override;
};

#endif /* TELNET_H */

