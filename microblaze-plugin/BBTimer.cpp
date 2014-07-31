#include <otawa/parexegraph/GraphBBTime.h>
#include <otawa/parexegraph/ParExeGraph.h>
#include <otawa/cfg/features.h>
#include "../otawa-microblaze/microblaze.h"

#include "BBTimer.h"

namespace otawa { namespace microblaze { 

using namespace otawa;

// ============================================
// ExeGraph stuff
// ============================================
ExeGraph::ExeGraph(WorkSpace* ws, ParExeProc* proc, ParExeSequence *seq, const PropList &props) :
		otawa::ParExeGraph(ws, proc, seq, props)
{
	mem = hard::MEMORY(ws);

	info = otawa::microblaze::INFO(ws->process());
	ASSERT(info);
	reg_count = ws->process()->platform()->regCount();

	// Find the pipeline stages
	for(ParExePipeline::StageIterator stage(proc->pipeline()); stage; stage++)
	{
		if(stage->name() == "MEM")
			mem_stage = stage;
		else if(stage->name() == "EX")
			for(int i = 0; i < stage->numFus(); i++)
				for(ParExePipeline::StageIterator fu(stage->fu(i)); fu; fu++)
					if(fu->name() == "ALU")
						exe_stage = fu;
	}

	assert(mem_stage);
	assert(exe_stage);
}

static ParExeNode* findMemoryStage(ParExeInst* instruction)
{
	for(ParExeGraph::InstNodeIterator node(instruction); node; node++)
	{
//		elm::cout << node->stage()->index() << elm::io::endl;
		if(node->stage()->name() == "MEM")
			return node;
	}

	ASSERTP(false, "No memory stage found in the pipeline...");
}

void ExeGraph::build(void)
{
	// Call default implementation
	ParExeGraph::build();

	// Fixup and add memory latencies
	for(ParExeGraph::InstIterator inst(this); inst; inst++)
	{
		if(inst->inst()->kind() & Inst::IS_MEM)
		{
			if(inst->inst()->kind() & Inst::IS_LOAD)
			{
				findMemoryStage(inst)->setDefaultLatency(1000);
			}
			else
			{
				findMemoryStage(inst)->setDefaultLatency(1000);
			}
		}
	}

	// Add edges between a used mem stage and the fetch stage
	// This is because we're currently assuming only a single
	// read/write port on the processor, which is a shared
	// resource.
	for(ParExeGraph::InstIterator inst(this); inst; inst++)
	{
		if(inst->inst()->kind() & Inst::IS_MEM)
		{
			ParExeNode* mem_stage = findMemoryStage(inst);
			ParExeGraph::InstIterator curInst(inst);
			ParExeGraph::InstIterator nextInst(curInst);
			nextInst++;
			
			int memIndex = mem_stage->stage()->index();

			while(memIndex)
			{
				if(!nextInst)
					break;

				ParExeNode* source_node = curInst->node(memIndex);
				ParExeNode* dest_node = nextInst->node(memIndex - 1);

				new ParExeEdge(source_node, dest_node, ParExeEdge::SOLID);

				curInst++;
				nextInst++;
				memIndex--;
			}
		}
	}
}

// ============================================
// BBTimer stuff
// ============================================
BBTimer::BBTimer(void) : GraphBBTime<ExeGraph>(reg) { }

p::declare BBTimer::reg = p::init("otawa::microblaze::BBTimer", Version(1, 0, 0))
	.base(GraphBBTime<ParExeGraph>::reg)
	.maker<BBTimer>();

} // otawa::microblaze
} // otawa
