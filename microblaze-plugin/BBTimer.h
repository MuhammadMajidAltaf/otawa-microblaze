#ifndef __BBTIMER_H__
#define __BBTIMER_H__

#include <otawa/parexegraph/GraphBBTime.h>
#include <otawa/parexegraph/ParExeGraph.h>
#include <otawa/hard/Memory.h>

namespace otawa { namespace microblaze {

using namespace otawa;

class ExeGraph : public ParExeGraph
{
public:
	ExeGraph(WorkSpace* ws, ParExeProc* proc, ParExeSequence *seq, const PropList &props = PropList::EMPTY);
	void build(void);

private:
	// Convienence access for the memory
	const hard::Memory* mem;

	ParExeStage* exe_stage;
	ParExeStage* mem_stage;
	int reg_count;
	otawa::microblaze::Info* info;
}; // ExeGraph

class BBTimer : public GraphBBTime<ExeGraph>
{
public:
	static p::declare reg;
	BBTimer(void);

protected:
	
}; // BBTimer

} // otawa::microblaze
} // otawa

#endif