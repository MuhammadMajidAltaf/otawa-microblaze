#include "DOMOutput.h"

#include <otawa/otawa.h>
#include <otawa/cfg/CFGInfo.h>
#include <otawa/cfg.h>
#include <otawa/cfg/CFGCollector.h>

#include <otawa/util/PostDominance.h>
#include <otawa/util/Dominance.h>
#include <otawa/dfa/BitSet.h>

#include <elm/io/UnixOutStream.h>
#include <elm/sys/System.h>

using namespace otawa;
using namespace otawa::dfa;

namespace otawa { namespace microblaze { 

DOMOutput::DOMOutput(p::declare& r) : Processor(r)
{

}

void DOMOutput::processWorkSpace(WorkSpace* ws)
{
	const CFGCollection* cfgs = INVOLVED_CFGS(ws);
	ASSERT(cfgs);

	for(CFGCollection::Iterator cfg(cfgs); cfg; cfg++)
	{
		elm::string cfgName = cfg->label();
		cfgName = _ << cfgName << ".dot";
		Path p = cfgName;
		Path baseDir = "domout";
		p = baseDir / p;

		elm::io::OutStream* strm = elm::sys::System::createFile(p);
		io::Output output(*strm);

		output << "digraph LOL {" << elm::io::endl;

		for(CFG::BBIterator bb(cfg); bb; bb++)
		{
			output << "BB" << bb->number() << " [label=\"BB" << bb->number() << "\"];" << elm::io::endl;
		}

		for(CFG::BBIterator bb(cfg); bb; bb++)
		{
			for(CFG::BBIterator bb2(cfg); bb2; bb2++)
			{	
				BitSet* dom2 = REVERSE_POSTDOM(bb2);

				if(dom2->contains(bb->number()))
					output << "BB" << bb->number() << " -> BB" << bb2->number() << ";" << elm::io::endl;
			}
		}

		output << "} "<< elm::io::endl;

		delete strm;
	}
}

p::declare DOMOutput::reg = p::init("otawa::microblaze::DOMOutput", Version(1,0,0))
							.maker<DOMOutput>()
							.require(otawa::DOMINANCE_FEATURE);

} // otawa::microblaze
} // otawa