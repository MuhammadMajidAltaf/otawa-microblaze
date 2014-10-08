#include "LoopStride.h"

#include <otawa/otawa.h>
#include <otawa/cfg/CFGInfo.h>
#include <otawa/cfg.h>
#include <otawa/cfg/CFGCollector.h>

#include <otawa/ipet/features.h>
#include <otawa/ipet/FlowFactLoader.h>
#include <otawa/util/FlowFactLoader.h>

#include <otawa/prog/sem.h>
#include <otawa/dfa/State.h>

#include <elm/int.h>

#define HAI_DEBUG
#include <otawa/util/HalfAbsInt.h>
#include <otawa/util/DefaultFixPoint.h>
#include <otawa/util/DefaultListener.h>
#include <otawa/util/AccessedAddress.h>
#include <otawa/hard/Register.h>
#include <otawa/hard/Platform.h>

using namespace otawa;
using namespace elm;
using namespace otawa::util;

#define abs(x) x < 0 ? -x : x

#define TRACEU(t)	t
#define TRACEI(t)	//t
#define TRACES(t)	//t
#define TRACED(t)	//t

namespace otawa { namespace microblaze { 

namespace loop {
	typedef enum {
		NONE,
		REG,
		CST,
		ALL
	} kind_t;

	class Value {
	public:
		inline Value(kind_t kind = NONE, unsigned long value = 0, int stride = 0, int strideConf = 0): _kind(kind), _value(value), _stride(stride), _strideConf(strideConf) { }
		inline Value(const Value& val): _kind(val._kind), _value(val._value), _stride(val._stride), _strideConf(val._strideConf) { }
		inline Value& operator=(const Value& val)
			{ _kind = val._kind; _value = val._value; _stride = val._stride; _strideConf = val._strideConf; return *this; }

		inline bool operator==(const Value& val) const { return _kind == val._kind && _value == val._value && _stride == val._stride && _strideConf == val._strideConf; }
		inline bool operator!=(const Value& val) const { return ! operator==(val); }
		inline bool operator<(const Value& val) const
		{
			if(_kind < val._kind)
				return true;
			if(_kind == val._kind && abs(_stride) < abs(val._stride))
				return true;
			if(_kind == val._kind && abs(_stride) == abs(val._stride) && _strideConf < val._strideConf)
				return true;
			if(_kind == val._kind && abs(_stride) == abs(val._stride) && _strideConf == val._strideConf && _value < val._value)
				return true;

			return false;
		}

		inline kind_t kind(void) const { return _kind; }
		inline unsigned long value(void) const { return _value; }
		inline int stride(void) const { return _stride; }
		inline int strideConf(void) const { return _strideConf; }

		void add(const Value& val) {
			elm::cout << "ADD!\n";
			if(_kind == CST && val._kind == CST)
			{
				unsigned int newVal = _value + val._value;
				int s = newVal - _value;

				// Are we adding zero (aka a set)?
				if(_value != 0 && val._value != 0)
				{
					if(_stride == 0 || _stride == s)
					{
						_stride = s;
						_strideConf++;
					}
					else
					{
						_stride = 999;
						_strideConf = -1;
					}
				}

				_value = newVal;
			}
			else if(_kind == NONE && val._kind == NONE)
				set(NONE, 0, 0, 0);
			else
				set(ALL, 0, 0, 0);
		}

		void sub(const Value& val) {
			if(_kind == CST && val._kind == CST)
				_value -= val._value;
			else if(_kind == NONE && val._kind == NONE)
				set(NONE, 0, 0, 0);
			else
				set(ALL, 0, 0, 0);
		}

		void print(io::Output& out) const {
			switch(_kind) {
			case NONE: out << '_'; break;
			case REG: out << 'r' << _value; break;
			case CST: out << "k(" << io::hex(_value) << ')' << " " << _stride << "(" << _strideConf << ")"; break;
			case ALL: out << 'T'; break;
			}
		}

