#ifndef __BCET_H__
#define __BCET_H__

#include <otawa/hard/Memory.h>
#include <otawa/proc/BBProcessor.h>

namespace otawa { namespace microblaze {

using namespace otawa;

class BCETCalculator : public BBProcessor
{
public:
	static p::declare reg;
	BCETCalculator();

protected:
	virtual void processBB(WorkSpace* ws, CFG* cfg, BasicBlock* bb);
};

extern p::feature BCET_FEATURE;
extern Identifier<int> BCET;

} // otawa::microblaze
} // otawa

#endif