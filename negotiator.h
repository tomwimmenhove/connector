#ifndef NEGOTIATOR_H
#define NEGOTIATOR_H

#include <memory>
#include <string>

class negotiator
{
	public:
		virtual std::string crunch(int fd, unsigned char* buffer, size_t n) = 0;
};

#endif /* NEGOTIATOR_H */

