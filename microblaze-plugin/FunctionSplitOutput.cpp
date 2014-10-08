#include "FunctionSplitOutput.h"

#include <otawa/otawa.h>
#include <otawa/cfg/CFGInfo.h>
#include <otawa/cfg.h>
#include <otawa/cfg/CFGCollector.h>

#include <elm/io/UnixOutStream.h>
#include <elm/sys/System.h>

#include "FunctionSplitter.h"

using namespace otawa;

namespace otawa { namespace microblaze {

FunctionSplitOutput::FunctionSplitOutput(p::declare& r) : Processor(r)
{

}

void FunctionSplitOutput::processWorkSpace(WorkSpace* ws)
{
	static elm::string colours[] = { "red", "green", "blue", "cyan", "magenta", "yellow", "crimson", "forestgreen", "gold", "hotpink", "khaki", "lightsteelblue", "royalblue", "tan", "snow" };
	int colIndex = 0;
	int numCols = 15;

	const CFGCollection* cfgs = INVOLVED_CFGS(ws);
	ASSERT(cfgs);

	for(CFGCollection::Iterator cfg(cfgs); cfg; cfg++)
	{
		elm::string cfgName = cfg->label();
		cfgName = _ << cfgName << ".dot";
		Path p = cfgName;
		Path baseDir = "splitout";
		p = baseDir / p;

		elm::io::OutStream* strm = elm::sys::System::createFile(p);
		io::Output output(*strm);

		output << "digraph splitgraph {" << elm::io::endl;

		for(CFG::BBIterator bb(cfg); bb; bb++)
		{
			output << "BB" << bb->number() << " [label=\"BB" << bb->number() << "\",style=\"filled\",";

			FunctionRegion* fr = REGION(bb);
			if(fr == 0)
				output << "fillcolor=\"#ffffff\"]" << elm::io::endl;
			else
			{
				if(REGION_COLOUR(fr) == "#000000")
					REGION_COLOUR(fr) = colours[colIndex++ % numCols];
				elm::string regionColour = REGION_COLOUR(fr);
				output << "fillcolor=\"" << regionColour << "\"]" << elm::io::endl;
			}
		}

		// For each BB, now print out the links...
		for(CFG::BBIterator bb(cfg); bb; bb++)
		{
			for(BasicBlock::OutIterator next(bb); next; next++)
			{
				if(next->kind() == Edge::CALL)
					continue;
				
				if(!BACK_EDGE(next))
					output << "BB" << next->source()->number() << " -> BB" << next->target()->number() << "[label=\"" << Edge::kindName(next->kind()) << "\"];" << elm::io::endl;
			}
		}

		output << "}" << elm::io::endl;
	}
}

p::declare FunctionSplitOutput::reg = p::init("otawa::microblaze::FunctionSplitOutput", Version(1,0,0))
									  .maker<FunctionSplitOutput>()
									  .require(otawa::microblaze::FUNCTION_SPLIT_FEATURE);

Identifier<elm::string> REGION_COLOUR("otawa::microblaze::REGION_COLOUR", "#000000");
} // otawa::microblaze
} // otawa