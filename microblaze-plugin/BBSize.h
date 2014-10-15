#ifndef __BB_SIZES__
#define __BB_SIZES__

#include <otawa/proc/Processor.h>

namespace otawa { namespace microblaze {

class BBSize : public Processor
{
public:
	BBSize(p::declare& r = reg);

protected:
	virtual void processWorkSpace(WorkSpace* ws);

public:
	static p::declare reg;
};

} // otawa::microblaze
} // otawa

#endif
