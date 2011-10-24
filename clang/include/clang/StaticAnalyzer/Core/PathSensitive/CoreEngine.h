//==- CoreEngine.h - Path-Sensitive Dataflow Engine ----------------*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a generic engine for intraprocedural, path-sensitive,
//  dataflow analysis via graph reachability.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_GR_COREENGINE
#define LLVM_CLANG_GR_COREENGINE

#include "clang/AST/Expr.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/WorkList.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/BlockCounter.h"
#include "llvm/ADT/OwningPtr.h"

namespace clang {

class ProgramPointTag;
  
namespace ento {

class NodeBuilder;

//===----------------------------------------------------------------------===//
/// CoreEngine - Implements the core logic of the graph-reachability
///   analysis. It traverses the CFG and generates the ExplodedGraph.
///   Program "states" are treated as opaque void pointers.
///   The template class CoreEngine (which subclasses CoreEngine)
///   provides the matching component to the engine that knows the actual types
///   for states.  Note that this engine only dispatches to transfer functions
///   at the statement and block-level.  The analyses themselves must implement
///   any transfer function logic and the sub-expression level (if any).
class CoreEngine {
  friend struct NodeBuilderContext;
  friend class NodeBuilder;
  friend class CommonNodeBuilder;
  friend class GenericNodeBuilderImpl;
  friend class IndirectGotoNodeBuilder;
  friend class SwitchNodeBuilder;
  friend class EndOfFunctionNodeBuilder;
  friend class CallEnterNodeBuilder;
  friend class CallExitNodeBuilder;
  friend class ExprEngine;

public:
  typedef std::vector<std::pair<BlockEdge, const ExplodedNode*> >
            BlocksExhausted;
  
  typedef std::vector<std::pair<const CFGBlock*, const ExplodedNode*> >
            BlocksAborted;

private:

  SubEngine& SubEng;

  /// G - The simulation graph.  Each node is a (location,state) pair.
  llvm::OwningPtr<ExplodedGraph> G;

  /// WList - A set of queued nodes that need to be processed by the
  ///  worklist algorithm.  It is up to the implementation of WList to decide
  ///  the order that nodes are processed.
  WorkList* WList;

  /// BCounterFactory - A factory object for created BlockCounter objects.
  ///   These are used to record for key nodes in the ExplodedGraph the
  ///   number of times different CFGBlocks have been visited along a path.
  BlockCounter::Factory BCounterFactory;

  /// The locations where we stopped doing work because we visited a location
  ///  too many times.
  BlocksExhausted blocksExhausted;
  
  /// The locations where we stopped because the engine aborted analysis,
  /// usually because it could not reason about something.
  BlocksAborted blocksAborted;

  void generateNode(const ProgramPoint &Loc,
                    const ProgramState *State,
                    ExplodedNode *Pred);

  void HandleBlockEdge(const BlockEdge &E, ExplodedNode *Pred);
  void HandleBlockEntrance(const BlockEntrance &E, ExplodedNode *Pred);
  void HandleBlockExit(const CFGBlock *B, ExplodedNode *Pred);
  void HandlePostStmt(const CFGBlock *B, unsigned StmtIdx, ExplodedNode *Pred);

  void HandleBranch(const Stmt *Cond, const Stmt *Term, const CFGBlock *B,
                    ExplodedNode *Pred);
  void HandleCallEnter(const CallEnter &L, const CFGBlock *Block,
                       unsigned Index, ExplodedNode *Pred);
  void HandleCallExit(const CallExit &L, ExplodedNode *Pred);

private:
  CoreEngine(const CoreEngine&); // Do not implement.
  CoreEngine& operator=(const CoreEngine&);

public:
  /// Construct a CoreEngine object to analyze the provided CFG using
  ///  a DFS exploration of the exploded graph.
  CoreEngine(SubEngine& subengine)
    : SubEng(subengine), G(new ExplodedGraph()),
      WList(WorkList::makeBFS()),
      BCounterFactory(G->getAllocator()) {}

  /// Construct a CoreEngine object to analyze the provided CFG and to
  ///  use the provided worklist object to execute the worklist algorithm.
  ///  The CoreEngine object assumes ownership of 'wlist'.
  CoreEngine(WorkList* wlist, SubEngine& subengine)
    : SubEng(subengine), G(new ExplodedGraph()), WList(wlist),
      BCounterFactory(G->getAllocator()) {}

  ~CoreEngine() {
    delete WList;
  }

