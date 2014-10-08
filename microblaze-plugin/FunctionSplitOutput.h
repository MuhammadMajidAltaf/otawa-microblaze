#ifndef __FUNCTION_SPLIT_OUTPUT__
#define __FUNCTION_SPLIT_OUTPUT__

#include <otawa/proc/Processor.h>

namespace otawa { namespace microblaze { 

using namespace otawa;

class FunctionSplitOutput : public Processor
{
public:
	FunctionSplitOutput(p::declare& r = reg);

protected:
	virtual void processWorkSpace(WorkSpace* ws);

public:
	static p::declare reg;
};

extern Identifier<elm::string> REGION_COLOUR;

} // otawa::microblaze
} // otawa

#endif