		void shl(const Value& val) {
			if(_kind == CST && val._kind == CST)
				_value <<= val._value;
			else if(_kind == NONE && val._kind == NONE)
				set(NONE, 0, 0, 0);
			else
				set(ALL, 0, 0, 0);
		}

		void shr(const Value& val) {
			if(_kind == CST && val._kind == CST)
				_value >>= val._value;
			else if(_kind == NONE && val._kind == NONE)
				set(NONE, 0, 0, 0);
			else
				set(ALL, 0, 0, 0);
		}

		void asr(const Value& val)
		{
			if(_kind == CST && val._kind == CST)
				_value = (uint32_t)((int32_t)_value >> val._value);
			else if(_kind == NONE && val._kind == NONE)
				set(NONE, 0, 0, 0);
			else
				set(ALL, 0, 0, 0);
		}

		void join(const Value& val) {
			if(_kind != val._kind)
			{
				if(_kind != ALL && val._kind != ALL
					&& (_kind == NONE || val._kind == NONE))
					set(NONE, 0, 0, 0);
				else
					set(ALL, 0, 0, 0);
			}
			else if(_value != val._value) // Both the same kind...
			{
				// Figure out the stride
				int s = val._value - _value;

				if(_stride == 0 || _stride == s)
				{
					_stride = s;
					_strideConf++;
					_value = val._value;
				}
				else
					set(ALL, 0, 0, 0);
			}


			/*if(!(_kind == val._kind && _value == val._value)) {
				if(_kind != ALL && val._kind != ALL
				&& (_kind == NONE || val._kind == NONE))
					set(NONE, 0, 0, 0);
				else 
					set(ALL, 0, 0, 0);
			}*/
		}

		static const Value none, all;

	private:
		inline void set(kind_t kind, unsigned long value, int stride, int strideConf) { _kind = kind; _value = value; _stride = stride; _strideConf = strideConf; }
		kind_t _kind;
		t::uint32 _value;
		int _stride;
		int _strideConf;
	};
	inline io::Output& operator<<(io::Output& out, const Value& v) { v.print(out); return out; }
	const Value Value::none(NONE), Value::all(ALL);

	class State {
	public:

		class Node {
		public:
			friend class State;
			inline Node(void): next(0), addr(Value::none) { }
			inline Node(const Value& address, const Value& value)
				: next(0), addr(address), val(value) { }
			inline Node(const Node *node)
				: next(0), addr(node->addr), val(node->val) { }
		private:
			Node *next;
			Value addr;
			Value val;
		};

		State(const Value& def = Value::all): first(Value::none, def)
			{ TRACED(cerr << "State(" << def << ")\n"); }
		State(const State& state): first(Value::none, Value::all)
			{ TRACED(cerr << "State("; state.print(cerr); cerr << ")\n"); copy(state); }
		~State(void) { clear(); }

		inline State& operator=(const State& state) { copy(state); return *this; }

		void copy(const State& state) {
			TRACED(cerr << "copy("; print(cerr); cerr << ", "; state.print(cerr); cerr << ") = ");
			clear();
			first = state.first;
			for(Node *prev = &first, *cur = state.first.next; cur; cur = cur->next) {
				prev->next = new Node(cur);
				prev = prev->next;
			}
			TRACED(print(cerr); cerr << io::endl);
		}

		void clear(void) {
			for(Node *cur = first.next, *next; cur; cur = next) {
				next = cur->next;
				delete cur;
			}
			first.next = 0;
		}

