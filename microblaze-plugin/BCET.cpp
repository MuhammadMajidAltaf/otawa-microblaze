#include "BCET.h"
#include <otawa/prog/Inst.h>

#define STORE_LATENCY 1500
#define BR_BCET 2

namespace otawa { namespace microblaze {

BCETCalculator::BCETCalculator(void) : BBProcessor("otawa::microblaze::BCETCalculator")
{
	provide(BCET_FEATURE);
}

void BCETCalculator::processBB(WorkSpace* ws, CFG* cfg, BasicBlock* bb)
{
	int cost = 0;

	// Iterate over the BB
	// We'll just do it the dumb way for now...
	// We're not caring if it's tight, just if it's optimistic.
	for(BasicBlock::InstIterator inst(bb); inst; inst++)
	{
		if(inst->isLoad())
		{	
			// Assume DCache hit
			cost++;
		}
		else if(inst->isStore())
		{
			// Assume best case store latency
			// Test value...
			cost += STORE_LATENCY;
		}
		else if(inst->isControl())
		{
			cost += BR_BCET;
		}
		else
		{
			// Standard inst
			cost++;
		}
	}

	BCET(bb) = cost;
}

p::declare BCETCalculator::reg = p::init("otawa::microblaze::BCETCalculator", Version(1,0,0))
	.base(BBProcessor::reg)
	.maker<BCETCalculator>()
	.provide(BCET_FEATURE);

p::feature BCET_FEATURE("otawa::microblaze::BCET_FEATURE", new Maker<BCETCalculator>());
Identifier<int> BCET("otawa::microblaze::BCET", 99999999);

} // otawa::microblaze
} // otawa