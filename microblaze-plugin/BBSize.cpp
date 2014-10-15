#include "BBSize.h"

#include <otawa/otawa.h>
#include <otawa/cfg.h>
#include <otawa/cfg/CFGCollector.h>

#include <elm/genstruct/Vector.h>
#include <elm/genstruct/VectorQueue.h>


namespace otawa { namespace microblaze {

BBSize::BBSize(p::declare& r) : Processor(r)
{

}

void BBSize::processWorkSpace(WorkSpace* ws)
{
	const CFGCollection* cfgs = INVOLVED_CFGS(ws);
	ASSERT(cfgs);

	int totalSize = 0;
	int totalNum = 0;

	int totalMax = 0;

	for(CFGCollection::Iterator cfg(cfgs); cfg; cfg++)
	{
		int cfgSize = 0;
		int cfgNum = 0;

		int cfgMax = 0;

		for(CFG::BBIterator bb(cfg); bb; bb++)
		{
			if(bb->size() > 0)
			{
				cfgSize += bb->size();
				cfgNum++;
				totalSize += bb->size();
				totalNum++;

				if(bb->size() > cfgMax)
					cfgMax = bb->size();
				if(bb->size() > totalMax)
					totalMax = bb->size();
			}
		}

		elm::cout << "CFG " << cfg->label() << " avg " << ((float)cfgSize/(float)cfgNum) << " max " << cfgMax << elm::io::endl;
	}

	elm::cout << "PROG avg " << ((float)totalSize/(float)totalNum) << " max " << totalMax << elm::io::endl;
}

p::declare BBSize::reg = p::init("otawa::microblaze::BBSize", Version(1,0,0))
							.maker<BBSize>()
							.require(otawa::COLLECTED_CFG_FEATURE);

} // otawa::microblaze
} // otawa
