// Every now and then, you get one of those stupid ideas to use a tool
// to do something it was *never* meant to do.
// Well, this is the same thing. This module is designed to dump the ASM
// back out of an OTAWA CFG, and relink the branches.
// THIS IS A MASSIVE HACK. THIS IS NOT QUALITY WORK. OTAWA WAS NEVER MEANT TO DO THIS.
// This will not work with computed branches for (hopefully) obvious reasons.
// If this works at all, I'll be amazed.
// Here we go...

#include "AsmDumper.h"

#include <otawa/otawa.h>
#include <otawa/cfg.h>
#include <otawa/cfg/CFGCollector.h>
#include <otawa/util/Dominance.h>

namespace otawa { namespace microblaze {

AsmDumper::AsmDumper(p::declare& r) : Processor(r)
{

}

void AsmDumper::processWorkSpace(WorkSpace* ws)
{

}

p::declare AsmDumper::reg = p::init("otawa::microblaze::AsmDumper", Version(1,0,0))
							.maker<AsmDumper>()
							.require(otawa::COLLECTED_CFG_FEATURE)
							.require(otawa::LOOP_INFO_FEATURE);

} // otawa::microblaze
} // otawa