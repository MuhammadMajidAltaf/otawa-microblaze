#include "PrefetcherPass.h"

#include <otawa/otawa.h>
#include <otawa/cfg/CFGInfo.h>
#include <otawa/cfg.h>
#include <otawa/cfg/CFGCollector.h>

#include <otawa/util/FlowFactLoader.h>
#include <otawa/ipet/features.h>

#include <otawa/util/Dominance.h>
#include <otawa/dfa/BitSet.h>

#include <otawa/hard/Cache.h>
#include <otawa/hard/Platform.h>
#include <otawa/hard/CacheConfiguration.h>

#include "BCET.h"

using namespace otawa;
using namespace otawa::dfa;

#define PF_WCET 2000

namespace otawa { namespace microblaze { 

PrefetcherPass::PrefetcherPass(p::declare& r) : Processor(r)
{

}

genstruct::Vector<BasicBlock*> PrefetcherPass::getLoopBBs(BasicBlock* loadBB)
{
	genstruct::Vector<BasicBlock*> loopBB;

	// Get the CFG that the BB is in
	CFG* cfg = loadBB->cfg();
	BasicBlock* loopHeader = ENCLOSING_LOOP_HEADER(*loadBB); // And the BB of the loop the BB is in
	ASSERT(cfg);
	ASSERT(loopHeader);

	elm::cout << "Searching from BB " << loadBB->number() << elm::io::endl;

	// Then iterate over all BBs, and check whether they belong within this loop.
	for(CFG::BBIterator bb(cfg); bb; bb++)
	{
		if(bb->number() == loopHeader->number())
			loopBB.add(bb);
		if(ENCLOSING_LOOP_HEADER(*bb) && ENCLOSING_LOOP_HEADER(*bb)->number() == loopHeader->number())
			loopBB.add(bb);
	}

	return loopBB;
}

void PrefetcherPass::processWorkSpace(WorkSpace* ws)
{
	const CFGCollection* cfgs = INVOLVED_CFGS(ws);

	ASSERT(cfgs);
	CFG* cfg = cfgs->get(0);

	int bestExec = 0;

	if(!hard::CACHE_CONFIGURATION(ws)->hasDataCache())
		throw otawa::Exception("No data cache in the system");

	const hard::Cache* dCache = hard::CACHE_CONFIGURATION(ws)->dataCache();
	assert(dCache != 0);
	int dCacheLineSize = dCache->blockSize();

	// Find elements with a valid ACCESS_STRIDE
	for(CFGCollection::Iterator cfg(cfgs); cfg; cfg++)
	{
		for(CFG::BBIterator bb(cfg); bb; bb++)
		{
			for(BasicBlock::InstIterator inst(bb); inst; inst++)
			{
				if(ACCESS_STRIDE(inst))
				{
					elm::cout << "Got stride of " << ACCESS_STRIDE(inst) << " on instruction " << *inst << " (0x" << inst->address() << ")" << elm::io::endl;

					// Get all of the BBs of the loop
					genstruct::Vector<BasicBlock*> loopBBs = getLoopBBs(bb);
					elm::cout << "\tLOOP MEMBERS:\n";
					for(genstruct::Vector<BasicBlock*>::Iterator i(loopBBs); i; i++)
					{
						elm::cout << "\t\t" << (*i)->number() << " ET: " << BCET(**i) << elm::io::endl;
						bestExec += BCET(**i);
					}

					// How many accesses before the prefetch is due?
					int nAccess = dCacheLineSize / ACCESS_STRIDE(inst);
					bestExec *= nAccess;
					elm::cout << "Got BCET " << bestExec << " (interval " << nAccess << ")" << elm::io::endl;

					if(bestExec > PF_WCET)
						elm::cout << "--- Improvement! (" << dCache->missPenalty() << " cycles) ---" << elm::io::endl;
				}
			}
		}
	}

	


	/*for(CFGCollection::Iterator cfg(cfgs); cfg; cfg++)
	{
		elm::cout << "Using CFG " << cfg->label() << elm::io::endl;

		for(CFG::BBIterator bb(cfg); bb; bb++)
		{
			BitSet* dom = REVERSE_DOM(bb);
			elm::cout << "\tDOM: " << *dom << elm::io::endl;

			int max_iter = MAX_ITERATION(bb);
			elm::cout << "\tBB " << bb->number();
			if(LOOP_HEADER(bb))
				elm::cout << " HEADER MAX ITER " << max_iter << " ";
			if(ENCLOSING_LOOP_HEADER(bb))
				elm::cout << " ENCLOSED MAX ITER " << MAX_ITERATION(ENCLOSING_LOOP_HEADER(bb));
			if(!LOOP_HEADER(bb) && !ENCLOSING_LOOP_HEADER(bb))
				elm::cout << " NO LOOP!";

			elm::cout << "\n";

			for(BasicBlock::InstIterator inst(bb); inst; inst++)
			{
				int stride = ACCESS_STRIDE(inst);
				elm::cout << "\t\tInst Stride: " << stride << elm::io::endl;

				if(stride != 0)
				{
					genstruct::Vector<BasicBlock*> bbz = getLoopBBs(bb);
					elm::cout << "\tLOOP MEMBERS:\n";
					for(genstruct::Vector<BasicBlock*>::Iterator i(bbz); i; i++)
						elm::cout << "\t\t" << (*i)->number() << elm::io::endl;
				}
			}
		}
	}*/
}

p::declare PrefetcherPass::reg = p::init("otawa::microblaze::PrefetcherPass", Version(1, 0, 0))
                                 .maker<PrefetcherPass>()
                                 .require(otawa::COLLECTED_CFG_FEATURE)
                                 .require(otawa::DOMINANCE_FEATURE)
                                 .require(otawa::LOOP_INFO_FEATURE)
                                 .require(otawa::microblaze::BCET_FEATURE)
                                 .require(otawa::hard::CACHE_CONFIGURATION_FEATURE)
                                 .require(otawa::ipet::FLOW_FACTS_FEATURE);

} // otawa::microblaze
} // otawa