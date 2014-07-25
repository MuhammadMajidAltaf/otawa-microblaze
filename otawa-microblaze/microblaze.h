/*
 *	Microblaze loader header file
 */
#ifndef OTAWA_MICROBLAZE_H
#define OTAWA_MICROBLAZE_H

#include <otawa/proc/Feature.h>

namespace otawa { namespace microblaze {

using namespace elm;
using namespace otawa;

class Info {
public:
	Info(Process& _proc);
	int bundleSize(const Address& addr);
private:
	Process& proc;
};

extern Identifier<Info *> INFO;
extern Feature<NoProcessor> INFO_FEATURE;

} } // otawa::microblaze

#endif // OTAWA_MICROBLAZE_H