		void set(const Value& addr, const Value& val) {
			TRACED(cerr << "set("; print(cerr); cerr << ", " << addr << ", " << val << ") = ");
			Node *prev, *cur, *next;
			if(first.val == Value::none) {
				TRACED(print(cerr); cerr << io::endl);
				return;
			}

			// consum all memory references
			if(addr.kind() == ALL) {
				for(prev = &first, cur = first.next; cur && cur->addr.kind() <= REG; prev = cur, cur = cur->next) ;
				while(cur) {
					next = cur->next;
					delete cur;
					cur = next;
				}
				prev->next = 0;
			}

			// find a value
			else {
				for(prev = &first, cur = first.next; cur && cur->addr < addr; prev = cur, cur = cur->next);
				if(cur && cur->addr == addr) {
					if(val.kind() != ALL)
						cur->val = val;
					else {
						prev->next = cur->next;
						delete cur;
					}
				}
				else if(val.kind() != ALL) {
					next = new Node(addr, val);
					prev->next = next;
					prev->next->next = cur;
				}
			}

			TRACED(print(cerr); cerr << io::endl);
		}

		bool equals(const State& state) const {
			if(first.val.kind() != state.first.val.kind())
				return false;
			Node *cur = first.next, *cur2 = state.first.next;
			while(cur && cur2) {
				if(cur->addr != cur2->addr)
					return false;
				if(cur->val != cur->val)
					return false;
				cur = cur->next;
				cur2 = cur2->next;
			}
			return cur == cur2;
		}

		void join(const State& state) {
			TRACED(cerr << "join(\n\t"; print(cerr); cerr << ",\n\t";  state.print(cerr); cerr << "\n\t) = ");

			// test none states
			if(state.first.val == Value::none)
				return;
			if(first.val == Value::none) {
				copy(state);
				TRACED(print(cerr); cerr << io::endl;);
				return;
			}

			Node *prev = &first, *cur = first.next, *cur2 = state.first.next, *next;
			while(cur && cur2) {
				// addr1 < addr2 -> remove cur1
				if(cur->addr < cur2->addr) {
					prev->next = cur->next;
					delete cur;
					cur = prev->next;
				}

				// equality ? remove if join result in all
				else if(cur->addr == cur2->addr) {
					cur->val.join(cur2->val);
					if(cur->val.kind() == ALL) {
						prev->next = cur->next;
						delete cur;
						cur = prev->next;
					}
					else {
						prev = cur;
						cur = cur->next;
						cur2 = cur2->next;
					}
				}

				// addr1 > addr2 => remove cur2
				else
					cur2 = cur2->next;
			}

			// remove tail
			prev->next = 0;
			while(cur) {
				next = cur->next;
				delete cur;
				cur = next;
			}
			TRACED(print(cerr); cerr << io::endl;);
		}

		void print(io::Output& out) const {
			if(first.val == Value::none)
				out << '_';
			else {
				bool f =  true;
				out << "{ ";
				for(Node *cur = first.next; cur; cur = cur->next) {
					if(f)
						f = false;
					else
						out << ", ";
					switch(cur->addr.kind()) {
					case NONE:
						out << "_";
						break;
					case REG:
						out << 'r' << cur->addr.value();
						break;
					case CST:
						out << Address(cur->addr.value());
						break;
					case ALL:
						out << 'T';
						break;
					}
					out << " = " << cur->val;
				}
				out << " }";
			}
		}

		Value fromImage(const Address& addr, Process *proc, int size) const {
			switch(size) {
			case 1: { t::uint8 v; proc->get(addr, v); return Value(CST, v); }
			case 2: { t::uint16 v; proc->get(addr, v); return Value(CST, v); }
			case 4: { t::uint32 v; proc->get(addr, v); return Value(CST, v); }
			}
			return first.val;
		}

		Value get(const Value& addr, Process *proc, int size) const {
			Node * cur;

			for(cur = first.next; cur && cur->addr < addr; cur = cur->next) ;
			if(cur && cur->addr == addr)
				return cur->val;
			if(addr.kind() == CST)
				for(Process::FileIter file(proc); file; file++)
					for(File::SegIter seg(file); seg; seg++)
						if(seg->contains(addr.value()))
							return fromImage(addr.value(), proc, size);
			return first.val;
		}

		static const State EMPTY, FULL;

