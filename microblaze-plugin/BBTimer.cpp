#include <otawa/parexegraph/GraphBBTime.h>
#include <otawa/parexegraph/ParExeGraph.h>
#include <otawa/cfg/features.h>
#include "../otawa-microblaze/microblaze.h"

#include <otawa/dcache/features.h>

#include "BBTimer.h"

namespace otawa { namespace microblaze { 

using namespace otawa;

#define TIME_TEST 20000

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
	createResources();

    createNodes();
    findDataDependencies();

    addEdgesForPipelineOrder();
    addEdgesForFetch();
    addEdgesForProgramOrder();

    addEdgesForMemoryOrder();
    addEdgesForDataDependencies();
  
    addEdgesForQueues();
    findContendingNodes();

	// Fixup and add memory latencies
	for(ParExeGraph::InstIterator inst(this); inst; inst++)
	{
		if(inst->inst()->kind() & Inst::IS_MEM)
		{
			if(inst->inst()->kind() & Inst::IS_LOAD)
			{
				findMemoryStage(inst)->setDefaultLatency(1);
			}
			else
			{
				// Store instruction...
				findMemoryStage(inst)->setDefaultLatency(TIME_TEST);
			}
		}
	}

	// Add edges between a used mem stage and the fetch stage
	// This is because we're currently assuming only a single
	// read/write port on the processor, which is a shared
	// resource.
	/*for(ParExeGraph::InstIterator inst(this); inst; inst++)
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

				new ParExeEdge(source_node, dest_node, ParExeEdge::SOLID, 0);

				curInst++;
				nextInst++;
				memIndex--;
			}
		}
	}*/
}

void ExeGraph::addEdgesForMemoryOrder(void)
{
	// Adapted slightly from the default implementation.
	static string msg = "MB memory order";

	ParExePipeline* pipe = _microprocessor->pipeline();
	ParExeStage* memStage = NULL;

	for(ParExePipeline::StageIterator s(pipe); s; s++)
	{
		if(s->name() == "MEM")
		{
			memStage = s;
			break;
		}
	}

	ASSERT(memStage)

	ParExeNode* prev_access = NULL;

	// There's no FUs in the mem stage. Just link between the stages
	for(int i = 0; i < memStage->numNodes(); i++)
	{
		ParExeNode* memNode = memStage->node(i);

		if(memNode->inst()->inst()->isLoad() || memNode->inst()->inst()->isStore())
		{
			if(prev_access)
			{
				// Connect the two nodes up.
				new ParExeEdge(prev_access, memNode, ParExeEdge::SOLID, 0, msg);
				prev_access = memNode;
			}
			else
				prev_access = memNode;
		}
	}
}

void ExeGraph::addEdgesForFetch(void)
{
	ParExeGraph::addEdgesForFetch();

	ParExeStage *fetch_stage = _microprocessor->fetchStage();
	for(int i=0 ; i<fetch_stage->numNodes()-1 ; i++) {
		ParExeNode *node = fetch_stage->node(i);
		ParExeNode *next = fetch_stage->node(i+1);
		new ParExeEdge(node, next, ParExeEdge::SOLID);
    }
}

// ============================================
// BBTimer stuff
// ============================================
BBTimer::BBTimer(void) : GraphBBTime<ExeGraph>(reg) { }

void BBTimer::processWorkSpace(WorkSpace* ws)
{
	_ws = ws;
	elm::cout << "Setting persistence analysis on..." << elm::io::endl;
	//dcache::DATA_FIRSTMISS_LEVEL(_props) = dcache::DFML_MULTI;
	ws->require(dcache::MAY_ACS_FEATURE);
	ws->require(dcache::CATEGORY_FEATURE, _props);

	GraphBBTime<ExeGraph>::processWorkSpace(ws);
}

void BBTimer::configure(const PropList& props)
{
	_props = props;
	GraphBBTime<ExeGraph>::configure(props);
}

// Adapted from the standard version...
// This is copied out just so that we can attempt to extract other
// path contexts at the same time.
void BBTimer::analyzePathContext(PathContext*ctxt, int context_index)
{
	int case_index = 0;
	BasicBlock * bb = ctxt->mainBlock();
	Edge *edge = ctxt->edge();


	ParExeSequence *sequence = buildSequence(ctxt);
	ExeGraph *execution_graph = new ExeGraph(_ws,_microprocessor, sequence, _props);
	execution_graph->build();

	// no cache
	if(!_do_consider_icache)
		for(ExeGraph::InstIterator inst(execution_graph); inst; inst++)
			inst->fetchNode()->setLatency(memoryLatency(inst->inst()->address()));

	// compute reference cost
	int reference_cost = execution_graph->analyze();
	if(isVerbose())
		log << "\t\treference cost = " << reference_cost << "\n\n";
	if (_do_output_graphs){
		outputGraph(execution_graph, bb->number(), context_index, case_index++,
			_ << "reference cost = " << reference_cost);
	}

	// case of a cache
	if (_do_consider_icache){

		// verbosity
		if(isVerbose()) {
			LBlockManager lbm;
			for (ParExeSequence::InstIterator inst(sequence) ; inst ; inst++)  {
				cache::category_t cat = lbm.next(inst->basicBlock(), inst->inst());
				if (cat != cache::INVALID_CATEGORY){
					log << "\t\t\tcategory of I" << inst->index() << " is ";
					switch(cat){
					case cache::ALWAYS_HIT:
						log << "ALWAYS_HIT\n";
						break;
					case cache::ALWAYS_MISS:
						log << "ALWAYS_MISS\n";
						break;
					case cache::FIRST_MISS:
						log << "FIRST_MISS (with header b" << lbm.header()->number() << ")\n";
						break;
					case cache::NOT_CLASSIFIED:
						log << "NOT_CLASSIFIED\n";
						break;
					default:
						log << "unknown !!!\n";
						break;
					}
				}
			}
		}

		// set constant latencies (ALWAYS_MISS in the cache)
		TimingContext default_timing_context;
		computeDefaultTimingContextForICache(&default_timing_context, sequence);
		//computeDefaultTimingContextForDCache(&default_timing_context, sequence);
		int default_icache_cost = reference_cost;
		if (!default_timing_context.isEmpty()){
			if(isVerbose()) {
				log << "\t\t\t\tdefault timing context: misses for";
				for (TimingContext::NodeLatencyIterator nl(default_timing_context) ; nl ; nl++){
					log << "I" << nl->node()->inst()->index() << ", ";
				}
				log << " - ";
			}
			execution_graph->setDefaultLatencies(&default_timing_context);
			default_icache_cost = execution_graph->analyze();
			if (_do_output_graphs) {
				StringBuffer buf;
				buf << "cost with AM = " << default_icache_cost << "\\lAM (";
				for (TimingContext::NodeLatencyIterator nl(default_timing_context) ; nl ; nl++)
					buf << "I" << nl->node()->inst()->index() << ", ";
				buf << ")";
				outputGraph(execution_graph, bb->number(), context_index, case_index++, buf.toString());
			}
			if(isVerbose())
				log << "cost = " << default_icache_cost << " (only accounting for fixed latencies)\n\n";
			if (default_icache_cost > reference_cost)
				reference_cost = default_icache_cost;
		}
    
		// consider variable latencies (FIRST_MISS, NOT_CLASSIFIED)
		elm::genstruct::SLList<TimingContext *> NC_timing_context_list;
		elm::genstruct::SLList<TimingContext *> FM_timing_context_list;
		buildNCTimingContextListForICache(&NC_timing_context_list, sequence);	
		buildFMTimingContextListForICache(&FM_timing_context_list, sequence);

		// TODO: Is it safe to use the same lists here?
		// TODO: Gonna need to check analyzeTimingContext too...
		//buildNCTimingContextListForDCache(&NC_timing_context_list, sequence);	

		// Overridden I-cache one...
		// Consider renaming the function call...
		//buildFMTimingContextListForDCache(&FM_timing_context_list, sequence);

		int index = 0;
		CachePenalty *cache_penalty = new CachePenalty();

		bool first = true;
		if (!FM_timing_context_list.isEmpty()){
			for (elm::genstruct::SLList<TimingContext *>::Iterator FM_tctxt(FM_timing_context_list) ; FM_tctxt ; FM_tctxt++){
				if (first) {
					cache_penalty->setHeader(0, FM_tctxt->header(0));
					cache_penalty->setHeader(1, FM_tctxt->header(1));
					first = false;
				}
				if (!NC_timing_context_list.isEmpty()){
					for (elm::genstruct::SLList<TimingContext *>::Iterator NC_tctxt(NC_timing_context_list) ; NC_tctxt ; NC_tctxt++){
						int NC_cost = analyzeTimingContext(execution_graph, NC_tctxt.item(), NULL);
						if (NC_cost > reference_cost)
							reference_cost = NC_cost;
						int cost = analyzeTimingContext(execution_graph, NC_tctxt.item(), FM_tctxt.item());
						if(isVerbose()) {
							log << "\n\t\tcontext " << index << ": ";
							for (TimingContext::NodeLatencyIterator nl(*(NC_tctxt.item())) ; nl ; nl++){
								log << "I" << (nl.item())->node()->inst()->index() << ",";
							}
							for (TimingContext::NodeLatencyIterator nl(*(FM_tctxt.item())) ; nl ; nl++){
								log << "I" << (nl.item())->node()->inst()->index() << ",";
							}
							log << "  - ";
							log << "cost=" << cost;
							log << "  - NC_cost=" << NC_cost << "\n";
						}
		
						int penalty = cost - reference_cost; 
						// default_icache_cost is when all NCs hit
						if ((FM_tctxt->type() == CachePenalty::x_HIT) && (penalty < 0))
							penalty = 0;  // penalty = max [ hit-hit, miss-hit ]
						if (penalty > cache_penalty->penalty(FM_tctxt->type()))
							cache_penalty->setPenalty(FM_tctxt->type(), penalty);
						//cache_penalty->dump(elm::cout);
						//elm::cout << "\n";
						//elm::cout << " (penalty = " << penalty << " - p[" << FM_tctxt->type() << "] = " << cache_penalty->penalty(FM_tctxt->type()) << ")\n";
						if (_do_output_graphs) {
							StringBuffer buf;
							buf << "FM-NC cost = " << cost << "\\lfirst(";
								for (TimingContext::NodeLatencyIterator nl(*(FM_tctxt.item())) ; nl ; nl++)
									buf << "I" << (nl.item())->node()->inst()->index() << ",";
							buf << "),\\lNC(";
								for (TimingContext::NodeLatencyIterator nl(*(NC_tctxt.item())) ; nl ; nl++)
									buf << "I" << (nl.item())->node()->inst()->index() << ",";
							buf << ")";
							outputGraph(execution_graph, bb->number(), context_index, case_index++, buf.toString());
						}
					} 
				}
				else { // no NC context
					int cost = analyzeTimingContext(execution_graph, NULL, FM_tctxt.item());
					if(isVerbose()) {
						log << "\t\tcontext " << index << ": ";
						for (TimingContext::NodeLatencyIterator nl(*(FM_tctxt.item())) ; nl ; nl++){
							log << "I" << (nl.item())->node()->inst()->index() << ",";
						}
						log << "  - ";
						log << "cost=" << cost << "\n";
					}
					int penalty = cost - reference_cost;
					if ((FM_tctxt->type() == CachePenalty::x_HIT) && (penalty < 0))
						penalty = 0;  // penalty = max [ hit-hit, miss-hit ]
					if (penalty > cache_penalty->penalty(FM_tctxt->type()))
						cache_penalty->setPenalty(FM_tctxt->type(), penalty);
					/* cache_penalty->dump(elm::cout); */
/* 							elm::cout << "\n"; */
/* 						elm::cout << " (penalty = " << penalty << " - p[" << FM_tctxt->type() << "] = " << cache_penalty->penalty(FM_tctxt->type()) << ")\n"; */
					if (_do_output_graphs){
						StringBuffer buf;
						buf << "NC cost = " << cost << "\\lNC (";
							for (TimingContext::NodeLatencyIterator nl(*(FM_tctxt.item())) ; nl ; nl++)
								buf << "I" << (nl.item())->node()->inst()->index() << ",";
						buf << ")";
						outputGraph(execution_graph, bb->number(), context_index, case_index++, buf.toString());
					}
				}
			}
    
		}
		else { // no FM context
			for (elm::genstruct::SLList<TimingContext *>::Iterator NC_tctxt(NC_timing_context_list) ; NC_tctxt ; NC_tctxt++){
				int NC_cost = analyzeTimingContext(execution_graph, NC_tctxt.item(), NULL);
				if(isVerbose()) {
					log << "\t\tcontext " << index << ": ";
					for (TimingContext::NodeLatencyIterator nl(*(NC_tctxt.item())) ; nl ; nl++){
						log << "I" << (nl.item())->node()->inst()->index() << ",";
					}
					log << " - ";
					log << "cost=" << NC_cost << "\n";
				}
				if (NC_cost > reference_cost)
					reference_cost = NC_cost;
				if (_do_output_graphs){
					StringBuffer buf;
					buf << "NC cost = " << NC_cost << "\\l NC (";
					for (TimingContext::NodeLatencyIterator nl(*(NC_tctxt.item())) ; nl ; nl++)
						buf << "I" << (nl.item())->node()->inst()->index() << ",";
					buf << ")";
					outputGraph(execution_graph, bb->number(), context_index, case_index++, buf.toString());
				}
			}
		}

		// set the cache penalty
		if (cache_penalty->header(0)){
			ICACHE_PENALTY(bb) = cache_penalty;
			if (edge)
				ICACHE_PENALTY(edge) = cache_penalty;
			if(isVerbose()) {
				log << "\t\tcache penalty: ";
				cache_penalty->dump(log);
				log << "\n";
			}
		}
		
	}

	// record the times
	if(isVerbose())
		log << "\t\tReference cost: " << reference_cost << "\n";
	if (otawa::ipet::TIME(bb) < reference_cost)
		otawa::ipet::TIME(bb) = reference_cost;
	if (edge){
		if (otawa::ipet::TIME(edge) < reference_cost){
			otawa::ipet::TIME(edge) = reference_cost;
		}
	}

	// cleanup
	delete execution_graph;  
}

void BBTimer::computeDefaultTimingContextForDCache(TimingContext *dtctxt, ParExeSequence *seq){
	// We need to find all the LD/ST instructions in the set...
	for(ParExeSequence::InstIterator inst(seq); inst; inst++)
	{
		// Get the BB
		BasicBlock* bb = inst->basicBlock();
		Pair<int, dcache::BlockAccess*> ab = dcache::DATA_BLOCKS(bb);

		for(int ba = 0; ba < ab.fst; ba++)
		{
			if(ab.snd[ba].instruction()->address() == inst->inst()->address())
			{
				elm::cout << "GOT INST " << inst->inst() << elm::io::endl;
				// Got it!
				dcache::BlockAccess& access = ab.snd[ba];

				// Get the category
				elm::cout << dcache::CATEGORY(access) << elm::io::endl;
				if(dcache::CATEGORY(access) == cache::ALWAYS_MISS)
				{
					NodeLatency* nl = new NodeLatency(findMemoryStage(inst), TIME_TEST);
					dtctxt->addNodeLatency(nl);
				}
				break;
			}
		}
	}


/*
	LBlockManager lbm;
	for (ParExeSequence::InstIterator inst(seq) ; inst ; inst++)  {
		cache::category_t cat = lbm.next(inst->basicBlock(), inst->inst());
		if (cat != cache::INVALID_CATEGORY){
			if (cat == cache::ALWAYS_MISS) {
				NodeLatency * nl = new NodeLatency(inst->fetchNode(), cacheMissPenalty(inst->inst()->address()));
				dtctxt->addNodeLatency(nl);
			}
		}
	}
*/
}

void BBTimer::buildNCTimingContextListForDCache(elm::genstruct::SLList<TimingContext *> *list, ParExeSequence *seq) 
{
	elm::genstruct::SLList<TimingContext *> * to_add = new elm::genstruct::SLList<TimingContext *>();

	// process NOT_CLASSIFIED lblocks
	for(ParExeSequence::InstIterator inst(seq); inst; inst++)
	{
		// Get the BB
		BasicBlock* bb = inst->basicBlock();
		Pair<int, dcache::BlockAccess*> ab = dcache::DATA_BLOCKS(bb);

		for(int ba = 0; ba < ab.fst; ba++)
		{
			if(ab.snd[ba].instruction()->address() == inst->inst()->address())
			{
				elm::cout << "GOT INST " << inst->inst() << elm::io::endl;
				// Got it!
				dcache::BlockAccess& access = ab.snd[ba];
				category_t cat = dcache::CATEGORY(access);
				if (cat != otawa::INVALID_CATEGORY){
					if (cat == cache::NOT_CLASSIFIED){
						if (list->isEmpty()){
							TimingContext *tctxt = new TimingContext();
							NodeLatency * nl = new NodeLatency(findMemoryStage(inst), TIME_TEST);
							tctxt->addNodeLatency(nl);
							list->addLast(tctxt);
						}
						else {
							for (elm::genstruct::SLList<TimingContext *>::Iterator tctxt(*list) ; tctxt ; tctxt++){
								TimingContext *new_tctxt = new TimingContext(tctxt.item());
								NodeLatency * nl = new NodeLatency(findMemoryStage(inst), TIME_TEST);
								new_tctxt->addNodeLatency(nl);
								to_add->addLast(new_tctxt);
							}
							for (elm::genstruct::SLList<TimingContext *>::Iterator tctxt(*to_add) ; tctxt ; tctxt++){
								list->addLast(tctxt.item());
							}
							to_add->clear();
							TimingContext *new_tctxt = new TimingContext();
							NodeLatency * nl = new NodeLatency(findMemoryStage(inst), TIME_TEST);
							new_tctxt->addNodeLatency(nl);
							list->addLast(new_tctxt);
						}
					}
				}
			}
		}
	}

	delete to_add;
}

// We don't currently do persistence analysis anyway, so don't bother rolling data
// cache analysis into this yet.
// This is the default implementation, consider just removing it.
void BBTimer::buildFMTimingContextListForICache(elm::genstruct::SLList<TimingContext *> *list, ParExeSequence *seq) 
{
	BasicBlock *header0 = NULL;
	BasicBlock *header1 = NULL;
	int num_headers = 0;

	// process FIRST_MISS lblocks

	// find FIRST_MISS headers
	LBlockManager lbm;
	for (ParExeSequence::InstIterator inst(seq) ; inst ; inst++)  {
		category_t cat = lbm.next(inst->basicBlock(), inst->inst());
		if (cat != otawa::INVALID_CATEGORY){
			if (cat == cache::FIRST_MISS){
				BasicBlock *header = lbm.header();
				//		elm::cout << "found header b" << header->number() << "\n";
				if (header0 == NULL){
					header0 = header;
					//	    elm::cout << "\tsaved in header0\n";
					num_headers++;
				}
				else {
					if (header0 != header){
						if (header1 == NULL){
							if (Dominance::dominates(header, header0)){
								header1 = header0;
								header0 = header;
								//		elm::cout << "\tsaved in header0 (header1 takes header0)\n";
							}
							else {
								header1 = header;
								//		elm::cout << "\tsaved in header1\n";
							}
							num_headers++;
						}
						else { 
							if (header1 != header) {
								// third header: is not expected to be found - could be implemented by ignoring the first headers in the sequence
								ASSERTP(0, "this sequence has more than 2 headers for cache categories: this is not supported so far\n");
							}
							// else
							//	elm::cout << "\talready found in header1\n";
						}	  
					} // header0 != header
					// else {
					//	elm::cout << "\talready found in header0\n";
					// }
				}
			}
		}
	}
	// create timing contexts
	if (num_headers){
		if (num_headers == 1){
			TimingContext *tctxt_first = new TimingContext(header0);
			tctxt_first->setType(CachePenalty::MISS); 
			list->addLast(tctxt_first);

			LBlockManager lbm;
			for (ParExeSequence::InstIterator inst(seq) ; inst ; inst++)  {
				cache::category_t cat = lbm.next(inst->basicBlock(), inst->inst());
				if (cat != cache::INVALID_CATEGORY){
					if (cat == cache::FIRST_MISS){ // must be with header0
						NodeLatency * nl = new NodeLatency(inst->fetchNode(), cacheMissPenalty(inst->inst()->address()));
						tctxt_first->addNodeLatency(nl);
					}
				}
			}
			/* 	    elm::cout << "One header: context is: \n"; */
			/* 	    for (TimingContext::NodeLatencyIterator nl(*tctxt_first) ; nl ; nl++){ */
			/* 		elm::cout << "\t\t\t" << (nl.item())->node()->name() << " : lat=" << (nl.item())->latency() << "\n"; */
			/* 	    }	 */
		}
		else { // num_headers == 2
			TimingContext *tctxt_first_first = new TimingContext(header0, header1);
			tctxt_first_first->setType(CachePenalty::MISS_MISS);
			list->addLast(tctxt_first_first);
			TimingContext *tctxt_others_first = new TimingContext(header0, header1);
			tctxt_others_first->setType(CachePenalty::HIT_MISS);
			list->addLast(tctxt_others_first);
			TimingContext *tctxt_first_others = new TimingContext(header0, header1);
			tctxt_first_others->setType(CachePenalty::x_HIT);
			list->addLast(tctxt_first_others);
			LBlockManager lbm;
			for (ParExeSequence::InstIterator inst(seq) ; inst ; inst++)  {
				cache::category_t cat = lbm.next(inst->basicBlock(), inst->inst());
				if (cat != cache::INVALID_CATEGORY){
					if (cat == cache::FIRST_MISS){
						BasicBlock *header = lbm.header();
						NodeLatency * nl = new NodeLatency(inst->fetchNode(), cacheMissPenalty(inst->inst()->address()));
						tctxt_first_first->addNodeLatency(nl);
						nl = new NodeLatency(inst->fetchNode(), cacheMissPenalty(inst->inst()->address()));
						if (header == header0){
							tctxt_first_others->addNodeLatency(nl);
						}
						else {// must be header==header1
							tctxt_others_first->addNodeLatency(nl);
						}
					}
				}
			}
		}
	}
}

p::declare BBTimer::reg = p::init("otawa::microblaze::BBTimer", Version(1, 0, 0))
	.base(GraphBBTime<ParExeGraph>::reg)
	.maker<BBTimer>();

} // otawa::microblaze
} // otawa
