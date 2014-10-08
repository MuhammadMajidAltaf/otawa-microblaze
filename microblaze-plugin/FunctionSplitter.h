#ifndef __FUNCTION_SPLITTER_H__
#define __FUNCTION_SPLITTER_H__

#include <otawa/proc/Processor.h>
#include <otawa/cfg.h>
#include <otawa/cfg/BasicBlock.h>
#include <elm/genstruct/Vector.h>
#include <elm/genstruct/VectorQueue.h>
#include <otawa/dfa/BitSet.h>
#include <otawa/properties.h>

namespace otawa { namespace microblaze { 

using namespace otawa;
using namespace otawa::dfa;

class FunctionRegion : public PropList
{
public:
	FunctionRegion();
	void addBB(BasicBlock* bb);
	genstruct::Vector<BasicBlock*> getContainedBBs();
	int getRegionSize();
	bool checkDominance(BitSet* domSet);

protected:
	genstruct::Vector<BasicBlock*> bbs;
};


class FunctionSplitter : public Processor
{
public:
	FunctionSplitter(p::declare& r = reg);

protected:
	virtual void processWorkSpace(WorkSpace* ws);
	elm::genstruct::VectorQueue<FunctionRegion*> growRegion(FunctionRegion* region);
	bool isReady(BasicBlock* bb);
	bool canGrow(FunctionRegion* r, BasicBlock* bb);
	elm::genstruct::Vector<BasicBlock*> getLoopBBs(BasicBlock* header);

public:
	static p::declare reg;
};

extern p::feature FUNCTION_SPLIT_FEATURE;
extern Identifier<FunctionRegion*> REGION;

} // otawa::microblaze
} // otawa

#endif