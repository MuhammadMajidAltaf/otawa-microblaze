#include <elm/assert.h>
#include <otawa/prog/Manager.h>
#include <otawa/prog/Loader.h>
#include <otawa/platform.h>
#include <otawa/hard/Register.h>
#include <gel/gel.h>
#include <gel/gel_elf.h>
#include <gel/image.h>
#include <gel/debug_line.h>
#include <otawa/proc/Processor.h>
#include <otawa/util/FlowFactLoader.h>
#include <elm/genstruct/SortedSLList.h>
#include <otawa/sim/features.h>
#include <otawa/prop/Identifier.h>
#include "microblaze.h"


extern "C"
{
	// gliss2 C include files
	#include <microblaze/api.h>
	#include <microblaze/id.h>
	#include <microblaze/macros.h>

	// generated code
	#include <microblaze/used_regs.h>

	#include "config.h"
}

/* instruction kind */
#include "otawa_kind.h"

/* target computation */
#include "otawa_target.h"

#include "otawa_delayed.h"


using namespace otawa::hard;


#define TRACE(m) //cerr << m << io::endl
#define LTRACE	 //cerr << "POINT " << __FILE__ << ":" << __LINE__ << io::endl
#define RTRACE(m)	//m
//#define SCAN_ARGS

// Trace for switch parsing
#define STRACE(m)	//cerr << m << io::endl


namespace otawa { namespace microblaze {

/**
 * Register banks.
 */
static hard::PlainBank regR("D", hard::Register::INT,  32, "$r%d", 32);

static hard::Register  regPC ("PC",  hard::Register::ADDR, 32);
static hard::Register  regNPC("nPC", hard::Register::ADDR, 32);

static hard::Register  regCarry("CARRY", hard::Register::INT, 1);
static hard::Register  regBranchDelayed("BRANCHDELAYED", hard::Register::INT, 1);

static const hard::MeltedBank misc1("misc1", 4, &regPC, &regNPC, &regCarry, &regBranchDelayed, 0);

static const hard::RegBank *banks[] = {
        &regR,
        &misc1,
};

static const elm::genstruct::Table<const hard::RegBank *> banks_table(banks, 2);


// register decoding
class RegisterDecoder {
public:
    RegisterDecoder(void) {

        // clear the map
        for(int i = 0; i < MICROBLAZE_REG_COUNT; i++)
            map[i] = 0;

        // initialize it
        for(int i = 0; i < 32; i++) {
            map[MICROBLAZE_REG_R(i)] = regR[i];
		}

		map[MICROBLAZE_REG_C] = &regCarry;
		map[MICROBLAZE_REG_DB] = &regBranchDelayed;
		map[MICROBLAZE_REG_PC] = &regPC;
		map[MICROBLAZE_REG_NPC] = &regNPC;
    }

    inline hard::Register *operator[](int i) const { return map[i]; }

private:
    hard::Register *map[MICROBLAZE_REG_COUNT];
};
static RegisterDecoder register_decoder; 


// Platform class
class Platform: public hard::Platform {
public:
	static const Identification ID;

	/**
	 * Build a new gliss platform with the given configuration.
	 * @param props		Configuration properties.
	 */
        Platform(const PropList& props = PropList::EMPTY): hard::Platform(ID, props)
                { setBanks(banks_table); }
	/**
	 * Build a new platform by cloning.
	 * @param platform	Platform to clone.
	 * @param props		Configuration properties.
	 */
        Platform(const Platform& platform, const PropList& props = PropList::EMPTY)
                : hard::Platform(platform, props)
                { setBanks(banks_table); }