	private:
		Node first;
	};
	const State State::EMPTY(Value::none), State::FULL(Value::all);
	io::Output& operator<<(io::Output& out, const State& state) { state.print(out); return out; }


} // otawa::microblaze::loop

class LoopStrideProblem {
public:
	typedef loop::State Domain;

	typedef LoopStrideProblem Problem;
	Problem& getProb(void) { return *this; }

	LoopStrideProblem(Process *_proc): proc(_proc) {
		//stack::Value v(stack::SP, 0);
		//set(_init, 1, v);
	}

	void initialize(const hard::Register *reg, const Address& address) {
		loop::Value v;
		if(address.isNull())
			v = loop::Value(loop::CST, 0);
		else
			v = loop::Value(loop::CST, address.offset());
		set(_init, reg->platformNumber(), v);
	}

	inline const Domain& bottom(void) const { return loop::State::EMPTY; }
	inline const Domain& entry(void) const { TRACED(cerr << "entry() = " << _init << io::endl); return _init; }
	inline void lub(Domain &a, const Domain &b) const { a.join(b); }
	inline void assign(Domain &a, const Domain &b) const { a = b; }
	inline bool equals(const Domain &a, const Domain &b) const { return a.equals(b); }
	inline void enterContext(Domain &dom, BasicBlock *header, util::hai_context_t ctx) { }
	inline void leaveContext(Domain &dom, BasicBlock *header, util::hai_context_t ctx) { }

	loop::Value get(const loop::State& state, int i) {
		if(i <  0)
			return tmp[-i];
		else {
			loop::Value addr(loop::REG, i);
			return state.get(addr, proc, 0);
		}
	}

	const void set(loop::State& state, int i, const loop::Value& v) {
		if(i < 0)
			tmp[-i] = v;
		else {
			loop::Value addr(loop::REG, i);
			return state.set(addr, v);
		}
	}

	void addAddress(Inst *inst, bool store, const loop::Value& v) {
		switch(v.kind()) {
		case loop::ALL:
		case loop::NONE:
			addrs.add(new AccessedAddress(inst, store));
			break;
		case loop::CST:
			addrs.add(new AbsAddress(inst, store, v.value()));
			break;
		case loop::REG:
			ASSERT(false);
			break;
		}
	}