  /// getGraph - Returns the exploded graph.
  ExplodedGraph& getGraph() { return *G.get(); }

  /// takeGraph - Returns the exploded graph.  Ownership of the graph is
  ///  transferred to the caller.
  ExplodedGraph* takeGraph() { return G.take(); }

  /// ExecuteWorkList - Run the worklist algorithm for a maximum number of
  ///  steps.  Returns true if there is still simulation state on the worklist.
  bool ExecuteWorkList(const LocationContext *L, unsigned Steps,
                       const ProgramState *InitState);
  void ExecuteWorkListWithInitialState(const LocationContext *L,
                                       unsigned Steps,
                                       const ProgramState *InitState, 
                                       ExplodedNodeSet &Dst);

  // Functions for external checking of whether we have unfinished work
  bool wasBlockAborted() const { return !blocksAborted.empty(); }
  bool wasBlocksExhausted() const { return !blocksExhausted.empty(); }
  bool hasWorkRemaining() const { return wasBlocksExhausted() || 
                                         WList->hasWork() || 
                                         wasBlockAborted(); }

  /// Inform the CoreEngine that a basic block was aborted because
  /// it could not be completely analyzed.
  void addAbortedBlock(const ExplodedNode *node, const CFGBlock *block) {
    blocksAborted.push_back(std::make_pair(block, node));
  }
  
  WorkList *getWorkList() const { return WList; }

  BlocksExhausted::const_iterator blocks_exhausted_begin() const {
    return blocksExhausted.begin();
  }
  BlocksExhausted::const_iterator blocks_exhausted_end() const {
    return blocksExhausted.end();
  }
  BlocksAborted::const_iterator blocks_aborted_begin() const {
    return blocksAborted.begin();
  }
  BlocksAborted::const_iterator blocks_aborted_end() const {
    return blocksAborted.end();
  }

  /// Enqueue the results of the node builder onto the work list.
  void enqueue(ExplodedNodeSet &NB);
};

struct NodeBuilderContext {
  CoreEngine &Eng;
  const CFGBlock *Block;
  ExplodedNode *ContextPred;
  NodeBuilderContext(CoreEngine &E, const CFGBlock *B, ExplodedNode *N)
    : Eng(E), Block(B), ContextPred(N) { assert(B); assert(!N->isSink()); }

  /// \brief Return the CFGBlock associated with this builder.
  const CFGBlock *getBlock() const { return Block; }

  /// \brief Returns the number of times the current basic block has been
  /// visited on the exploded graph path.
  unsigned getCurrentBlockCount() const {
    return Eng.WList->getBlockCounter().getNumVisited(
                    ContextPred->getLocationContext()->getCurrentStackFrame(),
                    Block->getBlockID());
  }

};

/// \class NodeBuilder
/// \brief This is the simplest builder which generates nodes in the
/// ExplodedGraph.
///
/// The main benefit of the builder is that it automatically tracks the
/// frontier nodes (or destination set). This is the set of nodes which should
/// be propagated to the next step / builder. They are the nodes which have been
/// added to the builder (either as the input node set or as the newly
/// constructed nodes) but did not have any outgoing transitions added.
class NodeBuilder {
protected:
  const NodeBuilderContext &C;

  /// Specifies if the builder results have been finalized. For example, if it
  /// is set to false, autotransitions are yet to be generated.
  bool Finalized;
  bool HasGeneratedNodes;
  /// \brief The frontier set - a set of nodes which need to be propagated after
  /// the builder dies.
  ExplodedNodeSet &Frontier;

  /// Checkes if the results are ready.
  virtual bool checkResults() {
    if (!Finalized)
      return false;
    return true;
  }

  bool hasNoSinksInFrontier() {
    for (iterator I = Frontier.begin(), E = Frontier.end(); I != E; ++I) {
      if ((*I)->isSink())
        return false;
    }
    return true;
  }

  /// Allow subclasses to finalize results before result_begin() is executed.
  virtual void finalizeResults() {}
  
  ExplodedNode *generateNodeImpl(const ProgramPoint &PP,
                                 const ProgramState *State,
                                 ExplodedNode *Pred,
                                 bool MarkAsSink = false);

public:
  NodeBuilder(ExplodedNode *SrcNode, ExplodedNodeSet &DstSet,
              const NodeBuilderContext &Ctx, bool F = true)
    : C(Ctx), Finalized(F), HasGeneratedNodes(false), Frontier(DstSet) {
    Frontier.Add(SrcNode);
  }