        // otawa::Platform overload
        virtual bool accept(const Identification& id)
                { return id.abi() == "eabi" && id.architecture() == "microblaze"; }
};

/**
 * Identification of the default platform.
 */
const Platform::Identification Platform::ID("microblaze-");


/**
 * This class provides support to build a loader plug-in based on the GLISS V2
 * with ELF file loading based on the GEL library.
 *
 * This class allows to load a binary file, extract the instructions and the
 * symbols (labels and function). You have to provide a consistent
 * platform description for the processor.
 *
 * The details of the interface with V2 GLISS are managed by this class and you
 * have only to write :
 *   - the platform description,
 *   - the recognition of the instruction,
 *   - the assignment of the memory pointer.
 */
class Process: public otawa::Process, public otawa::DelayedInfo {
public:
	Process(Manager *manager, hard::Platform *pf, const PropList& props = PropList::EMPTY);

	~Process();

	virtual otawa::SimState *newState(void) {
		//microblaze_state_t *s = microblaze_new_state(_microblazePlatform);
		//ASSERTP(s, "otawa::microblaze::Process::newState(), cannot create a new microblaze_state");
		//return new SimState(this, s, _microblazeDecoder, true);
		return 0;
	}

	virtual int instSize(void) const { return 4; }

	void decodeRegs( Inst *inst, elm::genstruct::AllocatedTable<hard::Register *> *in, elm::genstruct::AllocatedTable<hard::Register *> *out);

	inline microblaze_decoder_t *microblazeDecoder() { return _microblazeDecoder;}

	inline void *microblazePlatform(void) const { return _microblazePlatform; }

	void setup(void);

	void getSem(otawa::Inst *inst, sem::Block& block);

	// Process Overloads
	virtual hard::Platform *platform(void) { return _platform; }
	virtual otawa::Inst *start(void) { return _start; }

	virtual File *loadFile(elm::CString path);
	virtual void get(Address at, t::int8& val);
	virtual void get(Address at, t::uint8& val);
	virtual void get(Address at, t::int16& val);
	virtual void get(Address at, t::uint16& val);
	virtual void get(Address at, t::int32& val);
	virtual void get(Address at, t::uint32& val);
	virtual void get(Address at, t::int64& val);
	virtual void get(Address at, t::uint64& val);
	virtual void get(Address at, Address& val);
	virtual void get(Address at, string& str);
	virtual void get(Address at, char *buf, int size);

	virtual Option<Pair<cstring, int> > getSourceLine(Address addr)
		throw (UnsupportedFeatureException);
	virtual void getAddresses(cstring file, int line, Vector<Pair<Address, Address> >& addresses)
		throw (UnsupportedFeatureException);

	// DelayedInfo implementation
	virtual delayed_t type(Inst *inst) {
		// Delayed instruction is executed regardless of if the branch is taken
		if(inst->isControl())
			return otawa::DELAYED_Always;
		else
			return otawa::DELAYED_None;
	}

	virtual int count(Inst *oinst) {
		// Get the actual instruction
		microblaze_inst_t *inst;
		inst = microblaze_decode(microblazeDecoder(), (microblaze_address_t)oinst->address());

		// Get how many delay slots there are for the instruction.
		int n = microblaze_delayed(inst);

		return n;
	}

protected:
	friend class Segment;

	virtual otawa::Inst *decode(Address addr);

	virtual gel_file_t *gelFile(void) { return _gelFile; }

	virtual microblaze_memory_t *microblazeMemory(void) { return _microblazeMemory; }

private:
	otawa::Inst *_start;
	hard::Platform *_platform;
	microblaze_platform_t *_microblazePlatform;
	microblaze_memory_t *_microblazeMemory;
	microblaze_decoder_t *_microblazeDecoder;
	int argc;
	char **argv, **envp;
	bool no_stack;
	bool init;
	struct gel_line_map_t *map;
	struct gel_file_info_t *file;
	gel_file_t *_gelFile;
	Info info;
};

// Process display
elm::io::Output& operator<<(elm::io::Output& out, Process *proc)
	{ out << "Process(" << (void *)proc << ")"; }


// Inst class
class Inst: public otawa::Inst {
public:

	inline Inst(Process& process, kind_t kind, Address addr, ot::size size)
		: proc(process), _kind(kind), _addr(addr), isRegsDone(false), _size(size) { }

