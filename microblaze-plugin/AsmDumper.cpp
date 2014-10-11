// Every now and then, you get one of those stupid ideas to use a tool
// to do something it was *never* meant to do.
// Well, this is the same thing. This module is designed to dump the ASM
// back out of an OTAWA CFG, and relink the branches.
// THIS IS A MASSIVE HACK. THIS IS NOT QUALITY WORK. OTAWA WAS NEVER MEANT TO DO THIS.
// This will not work with computed branches for (hopefully) obvious reasons.
// If this works at all, I'll be amazed.
// Here we go...

// TODO: Optimisation: If a branch is marked as "taken", but it goes to the next
// BB, then just remove the branch.

#include "AsmDumper.h"

#include <otawa/otawa.h>
#include <otawa/cfg.h>
#include <otawa/cfg/CFGCollector.h>
#include <otawa/util/Dominance.h>

#include <elm/string/StringBuffer.h>
#include <elm/io/UnixOutStream.h>
#include <elm/sys/System.h>

#include <elm/genstruct/Vector.h>
#include <elm/genstruct/VectorQueue.h>

#include "FunctionSplitter.h"

namespace otawa { namespace microblaze {

AsmDumper::AsmDumper(p::declare& r) : Processor(r)
{

}

void AsmDumper::processWorkSpace(WorkSpace* ws)
{
	const CFGCollection* cfgs = INVOLVED_CFGS(ws);
	ASSERT(cfgs);

	for(CFGCollection::Iterator cfg(cfgs); cfg; cfg++)
	{
		elm::string cfgName = cfg->label();
		cfgName = _ << cfgName << ".S";
		Path p = cfgName;
		Path baseDir = "asmout";
		p = baseDir / p;

		elm::io::OutStream* strm = elm::sys::System::createFile(p);
		io::Output output(*strm);

		// First dump the global decl for the first region
		output << ".globl " << cfg->label() << "_BB1;" << elm::io::endl;

		// So, first, we need to get a queue with all of the regions in it...
		elm::genstruct::Vector<FunctionRegion*> regions;
		for(CFG::BBIterator bb(cfg); bb; bb++)
			if(REGION(bb) != 0 && !regions.contains(REGION(bb)))
				regions.add(REGION(bb));

		elm::genstruct::Vector<BasicBlock*> visited;

		for(elm::genstruct::Vector<FunctionRegion*>::Iterator region(regions); region; region++)
		{
			// Dump all of the BBs...
			elm::genstruct::Vector<BasicBlock*> bbs = region->getContainedBBs();
			elm::genstruct::VectorQueue<BasicBlock*> todo;
			
			BasicBlock* next = bbs[0];

			while(next != 0)
			{
				if(visited.contains(next))
				{
					// Oops. Get something from the todo queue and try again
					if(todo.isEmpty())
						next = 0;
					else
						next = todo.get();

					continue;
				}

				visited.add(next);
				output << cfg->label() << "_BB" << next->number() << ":" << elm::io::endl;
				for(BasicBlock::InstIterator inst(next); inst; inst++)
				{
					elm::StringBuffer instTxtBuffer;
					elm::String instTxt;

					// Get the instruction text
					inst->dump(instTxtBuffer);
					instTxt = instTxtBuffer.toString();

					// Is the instruction a branch?
					if(inst->kind() & Inst::IS_CONTROL)
					{
						// Figure out where it goes!
						Inst* tgt = inst->target();
						if(!tgt) // Normally happens in the case of a return...
							elm::cout << "BB" << next->number() << " No target..." << elm::io::endl;
						else
						{
							elm::cout << "Got target!" << elm::io::endl;

							// Take both out edges, and figure out where it actually goes
							for(BasicBlock::OutIterator out(next); out; out++)
							{
								if(out->target()->address() == tgt->address())
								{
									elm::cout << "BB " << next->number() << " jump to BB" << out->target()->number() << elm::io::endl;
									int jumpAddrSplit = instTxt.lastIndexOf(',');
									if(jumpAddrSplit == -1)
										jumpAddrSplit = instTxt.lastIndexOf(' ');

									// Trim off the jump address
									instTxt = instTxt.substring(0, jumpAddrSplit + 1);

									elm::StringBuffer newInstStrBuffer;
									newInstStrBuffer << instTxt << out->target()->cfg()->label() << "_BB" << out->target()->number();
									instTxt = newInstStrBuffer.toString();

									break;
								}
							}
						}
					}

					output << "\t" << instTxt << elm::io::endl;
				}

				// Now figure out where to go next
				BasicBlock* newNext = 0;
				for(BasicBlock::OutIterator out(next); out; out++)
				{
					// Not taken normally implies fall through.
					if(out->kind() == Edge::NOT_TAKEN)
					{
						// Is it in the same region?
						if(REGION(next) != REGION(out->target()))
						{
							// Fallthru across regions, add a jump to it since it'll move
							elm::StringBuffer newInst;
							newInst << "\tbri " << out->target()->cfg()->label() << "_BB" << out->target()->number() << " # ADDED";

							output << newInst.toString() << elm::io::endl;
						}
						else // Fallthru. Go there next
							newNext = out->target();
					}
					else if(out->kind() != Edge::CALL && !visited.contains(out->target())) // Jump. Queue it for later.
					{
						elm::cout << "Queueing edge of type " << Edge::kindName(out->kind()) << elm::io::endl;
						todo.put(out->target());
					}
				}

				// If we don't have fallthru, get something from the work list.
				if(newNext == 0 && !todo.isEmpty())
					newNext = todo.get();

				next = newNext;
			} // while(next != 0)
		}

		//return;
	}
}

p::declare AsmDumper::reg = p::init("otawa::microblaze::AsmDumper", Version(1,0,0))
							.maker<AsmDumper>()
							.require(otawa::COLLECTED_CFG_FEATURE)
							.require(otawa::LOOP_INFO_FEATURE);

} // otawa::microblaze
} // otawa