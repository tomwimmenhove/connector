#ifndef TELNET_H
#define TELNET_H

#include <memory>
#include <string>

#include "negotiator.h"

class telnet_negotiator : public negotiator
{
public: 
	virtual std::string crunch(int fd, unsigned char* buffer, size_t n) override;

private:
	enum class state
	{
		normal,
		cmd1,
		cmd2,
	};

	unsigned char cmd;
	state st = state::normal;
};

#endif /* TELNET_H */