	/**
	 */
	void dump(io::Output& out) {
		char out_buffer[200];
		microblaze_inst_t *inst = microblaze_decode(proc.microblazeDecoder(), _addr);
		microblaze_disasm(out_buffer, inst);
		microblaze_free_inst(inst);
		out << out_buffer;
	}

	virtual kind_t kind(void) { return _kind; }
	virtual address_t address(void) const { return _addr; }
	virtual Process &process() { return proc; }

	virtual ot::size size(void) const { return _size; }

	virtual const elm::genstruct::Table<hard::Register *>& readRegs() {
		if (!isRegsDone) {
			decodeRegs();
			isRegsDone = true;
		}
		return in_regs;
	}

	virtual const elm::genstruct::Table<hard::Register *>& writtenRegs() {
		if (!isRegsDone) {
			decodeRegs();
			isRegsDone = true;
		}
		return out_regs;
	}

	virtual void semInsts (sem::Block &block) {
		proc.getSem(this, block);
	}

protected:
	virtual void decodeRegs(void) {
		proc.decodeRegs(this, &in_regs, &out_regs);
	}

	kind_t _kind;
	elm::genstruct::AllocatedTable<hard::Register *> in_regs;
	elm::genstruct::AllocatedTable<hard::Register *> out_regs;
	Process &proc;

private:
	microblaze_address_t _addr;
	ot::size _size;
	bool isRegsDone;
};


// BranchInst class
class BranchInst: public Inst {
public:

	inline BranchInst(Process& process, kind_t kind, Address addr, ot::size size)
	: Inst(process, kind, addr, size), _target(0), isTargetDone(false) {
	}

	virtual ot::size size() const { return 4; }

	virtual otawa::Inst *target() {
		if (!isTargetDone) {
			microblaze_address_t a = decodeTargetAddress();
			if (a)
				_target = process().findInstAt(a);
			isTargetDone = true;
		}
		return _target;
	}

        virtual delayed_t delayType(void) {
	  return _delaySlots > 0 ? otawa::DELAYED_Always : otawa::DELAYED_None;
	}

	virtual int delaySlots(void) {
	  return _delaySlots;
	}

protected:
	virtual microblaze_address_t decodeTargetAddress(void);

private:
	otawa::Inst *_target;
	int _delaySlots;
	bool isTargetDone;
};


// Segment class
class Segment: public otawa::Segment {
public:
	Segment(Process& process,
		CString name,
		address_t address,
		size_t size)
	: otawa::Segment(name, address, size, EXECUTABLE), proc(process) { }

protected:
	virtual otawa::Inst *decode(address_t address)
		{ return proc.decode(address); }

private:
	Process& proc;
};




