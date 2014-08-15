#ifndef __LOOP_STRIDE_H__
#define __LOOP_STRIDE_H__

#include <otawa/proc/Processor.h>

namespace otawa { namespace microblaze { 

using namespace otawa;

class LoopStride : public Processor
{
public:
	LoopStride(p::declare& r = reg);

protected:
	virtual void processWorkSpace(WorkSpace* ws);

public:
	static p::declare reg;
};

} // otawa::microblaze
} // otawa

#endif // __LOOP_STRIDE_H__