  NodeBuilder(const ExplodedNodeSet &SrcSet, ExplodedNodeSet &DstSet,
              const NodeBuilderContext &Ctx, bool F = true)
    : C(Ctx), Finalized(F), HasGeneratedNodes(false), Frontier(DstSet) {
    Frontier.insert(SrcSet);
    assert(hasNoSinksInFrontier());
  }

  virtual ~NodeBuilder() {}

  /// \brief Generates a node in the ExplodedGraph.
  ///
  /// When a node is marked as sink, the exploration from the node is stopped -
  /// the node becomes the last node on the path.
  ExplodedNode *generateNode(const ProgramPoint &PP,
                             const ProgramState *State,
                             ExplodedNode *Pred,
                             bool MarkAsSink = false) {
    return generateNodeImpl(PP, State, Pred, MarkAsSink);
  }

  const ExplodedNodeSet &getResults() {
    finalizeResults();
    assert(checkResults());
    return Frontier;
  }

  typedef ExplodedNodeSet::iterator iterator;
  /// \brief Iterators through the results frontier.
  inline iterator begin() {
    finalizeResults();
    assert(checkResults());
    return Frontier.begin();
  }
  inline iterator end() {
    finalizeResults();
    return Frontier.end();
  }

  const NodeBuilderContext &getContext() { return C; }
  bool hasGeneratedNodes() { return HasGeneratedNodes; }

  void takeNodes(const ExplodedNodeSet &S) {
    for (ExplodedNodeSet::iterator I = S.begin(), E = S.end(); I != E; ++I )
      Frontier.erase(*I);
  }
  void takeNodes(ExplodedNode *N) { Frontier.erase(N); }
  void addNodes(const ExplodedNodeSet &S) { Frontier.insert(S); }
  void addNodes(ExplodedNode *N) { Frontier.Add(N); }
};

class CommonNodeBuilder {
protected:
  ExplodedNode *Pred;
  CoreEngine &Eng;

  CommonNodeBuilder(CoreEngine* E, ExplodedNode *P) : Pred(P), Eng(*E) {}
  BlockCounter getBlockCounter() const { return Eng.WList->getBlockCounter(); }
};

/// \class StmtNodeBuilder
/// \brief This builder class is useful for generating nodes that resulted from
/// visiting a statement. The main difference from it's parent NodeBuilder is
/// that it creates a statement specific ProgramPoint.
class StmtNodeBuilder: public NodeBuilder {
  NodeBuilder *EnclosingBldr;
public:

  /// \brief Constructs a StmtNodeBuilder. If the builder is going to process
  /// nodes currently owned by another builder(with larger scope), use
  /// Enclosing builder to transfer ownership.
  StmtNodeBuilder(ExplodedNode *SrcNode, ExplodedNodeSet &DstSet,
                      const NodeBuilderContext &Ctx, NodeBuilder *Enclosing = 0)
    : NodeBuilder(SrcNode, DstSet, Ctx), EnclosingBldr(Enclosing) {
    if (EnclosingBldr)
      EnclosingBldr->takeNodes(SrcNode);
  }

  StmtNodeBuilder(ExplodedNodeSet &SrcSet, ExplodedNodeSet &DstSet,
                      const NodeBuilderContext &Ctx, NodeBuilder *Enclosing = 0)
    : NodeBuilder(SrcSet, DstSet, Ctx), EnclosingBldr(Enclosing) {
    if (EnclosingBldr)
      for (ExplodedNodeSet::iterator I = SrcSet.begin(),
                                     E = SrcSet.end(); I != E; ++I )
        EnclosingBldr->takeNodes(*I);
  }

  virtual ~StmtNodeBuilder();

  ExplodedNode *generateNode(const Stmt *S,
                             ExplodedNode *Pred,
                             const ProgramState *St,
                             bool MarkAsSink = false,
                             const ProgramPointTag *tag = 0,
                             ProgramPoint::Kind K = ProgramPoint::PostStmtKind){
    const ProgramPoint &L = ProgramPoint::getProgramPoint(S, K,
                                  Pred->getLocationContext(), tag);
    return generateNodeImpl(L, St, Pred, MarkAsSink);
  }

  ExplodedNode *generateNode(const ProgramPoint &PP,
                             ExplodedNode *Pred,
                             const ProgramState *State,
                             bool MarkAsSink = false) {
    return generateNodeImpl(PP, State, Pred, MarkAsSink);
  }
};

class BranchNodeBuilder: public NodeBuilder {
  const CFGBlock *DstT;
  const CFGBlock *DstF;