 /**
  * Build a process for the new GLISS V2 system.
  * @param manager	Current manager.
  * @param platform	Current platform.
  * @param props	Building properties.
  */
Process::Process(Manager *manager, hard::Platform *platform, const PropList& props)
:	otawa::Process(manager, props),
 	_start(0),
 	_platform(platform),
	_microblazeMemory(0),
	init(false),
	map(0),
	file(0),
	info(*this)
{
	ASSERTP(manager, "manager required");
	ASSERTP(platform, "platform required");

	// gliss2 microblaze structs
	_microblazePlatform = microblaze_new_platform();
	ASSERTP(_microblazePlatform, "otawa::microblaze::Process::Process(..), cannot create a microblaze_platform");
	_microblazeDecoder = microblaze_new_decoder(_microblazePlatform);
	ASSERTP(_microblazeDecoder, "otawa::microblaze::Process::Process(..), cannot create a microblaze_decoder");
	_microblazeMemory = microblaze_get_memory(_microblazePlatform, MICROBLAZE_MAIN_MEMORY);
	ASSERTP(_microblazeMemory, "otawa::microblaze::Process::Process(..), cannot get main microblaze_memory");
	microblaze_lock_platform(_microblazePlatform);

	// build arguments
	char no_name[1] = { 0 };
	static char *default_argv[] = { no_name, 0 };
	static char *default_envp[] = { 0 };
	argc = ARGC(props);
	if (argc < 0)
		argc = 1;
	argv = ARGV(props);
	if (!argv)
		argv = default_argv;
	envp = ENVP(props);
	if (!envp)
		envp = default_envp;

	// handle features
	INFO(this) = &info;
	provide(INFO_FEATURE);
	provide(MEMORY_ACCESS_FEATURE);
	provide(SOURCE_LINE_FEATURE);
	provide(CONTROL_DECODING_FEATURE);
	provide(REGISTER_USAGE_FEATURE);
	provide(MEMORY_ACCESSES);
	DELAYED_INFO(this) = this;
	provide(DELAYED2_FEATURE);
}


/**
 */
Process::~Process() {
	microblaze_delete_decoder(_microblazeDecoder);
	microblaze_unlock_platform(_microblazePlatform);
	if(_gelFile)
		gel_close(_gelFile);
}



Option<Pair<cstring, int> > Process::getSourceLine(Address addr) throw (UnsupportedFeatureException) {
	setup();
	if (!map)
		return none;
	const char *file;
	int line;
	if (!map || gel_line_from_address(map, addr.offset(), &file, &line) < 0)
		return none;
	return some(pair(cstring(file), line));
}


void Process::getAddresses(cstring file, int line, Vector<Pair<Address, Address> >& addresses) throw (UnsupportedFeatureException) {
	setup();
	addresses.clear();
	if (!map)
		return;
	gel_line_iter_t iter;
	gel_location_t loc, ploc = { 0, 0, 0, 0 };
	for (loc = gel_first_line(&iter, map); loc.file; loc = gel_next_line(&iter))
	{
		cstring lfile = loc.file;
		//cerr << loc.file << ":" << loc.line << ", " << loc.low_addr << "-" << loc.high_addr << io::endl;
		if (file == loc.file || lfile.endsWith(file))
		{
			if (line == loc.line)
			{
				//cerr << "added (1) " << loc.file << ":" << loc.line << " -> " << loc.low_addr << io::endl;
				addresses.add(pair(Address(loc.low_addr), Address(loc.high_addr)));
			}
			else if(loc.file == ploc.file && line > ploc.line && line < loc.line)
			{
				//cerr << "added (2) " << ploc.file << ":" << ploc.line << " -> " << ploc.low_addr << io::endl;
				addresses.add(pair(Address(ploc.low_addr), Address(ploc.high_addr)));
			}
		}
		ploc = loc;
	}
}


/**
 * Setup the source line map.
 */
void Process::setup(void) {
	ASSERT(_gelFile);
	if(init)
		return;
	init = true;
	map = gel_new_line_map(_gelFile);
}

File *Process::loadFile(elm::CString path) {
	LTRACE;

	// Check if there is not an already opened file !
	if(program())
		throw LoadException("loader cannot open multiple files !");

	File *file = new otawa::File(path);
	addFile(file);

	// initialize the environment
	ASSERTP(_microblazePlatform, "invalid microblaze_platform !");
	microblaze_env_t *env = microblaze_get_sys_env(_microblazePlatform);
	ASSERT(env);
	env->argc = argc;
	env->argv = argv;
	env->envp = envp;

	// load the binary
	if(microblaze_load_platform(_microblazePlatform, (char *)&path) == -1)
		throw LoadException(_ << "cannot load \"" << path << "\".");

	// get the initial state
	//SimState *state = dynamic_cast<SimState *>(newState());
	//microblaze_state_t *microblazeState = state->microblazeState();
	//if (!microblazeState)
	//	throw LoadException("invalid microblaze_state !");

	// build segments
	_gelFile = gel_open(&path, 0, GEL_OPEN_NOPLUGINS);
	if(!_gelFile)
		throw LoadException(_ << "cannot load \"" << path << "\".");
	gel_file_info_t infos;
	gel_file_infos(_gelFile, &infos);
	for (int i = 0; i < infos.sectnum; i++) {
		gel_sect_info_t infos;
		gel_sect_t *sect = gel_getsectbyidx(_gelFile, i);
		assert(sect);
		gel_sect_infos(sect, &infos);
		if (infos.flags & SHF_EXECINSTR) {
			Segment *seg = new Segment(*this, infos.name, infos.vaddr, infos.size);
			file->addSegment(seg);
		}
	}

	// Initialize symbols
	LTRACE;
	gel_enum_t *iter = gel_enum_file_symbol(_gelFile);
	gel_enum_initpos(iter);
	for(char *name = (char *)gel_enum_next(iter); name; name = (char *)gel_enum_next(iter)) {
		ASSERT(name);
		address_t addr = 0;
		Symbol::kind_t kind;
		gel_sym_t *sym = gel_find_file_symbol(_gelFile, name);
		assert(sym);
		gel_sym_info_t infos;
		gel_sym_infos(sym, &infos);
		switch(ELF32_ST_TYPE(infos.info)) {
		case STT_FUNC:
			kind = Symbol::FUNCTION;
			addr = (address_t)infos.vaddr;
			TRACE("SYMBOL: function " << infos.name << " at " << addr);
			break;
		case STT_NOTYPE:
			kind = Symbol::LABEL;
			addr = (address_t)infos.vaddr;
			TRACE("SYMBOL: notype " << infos.name << " at " << addr);
			break;
		default:
			continue;
		}

		// Build the label if required
		if(addr) {
			String label(infos.name);
			Symbol *sym = new Symbol(*file, label, kind, addr);
			file->addSymbol(sym);
			TRACE("function " << label << " at " << addr);
		}
	}
	gel_enum_free(iter);

	// Last initializations
	LTRACE;
	_microblazeMemory = microblazeMemory();
	ASSERTP(_microblazeMemory, "memory information mandatory");
	_start = findInstAt((address_t)infos.entry);
	return file;
}


// Memory read
#define GET(t, s) \
	void Process::get(Address at, t& val) { \
			val = microblaze_mem_read##s(_microblazeMemory, at.offset()); \
			/*cerr << "val = " << (void *)(int)val << " at " << at << io::endl;*/ \
	}
GET(t::int8, 8);
GET(t::uint8, 8);
GET(t::int16, 16);
GET(t::uint16, 16);
GET(t::int32, 32);
GET(t::uint32, 32);
GET(t::int64, 64);
GET(t::uint64, 64);
GET(Address, 32);


void Process::get(Address at, string& str) {
	Address base = at;
	while(!microblaze_mem_read8(_microblazeMemory, at.offset()))
		at = at + 1;
	int len = at - base;
	char buf[len];
	get(base, buf, len);
	str = String(buf, len);
}


void Process::get(Address at, char *buf, int size)
	{ microblaze_mem_read(_microblazeMemory, at.offset(), buf, size); }



void Process::decodeRegs(otawa::Inst *oinst, elm::genstruct::AllocatedTable<hard::Register *> *in,
	elm::genstruct::AllocatedTable<hard::Register *> *out)
{
	// Decode instruction
	microblaze_inst_t *inst;
	inst = microblaze_decode(_microblazeDecoder, oinst->address().offset());
	if(inst->ident == MICROBLAZE_UNKNOWN)
	{
		microblaze_free_inst(inst);
		return;
	}

	// get register infos
	microblaze_used_regs_read_t rds;
	microblaze_used_regs_write_t wrs;
	elm::genstruct::Vector<hard::Register *> reg_in;
	elm::genstruct::Vector<hard::Register *> reg_out;
        microblaze_used_regs(inst, rds, wrs);
        for (int i = 0; rds[i] != -1; i++ ) {
            hard::Register *r = register_decoder[rds[i]];
            if(r)
                reg_in.add(r);
        }
        for (int i = 0; wrs[i] != -1; i++ ) {
            hard::Register *r = register_decoder[wrs[i]];
            if(r)
                reg_out.add(r);
        } 

	// store results
	int cpt_in = reg_in.length();
	in->allocate(cpt_in);
	for (int i = 0 ; i < cpt_in ; i++)
		in->set(i, reg_in.get(i));
	int cpt_out = reg_out.length();
	out->allocate(cpt_out);
	for (int i = 0 ; i < cpt_out ; i++)
		out->set(i, reg_out.get(i));

	// Free instruction
	microblaze_free_inst(inst);
}


otawa::Inst *Process::decode(Address addr) {
	//cerr << "DECODING: " << addr << io::endl;

	// Decode the instruction
	microblaze_inst_t *inst;
	TRACE("ADDR " << addr);
	inst = microblaze_decode(_microblazeDecoder, (microblaze_address_t)addr.offset());

	// Build the instruction
	Inst::kind_t kind = 0;
	otawa::Inst *result = 0;

	// get the kind from the nmp otawa_kind attribute
	if(inst->ident == MICROBLAZE_UNKNOWN)
		TRACE("UNKNOWN !!!\n" << result);
	else
		kind = microblaze_kind(inst);

	bool is_branch = kind & Inst::IS_CONTROL;

	// build the object
	ot::size size = microblaze_get_inst_size(inst) / 8;
	if (is_branch)
	{
		//elm::cout << "BRANCH!\n";
		result = new BranchInst(*this, kind, addr, size);
	}
	else
	{
		//elm::cout << "NOT BRANCH\n";
		result = new Inst(*this, kind, addr, size);
	}

	// cleanup
	ASSERT(result);
	microblaze_free_inst(inst);
	return result;
}



microblaze_address_t BranchInst::decodeTargetAddress(void) {

	// Decode the instruction
	microblaze_inst_t *inst;

	inst = microblaze_decode(proc.microblazeDecoder(), (microblaze_address_t)address());

	// retrieve the target addr from the nmp otawa_target attribute
	Address target_addr;
	microblaze_address_t res = microblaze_target(inst);
	if(res != 0)
		target_addr = res;

	// Return result
	microblaze_free_inst(inst);
	return target_addr;
}


// otawa::loader::microblaze::Loader class
class Loader: public otawa::Loader {
public:
	Loader(void);

