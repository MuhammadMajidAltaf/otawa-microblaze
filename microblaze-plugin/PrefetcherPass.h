#ifndef __PREFETCHER_PASS_H__
#define __PREFETCHER_PASS_H__

#include <otawa/proc/Processor.h>
#include <elm/genstruct/Vector.h>
#include <otawa/cfg.h>

namespace otawa { namespace microblaze { 

using namespace otawa;

class PrefetcherPass : public Processor
{
public:
	PrefetcherPass(p::declare& r = reg);

protected:
	virtual void processWorkSpace(WorkSpace* ws);
	genstruct::Vector<BasicBlock*> getLoopBBs(BasicBlock* loadBB);

public:
	static p::declare reg;
};

} // otawa::microblaze
} // otawa

#endif