  bool InFeasibleTrue;
  bool InFeasibleFalse;

public:
  BranchNodeBuilder(ExplodedNode *SrcNode, ExplodedNodeSet &DstSet,
                    const NodeBuilderContext &C,
                    const CFGBlock *dstT, const CFGBlock *dstF)
  : NodeBuilder(SrcNode, DstSet, C), DstT(dstT), DstF(dstF),
    InFeasibleTrue(!DstT), InFeasibleFalse(!DstF) {}

  BranchNodeBuilder(const ExplodedNodeSet &SrcSet, ExplodedNodeSet &DstSet,
                    const NodeBuilderContext &C,
                    const CFGBlock *dstT, const CFGBlock *dstF)
  : NodeBuilder(SrcSet, DstSet, C), DstT(dstT), DstF(dstF),
    InFeasibleTrue(!DstT), InFeasibleFalse(!DstF) {}

  ExplodedNode *generateNode(const ProgramState *State, bool branch,
                             ExplodedNode *Pred);

  const CFGBlock *getTargetBlock(bool branch) const {
    return branch ? DstT : DstF;
  }

  void markInfeasible(bool branch) {
    if (branch)
      InFeasibleTrue = true;
    else
      InFeasibleFalse = true;
  }

  bool isFeasible(bool branch) {
    return branch ? !InFeasibleTrue : !InFeasibleFalse;
  }
};

class IndirectGotoNodeBuilder {
  CoreEngine& Eng;
  const CFGBlock *Src;
  const CFGBlock &DispatchBlock;
  const Expr *E;
  ExplodedNode *Pred;

public:
  IndirectGotoNodeBuilder(ExplodedNode *pred, const CFGBlock *src, 
                    const Expr *e, const CFGBlock *dispatch, CoreEngine* eng)
    : Eng(*eng), Src(src), DispatchBlock(*dispatch), E(e), Pred(pred) {}

  class iterator {
    CFGBlock::const_succ_iterator I;

    friend class IndirectGotoNodeBuilder;
    iterator(CFGBlock::const_succ_iterator i) : I(i) {}
  public:

    iterator &operator++() { ++I; return *this; }
    bool operator!=(const iterator &X) const { return I != X.I; }

    const LabelDecl *getLabel() const {
      return llvm::cast<LabelStmt>((*I)->getLabel())->getDecl();
    }

    const CFGBlock *getBlock() const {
      return *I;
    }
  };

  iterator begin() { return iterator(DispatchBlock.succ_begin()); }
  iterator end() { return iterator(DispatchBlock.succ_end()); }

  ExplodedNode *generateNode(const iterator &I,
                             const ProgramState *State,
                             bool isSink = false);

  const Expr *getTarget() const { return E; }

  const ProgramState *getState() const { return Pred->State; }
};

class SwitchNodeBuilder {
  CoreEngine& Eng;
  const CFGBlock *Src;
  const Expr *Condition;
  ExplodedNode *Pred;

public:
  SwitchNodeBuilder(ExplodedNode *pred, const CFGBlock *src,
                    const Expr *condition, CoreEngine* eng)
  : Eng(*eng), Src(src), Condition(condition), Pred(pred) {}

  class iterator {
    CFGBlock::const_succ_reverse_iterator I;

    friend class SwitchNodeBuilder;
    iterator(CFGBlock::const_succ_reverse_iterator i) : I(i) {}

  public:
    iterator &operator++() { ++I; return *this; }
    bool operator!=(const iterator &X) const { return I != X.I; }
    bool operator==(const iterator &X) const { return I == X.I; }

    const CaseStmt *getCase() const {
      return llvm::cast<CaseStmt>((*I)->getLabel());
    }

    const CFGBlock *getBlock() const {
      return *I;
    }
  };

  iterator begin() { return iterator(Src->succ_rbegin()+1); }
  iterator end() { return iterator(Src->succ_rend()); }

  const SwitchStmt *getSwitch() const {
    return llvm::cast<SwitchStmt>(Src->getTerminator());
  }

  ExplodedNode *generateCaseStmtNode(const iterator &I,
                                     const ProgramState *State);

  ExplodedNode *generateDefaultCaseNode(const ProgramState *State,
                                        bool isSink = false);

  const Expr *getCondition() const { return Condition; }

  const ProgramState *getState() const { return Pred->State; }
};

class GenericNodeBuilderImpl {
protected:
  CoreEngine &engine;
  ExplodedNode *pred;
  ProgramPoint pp;
  SmallVector<ExplodedNode*, 2> sinksGenerated;  