	// otawa::Loader overload
	virtual CString getName(void) const;
	virtual otawa::Process *load(Manager *_man, CString path, const PropList& props);
	virtual otawa::Process *create(Manager *_man, const PropList& props);
};


// Alias table
static string table[] = {
	"elf_189"
};
static elm::genstruct::Table<string> microblaze_aliases(table, 1);


/**
 * Build a new loader.
 */
Loader::Loader(void)
: otawa::Loader("microblaze", Version(1, 0, 0), OTAWA_LOADER_VERSION, microblaze_aliases) {
}


/**
 * Get the name of the loader.
 * @return Loader name.
 */
CString Loader::getName(void) const
{
	return "microblaze";
}


/**
 * Load a file with the current loader.
 * @param man		Caller manager.
 * @param path		Path to the file.
 * @param props	Properties.
 * @return	Created process or null if there is an error.
 */
otawa::Process *Loader::load(Manager *man, CString path, const PropList& props)
{
	otawa::Process *proc = create(man, props);
	if (!proc->loadProgram(path))
	{
		delete proc;
		return 0;
	}
	else
		return proc;
}


/**
 * Create an empty process.
 * @param man		Caller manager.
 * @param props	Properties.
 * @return		Created process.
 */
otawa::Process *Loader::create(Manager *man, const PropList& props)
{
	return new Process(man, new Platform(props), props);
}


/**
 * @class Info
 * Provide information specific to the Microblaze architecture.
 */
 
 
/**
 * Constructor.
 */
Info::Info(otawa::Process& _proc): proc(_proc) {
}

/**
 * Provide access to @ref Info data structure.
 * 
 * @p Features
 * @li @ref INFO_FEATURE
 * 
 * @p Hooks
 * @li Process
 */
Identifier<Info *> INFO("otawa::microblaze::INFO", 0);


/**
 * Feature ensuring that information about Microblaze are available.
 * 
 * @p Properties
 * @li @ref INFO
 */
Feature<NoProcessor> INFO_FEATURE("otawa::microblaze::INFO_FEATURE");

} }	// otawa::microblaze