	void update(Domain& out, const Domain& in, BasicBlock* bb) {
		int pc;
		out.copy(in);
		Domain *state;
		TRACEU(cerr << "update(BB" << bb->number() << ", " << in << ")\n");
		for(BasicBlock::InstIterator inst(bb); inst; inst++) {
			TRACEU(cerr << '\t' << inst->address() << ": "; inst->dump(cerr); cerr << io::endl);

			// get instructions
			b.clear();
			inst->semInsts(b);
			pc = 0;
			state = &out;

			// perform interpretation
			while(true) {

				// interpret current
				while(pc < b.length()) {
					sem::inst& i = b[pc];
					TRACES(cerr << "\t\t" << i << io::endl);
					switch(i.op) {
					case sem::BRANCH:
					case sem::TRAP:
					case sem::CONT:
						pc = b.length();
						TRACES(cerr << "\t\tcut\n");
						break;
					case sem::IF:
						//elm::cout << "PUSH!!!\n";
						todo.push(pair(pc + i.b() + 1, new Domain(*state)));
						break;
					case sem::NOP: break;
					case sem::LOAD: {
							loop::Value addr = get(*state, i.a());
							addAddress(inst, false, addr);
							set(*state, i.d(), state->get(addr, proc, i.b()));
						} break;
					case sem::STORE: {
							loop::Value addr = get(*state, i.a());
							addAddress(inst, true, addr);
							state->set(addr, get(*state, i.d()));
						} break;
					case sem::SETP:
					case sem::CMP:
					case sem::CMPU:
					case sem::SCRATCH:
						set(*state, i.d(), loop::Value::all);
						break;
					case sem::SET: {
							loop::Value v = get(*state, i.a());
							set(*state, i.d(), v);
						} break;
					case sem::SETI: {
							loop::Value v(loop::CST, i.cst());
							set(*state, i.d(), v);
						} break;
					case sem::ADD: {
							loop::Value v = get(*state, i.a());
							v.add(get(*state, i.b()));
							set(*state, i.d(), v);
						} break;
					case sem::SUB: {
							loop::Value v = get(*state, i.a());
							v.sub(get(*state, i.b()));
							set(*state, i.d(), v);
						} break;
					case sem::SHL: {
							loop::Value v = get(*state, i.a());
							v.shl(get(*state, i.b()));
							set(*state, i.d(), v);
						} break;
					case sem::ASR: {
							loop::Value v = get(*state, i.a());
							v.asr(get(*state, i.b()));
							set(*state, i.d(), v);
						} break;
					case sem::SHR: {
							loop::Value v = get(*state, i.a());
							v.shr(get(*state, i.b()));
							set(*state, i.d(), v);
						} break;
					}
					pc++;
				}

				// pop next
				if(state != &out) {
					out.join(*state);
					delete state;
				}
				if(!todo)
					break;
				else {
					Pair<int, Domain *> p = todo.pop();
					pc = p.fst;
					state = p.snd;
				}
			}
			TRACEI(cerr << "\t -> " << out << io::endl);
		}
		TRACEU(cerr << "\tout = " << out << io::endl);

		// record the addresses
		if(addrs) {
			//AccessedAddresses *aa = ADDRESSES(bb);
			//if(!aa) {
			//	aa = new AccessedAddresses();
			//	ADDRESSES(bb) = aa;
			//}
			//aa->set(addrs);
			addrs.clear();
		}
	}

private:
	loop::Value tmp[16];
	loop::State _init;
	sem::Block b;
	genstruct::Vector<Pair<int, Domain *> > todo;
	genstruct::Vector<AccessedAddress *> addrs;
	Process *proc;
};

LoopStride::LoopStride(p::declare& r) : Processor(r) { }

void LoopStride::processWorkSpace(WorkSpace* ws)
{
	typedef DefaultListener<LoopStrideProblem> LoopStrideListener;
	typedef DefaultFixPoint<LoopStrideListener> LoopStrideFP;
	typedef HalfAbsInt<LoopStrideFP> LoopStrideAI;

	const CFGCollection* cfgs = INVOLVED_CFGS(ws);

	ASSERT(cfgs);
	CFG* cfg = cfgs->get(0);

	for(CFGCollection::Iterator cfg(cfgs); cfg; cfg++)
	{
		elm::cout << "Using CFG " << cfg->label() << elm::io::endl;

		for(CFG::BBIterator bb(cfg); bb; bb++)
		{
			int max_iter = MAX_ITERATION(bb);
			elm::cout << "\tBB " << bb->number();
			if(LOOP_HEADER(bb))
				elm::cout << " HEADER MAX ITER " << max_iter << elm::io::endl;
			else if(ENCLOSING_LOOP_HEADER(bb))
				elm::cout << " ENCLOSED MAX ITER " << MAX_ITERATION(ENCLOSING_LOOP_HEADER(bb)) << elm::io::endl;
			else
				elm::cout << " NO LOOP!" << elm::io::endl;
		}
	}

	LoopStrideProblem prob(ws->process());
	const hard::Register *sp = ws->process()->platform()->getSP();
	prob.initialize(sp, Address::null);

	LoopStrideListener list(ws, prob);
	LoopStrideFP fp(list);
	LoopStrideAI sai(fp, *ws);
	sai.solve(cfg);
}

p::declare LoopStride::reg = p::init("otawa::microblaze::LoopStride", Version(1, 0, 0))
                                 .maker<LoopStride>()
                                 .require(otawa::COLLECTED_CFG_FEATURE)
                                 .require(otawa::ipet::FLOW_FACTS_FEATURE);

} //otawa::microblaze
} //otawa
