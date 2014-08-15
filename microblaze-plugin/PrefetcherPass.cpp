#include "PrefetcherPass.h"

#include <otawa/otawa.h>
#include <otawa/cfg/CFGInfo.h>
#include <otawa/cfg.h>
#include <otawa/cfg/CFGCollector.h>

#include <otawa/util/FlowFactLoader.h>
#include <otawa/ipet/features.h>

#include <otawa/util/Dominance.h>
#include <otawa/dfa/BitSet.h>

using namespace otawa;
using namespace otawa::dfa;

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

	for(CFG::BBIterator bb(cfg); bb; bb++)
	{
		elm::cout << "SEARCHING BB " << bb->number() << elm::io::endl;
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

	// Find elements with a valid ACCESS_STRIDE
	for(CFGCollection::Iterator cfg(cfgs); cfg; cfg++)
	{
		for(CFG::BBIterator bb(cfg); bb; bb++)
		{
			for(BasicBlock::InstIterator inst(bb); inst; inst++)
			{
				if(ACCESS_STRIDE(inst))
				{

				}
			}
		}
	}


	for(CFGCollection::Iterator cfg(cfgs); cfg; cfg++)
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
	}
}

p::declare PrefetcherPass::reg = p::init("otawa::microblaze::PrefetcherPass", Version(1, 0, 0))
                                 .maker<PrefetcherPass>()
                                 .require(otawa::COLLECTED_CFG_FEATURE)
                                 .require(otawa::DOMINANCE_FEATURE)
                                 .require(otawa::ipet::FLOW_FACTS_FEATURE);

} // otawa::microblaze
} // otawa