// Semantics information - Generic functions and constants
#define _NOP()			block.add(otawa::sem::nop())
#define _BRANCH(d)		block.add(otawa::sem::branch(d))
#define _TRAP()		        block.add(otawa::sem::trap())
#define _CONT()		        block.add(otawa::sem::cont())
#define _IF(cond, s1, jmp)	block.add(otawa::sem::_if(cond, s1, jmp))
#define _LOAD(d, s1, s2)	block.add(otawa::sem::load(d, s1, s2))
#define _STORE(d, s1, s2)	block.add(otawa::sem::store(d, s1, s2))
#define _SCRATCH(d)		block.add(otawa::sem::scratch(d))
#define _SET(d, s)		block.add(otawa::sem::set(d, s))
#define _SETI(d, i)		block.add(otawa::sem::seti(d, i))
#define _CMP(d, s1, s2)		block.add(otawa::sem::cmp(d, s1, s2))
#define _CMPU(d, s1, s2)	block.add(otawa::sem::cmpu(d, s1, s2))
#define _ADD(d, s1, s2)		block.add(otawa::sem::add(d, s1, s2))
#define _SUB(d, s1, s2)		block.add(otawa::sem::sub(d, s1, s2))
#define _SHL(d, s1, s2)		block.add(otawa::sem::shl(d, s1, s2))
#define _SHR(d, s1, s2)		block.add(otawa::sem::shr(d, s1, s2))
#define _ASR(d, s1, s2)		block.add(otawa::sem::asr(d, s1, s2))
#define _NOT(d, s1)		block.add(otawa::sem::_not(d, s1))
#define _AND(d, s1, s2)		block.add(otawa::sem::_and(d, s1, s2))
#define _OR(d, s1, s2)		block.add(otawa::sem::_or(d, s1, s2))
#define _MUL(d, s1, s2)		block.add(otawa::sem::mul(d, s1, s2))
#define _MULU(d, s1, s2)	block.add(otawa::sem::mulu(d, s1, s2))
#define _XOR(d, s1, s2)		block.add(otawa::sem::_xor(d, s1, s2))
#define _NO_COND		otawa::sem::NO_COND
#define _EQ			otawa::sem::EQ
#define _LT			otawa::sem::LT
#define _LE			otawa::sem::LE
#define _GE			otawa::sem::GE
#define _GT			otawa::sem::GT
#define _ANY_COND		otawa::sem::ANY_COND
#define _NE			otawa::sem::NE
#define _ULT			otawa::sem::ULT
#define _ULE			otawa::sem::ULE
#define _UGE			otawa::sem::UGE
#define _UGT			otawa::sem::UGT
#define _INT8			otawa::sem::INT8
#define _INT16			otawa::sem::INT16
#define _INT32			otawa::sem::INT32
#define _INT64			otawa::sem::INT64
#define _UINT8			otawa::sem::UINT8
#define _UINT16			otawa::sem::UINT16
#define _UINT32			otawa::sem::UINT32
#define _UINT64			otawa::sem::UINT64
// Semantics information - Microblaze bindings
#define _R(n)			otawa::microblaze::regR[n]->platformNumber()
#define _CARRY          otawa::microblaze::regCarry.platformNumber()
#define _BRANCH_DELAYED otawa::microblaze::regBranchDelayed.platformNumber()
#define _PC             otawa::microblaze::regPC.platformNumber()
#include "otawa_sem.h"


namespace otawa { namespace microblaze {

void Process::getSem(::otawa::Inst *oinst, ::otawa::sem::Block& block) {
	microblaze_inst_t *inst;
	inst = microblaze_decode(_microblazeDecoder, oinst->address().offset());
	microblaze_sem(inst, block);
	microblaze_free_inst(inst);
}

} }	// namespace otawa::microblaze

// microblaze GLISS Loader entry point
otawa::microblaze::Loader OTAWA_LOADER_HOOK;
otawa::microblaze::Loader& microblaze_plugin = OTAWA_LOADER_HOOK;