  ExplodedNode *generateNodeImpl(const ProgramState *state,
                                 ExplodedNode *pred,
                                 ProgramPoint programPoint,
                                 bool asSink);

  GenericNodeBuilderImpl(CoreEngine &eng, ExplodedNode *pr, ProgramPoint p)
    : engine(eng), pred(pr), pp(p), hasGeneratedNode(false) {}

public:
  bool hasGeneratedNode;
  
  WorkList &getWorkList() { return *engine.WList; }
  
  ExplodedNode *getPredecessor() const { return pred; }
  
  BlockCounter getBlockCounter() const {
    return engine.WList->getBlockCounter();
  }
  
  const SmallVectorImpl<ExplodedNode*> &sinks() const {
    return sinksGenerated;
  }
};

template <typename PP_T>
class GenericNodeBuilder : public GenericNodeBuilderImpl {
public:
  GenericNodeBuilder(CoreEngine &eng, ExplodedNode *pr, const PP_T &p)
    : GenericNodeBuilderImpl(eng, pr, p) {}

  ExplodedNode *generateNode(const ProgramState *state, ExplodedNode *pred,
                             const ProgramPointTag *tag, bool asSink) {
    return generateNodeImpl(state, pred, cast<PP_T>(pp).withTag(tag),
                            asSink);
  }
  
  const PP_T &getProgramPoint() const { return cast<PP_T>(pp); }
};

class EndOfFunctionNodeBuilder : public CommonNodeBuilder {
  const CFGBlock &B;
  const ProgramPointTag *Tag;

public:
  bool hasGeneratedNode;

public:
  EndOfFunctionNodeBuilder(const CFGBlock *b, ExplodedNode *N, CoreEngine* e,
                           const ProgramPointTag *tag = 0)
    : CommonNodeBuilder(e, N), B(*b), Tag(tag), hasGeneratedNode(false) {}

  ~EndOfFunctionNodeBuilder();

  EndOfFunctionNodeBuilder withCheckerTag(const ProgramPointTag *tag) {
    return EndOfFunctionNodeBuilder(&B, Pred, &Eng, tag);
  }

  WorkList &getWorkList() { return *Eng.WList; }

  ExplodedNode *getPredecessor() const { return Pred; }

  unsigned getCurrentBlockCount() const {
    return getBlockCounter().getNumVisited(
                            Pred->getLocationContext()->getCurrentStackFrame(),
                                           B.getBlockID());
  }

  ExplodedNode *generateNode(const ProgramState *State,
                             ExplodedNode *P = 0,
                             const ProgramPointTag *tag = 0);

  void GenerateCallExitNode(const ProgramState *state);

  const CFGBlock *getBlock() const { return &B; }

  const ProgramState *getState() const {
    return getPredecessor()->getState();
  }
};

class CallEnterNodeBuilder {
  CoreEngine &Eng;

  const ExplodedNode *Pred;

  // The call site. For implicit automatic object dtor, this is the trigger 
  // statement.
  const Stmt *CE;

  // The context of the callee.
  const StackFrameContext *CalleeCtx;

  // The parent block of the CallExpr.
  const CFGBlock *Block;

  // The CFGBlock index of the CallExpr.
  unsigned Index;

public:
  CallEnterNodeBuilder(CoreEngine &eng, const ExplodedNode *pred, 
                         const Stmt *s, const StackFrameContext *callee, 
                         const CFGBlock *blk, unsigned idx)
    : Eng(eng), Pred(pred), CE(s), CalleeCtx(callee), Block(blk), Index(idx) {}

  const ProgramState *getState() const { return Pred->getState(); }

  const LocationContext *getLocationContext() const { 
    return Pred->getLocationContext();
  }

  const Stmt *getCallExpr() const { return CE; }

  const StackFrameContext *getCalleeContext() const { return CalleeCtx; }

  const CFGBlock *getBlock() const { return Block; }

  unsigned getIndex() const { return Index; }

  void generateNode(const ProgramState *state);
};

class CallExitNodeBuilder {
  CoreEngine &Eng;
  const ExplodedNode *Pred;

public:
  CallExitNodeBuilder(CoreEngine &eng, const ExplodedNode *pred)
    : Eng(eng), Pred(pred) {}

  const ExplodedNode *getPredecessor() const { return Pred; }

  const ProgramState *getState() const { return Pred->getState(); }

  void generateNode(const ProgramState *state);
}; 

} // end GR namespace

} // end clang namespace

#endif
