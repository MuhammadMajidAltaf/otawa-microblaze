#include "FunctionSplitter.h"

#include <otawa/otawa.h>
#include <otawa/cfg/CFGInfo.h>
#include <otawa/cfg.h>
#include <otawa/cfg/CFGCollector.h>

#include <otawa/util/Dominance.h>
#include <otawa/dfa/BitSet.h>

#include <elm/genstruct/VectorQueue.h>

#include <otawa/display/Graph.h>

using namespace otawa;
using namespace otawa::dfa;

// TODO: Actually, you know, make this a parameter later
#define REGION_SIZE 1024

namespace otawa { namespace microblaze {

FunctionRegion::FunctionRegion()
{

}

void FunctionRegion::addBB(BasicBlock* bb)
{
	bbs.add(bb);
}

genstruct::Vector<BasicBlock*> FunctionRegion::getContainedBBs()
{
	return bbs;
}

int FunctionRegion::getRegionSize()
{
	int size = 0;

	for(genstruct::Vector<BasicBlock*>::Iterator bb(bbs); bb; bb++)
		size += bb->size();

	return size;
}

bool FunctionRegion::checkDominance(BitSet* dom)
{
	return false;
}

// =============================================================================

FunctionSplitter::FunctionSplitter(p::declare& r) : Processor(r)
{
	provide(FUNCTION_SPLIT_FEATURE);
}

bool FunctionSplitter::isReady(BasicBlock* bb)
{
	// Get the dominance info for the BB to check all predecessors have been
	// allocated a region
	//BitSet* dom = REVERSE_DOM(bb);
	//ASSERT(dom);

	// Check all the predecessors
	for(BasicBlock::InIterator in(bb); in; in++)
	{
		if(in->source()->number() == 0)
			continue;

		if(in->source()->number() == bb->number())
			continue;

		if(BACK_EDGE(in))
			continue;

		if(REGION(in->source()) == 0)
			return false;
	}

	return true;

	// Well, there goes the time complexity...
	/*for(CFG::BBIterator bb2(bb->cfg()); bb2; bb2++)
	{
		// Entry pseudo-BB doesn't count...
		if(bb2->number() == 0)
			continue;

		// Don't check against the same BB...
		if(bb2->number() == bb->number())
			continue;

		if(dom->contains(bb2->number()))
		{
			elm::cout << "FUNCSPLIT READY: checking BB " << bb2->number() << elm::io::endl;
			if(REGION(bb2) == (FunctionRegion*)NULL)
			{
				elm::cout << "FUNCSPLIT READY: BB " << bb2->number() << " is not ready" << elm::io::endl;
				return false;
			}
		}
	}*/

	return true;
}

elm::genstruct::Vector<BasicBlock*> FunctionSplitter::getLoopBBs(BasicBlock* header)
{
	elm::genstruct::Vector<BasicBlock*> loopBBs;

	loopBBs.add(header);

	if(LOOP_HEADER(header))
	{
		for(CFG::BBIterator bb(header->cfg()); bb; bb++)
		{
			if(ENCLOSING_LOOP_HEADER(bb) && ENCLOSING_LOOP_HEADER(bb)->number() == header->number())
				loopBBs.add(bb);
		}
	}

	return loopBBs;
}

bool FunctionSplitter::canGrow(FunctionRegion* r, BasicBlock* bb)
{
	int sz = r->getRegionSize();
	int newBBsz = 0;

	// Get the size of the BB, or if it is a loop header, the whole loop
	if(LOOP_HEADER(bb))
	{
		elm::genstruct::Vector<BasicBlock*> loopBBs = getLoopBBs(bb);
		for(elm::genstruct::Vector<BasicBlock*>::Iterator loopBB(loopBBs); loopBB; loopBB++)
			newBBsz += loopBB->size();
	}
	else
	{
		for(BasicBlock::InIterator in(bb); in; in++)
		{
			if(REGION(in->source()) != r)
			{
				elm::cout << "FUNCSPLIT CANGROW: BB" << bb->number() << " does not have all parents in the same region." << elm::io::endl;
				elm::cout << "FUNCSPLIT CANGROW: BB" << in->source()->number() << " is in region " << REGION(in->source()) << elm::io::endl;
				return false;
			}
		}

		elm::cout << "FUNCSPLIT CANGROW: BB" << bb->number() << " is trivial. Considering" << elm::io::endl;
		newBBsz = bb->size();
	}

	elm::cout << "FUNCSPLIT CANGROW: New size " << sz+newBBsz << ". Max " << REGION_SIZE << elm::io::endl;

	if(sz + newBBsz < REGION_SIZE)
		return true;
	else
		return false;
}

elm::genstruct::VectorQueue<FunctionRegion*> FunctionSplitter::growRegion(FunctionRegion* fr)
{
	elm::genstruct::VectorQueue<FunctionRegion*> newRegions;

	// Because this is greddy, the passed function set *should* only contain
	// a single BB for processing.
	// If not, shit's broken, yo
	ASSERT(fr->getContainedBBs().count() == 1);
	BasicBlock* bb = fr->getContainedBBs()[0];
	ASSERT(bb);

	elm::cout << "Growing region from BB "<< bb->number() << elm::io::endl;

	elm::genstruct::VectorQueue<BasicBlock*> bbList;
	
	for(BasicBlock::OutIterator cBB(bb); cBB; cBB++)
	{
		if(cBB->kind() == Edge::CALL || cBB->kind() == Edge::VIRTUAL_CALL)
			continue;
		if(BACK_EDGE(cBB))
			continue;
		if(!isReady(cBB->target()))
			continue;

		elm::cout << "From BB" << bb->number() << " trying BB" << cBB->target()->number() << elm::io::endl;

		bbList.put(cBB->target());
	}

	while(!bbList.isEmpty())
	{
		BasicBlock* nextBB = bbList.get();

		if(REGION(nextBB) != 0)
			continue;

		if(canGrow(fr, nextBB))
		{
			elm::cout << "FUNCSPLIT: Can grow with BB " << nextBB->number() << elm::io::endl;
			
			// If it's a loop header, add all BBs into the region, otherwise
			// just add the node into the region
			elm::genstruct::Vector<BasicBlock*> newBBs = getLoopBBs(nextBB);
			for(elm::genstruct::Vector<BasicBlock*>::Iterator i(newBBs); i; i++)
			{
				elm::cout << "Adding BB" << i->number() << " to region" << elm::io::endl;
				fr->addBB(i);
				REGION(i) = fr;
			}

			// Now get all children of all those nodes and add them to the
			// work list, only if they aren't already in the returned list
			for(elm::genstruct::Vector<BasicBlock*>::Iterator i(newBBs); i; i++)
			{
				// Get all children
				for(BasicBlock::OutIterator cBB(i); cBB; cBB++)
				{
					if(BACK_EDGE(cBB))
					{
						elm::cout << "GOT BACK EDGE, ABORTING " << cBB->source()->number() << " - " << cBB->target()->number() << elm::io::endl;
						continue;
					}
					// If it's an external call
					if(cBB->kind() == Edge::CALL || cBB->kind() == Edge::VIRTUAL_CALL)
						continue;
					// If it's not ready...
					if(!isReady(cBB->target()))
						continue;
					// If it's part of the loop
					if(newBBs.contains(cBB->target()))
						continue;
					// If it's already being considered (an optimisation, maybe remove)
					if(bbList.contains(cBB->target()))
						continue;

					elm::cout << "FUNCSPLIT: Adding BB" << cBB->target()->number() << " to work list" << elm::io::endl;

					bbList.put(cBB->target());
				}
			}
		}
		else
		{
			if(isReady(nextBB))
			{
				// Create a new region, add, and return the new region for processing
				FunctionRegion* newFr = new FunctionRegion();
				newFr->addBB(nextBB);
				REGION(nextBB) = newFr;

				newRegions.put(newFr);

				elm::cout << "FUNCSPLIT: BB" << nextBB->number() << " is too large..." << (LOOP_HEADER(nextBB) ? " is loop header " : "") << elm::io::endl;
			}
		}

		elm::cout << "FUNCSPLIT: Considering candidate BB " << nextBB->number() << elm::io::endl;
	}

	return newRegions;
}

void FunctionSplitter::processWorkSpace(WorkSpace* ws)
{
	const CFGCollection* cfgs = INVOLVED_CFGS(ws);
	ASSERT(cfgs);

	for(CFGCollection::Iterator cfg(cfgs); cfg; cfg++)
	{
		elm::cout << "FUNCSPLIT: Working on CFG " << cfg->label() << elm::io::endl;

		BasicBlock* rootBB = cfg->firstBB();
		ASSERT(rootBB);

		// Create the first region
		FunctionRegion* rootRegion = new FunctionRegion();
		rootRegion->addBB(rootBB);
		REGION(rootBB) = rootRegion;

		elm::genstruct::VectorQueue<FunctionRegion*> workQueue;
		workQueue.put(rootRegion);

		// While we have something to work on.
		// Size is the number of elements in the queue in this case, not
		// the size of the overall container.
		while(!workQueue.isEmpty())
		{
			FunctionRegion* fr = workQueue.get();
			elm::genstruct::VectorQueue<FunctionRegion*> newRegions = growRegion(fr);

			while(!newRegions.isEmpty())
			{
				workQueue.put(newRegions.get());
			}
		}
	}
}

p::declare FunctionSplitter::reg = p::init("otawa::microblaze::FunctionSplitter", Version(1,0,0))
								   .maker<FunctionSplitter>()
								   .require(otawa::COLLECTED_CFG_FEATURE)
								   .require(otawa::LOOP_INFO_FEATURE)
								   .require(otawa::DOMINANCE_FEATURE)
								   .provide(otawa::microblaze::FUNCTION_SPLIT_FEATURE);

p::feature FUNCTION_SPLIT_FEATURE("otawa::microblaze::FUNCTION_SPLIT_FEATURE", new Maker<FunctionSplitter>());
Identifier<FunctionRegion*> REGION("otawa::microblaze::REGION", (FunctionRegion*)NULL);

} // otawa::microblaze
} // otawa