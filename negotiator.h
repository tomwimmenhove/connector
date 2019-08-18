#ifndef NEGOTIATOR_H
#define NEGOTIATOR_H

#include <memory>
#include <string>

class negotiator
{
public:
	virtual ~negotiator() { }
	virtual std::string crunch(unsigned char* buffer, size_t n) = 0;
};

class negotiator_provider
{
public:
	virtual ~negotiator_provider() { }
	virtual std::shared_ptr<negotiator> provide(int sockfd) = 0;
};

#endif /* NEGOTIATOR_H */

