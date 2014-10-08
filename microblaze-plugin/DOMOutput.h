#ifndef __DOM_OUTPUT_H__
#define __DOM_OUTPUT_H__

#include <otawa/proc/Processor.h>
#include <elm/genstruct/Vector.h>
#include <otawa/cfg.h>

namespace otawa { namespace microblaze { 

using namespace otawa;

class DOMOutput : public Processor
{
public:
	DOMOutput(p::declare& r = reg);

protected:
	virtual void processWorkSpace(WorkSpace* ws);

public:
	static p::declare reg;
};

} // otawa::microblaze
} // otawa


#endif

