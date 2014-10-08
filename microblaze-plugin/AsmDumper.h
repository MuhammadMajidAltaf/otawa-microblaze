#ifndef __ASMDUMPER_H__
#define __ASMDUMPER_H__

#include <otawa/proc/Processor.h>

namespace otawa { namespace microblaze {

class AsmDumper : public Processor
{
public:
	AsmDumper(p::declare& r = reg);

protected:
	virtual void processWorkSpace(WorkSpace* ws);

public:
	static p::declare reg;
};

} // otawa::microblaze
} // otawa

#endif