//------------------------------------------------------------------------------
// AnalyzedAssertion.cpp
// Analysis support for concurrent assertions
//
// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#include "slang/analysis/AnalyzedAssertion.h"

#include "NonProceduralExprVisitor.h"

#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ClockInference.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/diagnostics/AnalysisDiags.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/util/Enum.h"
#include "slang/util/FlatMap.h"

// This works around an annoying MSVC bug where it warns
// in debug mode even though the code is reachable.
#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4702) // unreachable code
#endif

namespace slang::analysis {

using namespace ast;

static bool isSameClock(const TimingControl& left, const TimingControl& right) {
    if ((left.kind != TimingControlKind::SignalEvent &&
         left.kind != TimingControlKind::EventList) ||
        (right.kind != TimingControlKind::SignalEvent &&
         right.kind != TimingControlKind::EventList)) {
        // Ignore anything invalid here, we only want to compare valid clocks.
        return true;
    }

    if (left.kind != right.kind)
        return false;

    if (left.kind == TimingControlKind::EventList) {
        auto& le = left.as<EventListControl>();
        auto& re = right.as<EventListControl>();
        if (le.events.size() != re.events.size())
            return false;

        for (size_t i = 0; i < le.events.size(); i++) {
            if (!isSameClock(*le.events[i], *re.events[i]))
                return false;
        }
        return true;
    }

    auto& le = left.as<SignalEventControl>();
    auto& re = right.as<SignalEventControl>();
    if (le.edge != re.edge || bool(le.iffCondition) != bool(re.iffCondition))
        return false;

    if (le.iffCondition) {
        if (!le.iffCondition->syntax || !re.iffCondition->syntax)
            return false;
        return le.iffCondition->syntax->isEquivalentTo(*re.iffCondition->syntax);
    }

    if (!le.expr.syntax || !re.expr.syntax)
        return false;

    // TODO: this should check the AST expression and not the syntax
    return le.expr.syntax->isEquivalentTo(*re.expr.syntax);
}

using LocalSet = flat_hash_set<const Symbol*>;

static void computeFlowForBinaryOp(LocalSet& flow, const LocalSet& savedVars,
                                   const BinaryAssertionExpr& expr);

// Implements the sample(), block(), and flow() functions defined
// in F.5.4 for purposes of finding local assertion variables that
// are blocked from flowing out of a sequence.
struct BlockedVarsVisitor {
    LocalSet blocked;
    LocalSet sampled;
    LocalSet flow;

    void visit(const AssertionInstanceExpression& expr) {
        if (expr.isRecursiveProperty)
            return;

        // Collect assigned local variables in the context of the instantiation.
        for (auto local : expr.localVars) {
            if (local->getInitializer() ||
                (local->formalPort && local->formalPort->direction != ArgumentDirection::Out)) {
                // Inputs and inouts are always initialized by the caller,
                // whereas outputs are never initialized.
                flow.emplace(local);
            }
        }

        expr.body.visit(*this);

        for (auto local : expr.localVars)
            flow.erase(local);

        // Output local variable formal args are now definitely assigned.
        for (auto& [formal, actual] : expr.arguments) {
            if (formal->isLocalVar() && formal->direction == ArgumentDirection::Out) {
                if (auto init = std::get_if<const Expression*>(&actual)) {
                    if (auto sym = (*init)->getSymbolReference())
                        flow.emplace(sym);
                }
            }
        }
    }

    void visit(const SimpleAssertionExpr& expr) {
        LocalSet savedBlocked, savedSampled, savedFlow;
        if (isZeroRep(expr.repetition)) {
            savedBlocked = blocked;
            savedFlow = flow;
            savedSampled = sampled;
        }

        if (expr.expr.kind == ExpressionKind::AssertionInstance)
            visit(expr.expr.as<AssertionInstanceExpression>());

        if (isZeroRep(expr.repetition)) {
            blocked = std::move(savedBlocked);
            flow = std::move(savedFlow);
            sampled = std::move(savedSampled);
        }
    }

    void visit(const SequenceConcatExpr& expr) {
        for (auto& elem : expr.elements) {
            // The "block" of a concat should not include whatever flows out from later elems.
            flow.clear();
            elem.sequence->visit(*this);
            erase_if(blocked, [&](const Symbol* sym) { return flow.contains(sym); });
        }
    }

    void visit(const SequenceWithMatchExpr& expr) {
        LocalSet savedBlocked, savedSampled, savedFlow;
        if (isZeroRep(expr.repetition)) {
            savedBlocked = blocked;
            savedFlow = flow;
            savedSampled = sampled;
        }

        expr.expr.visit(*this);
        handleMatchItems(expr.matchItems);

        if (isZeroRep(expr.repetition)) {
            blocked = std::move(savedBlocked);
            flow = std::move(savedFlow);
            sampled = std::move(savedSampled);
        }
    }

    void visit(const FirstMatchAssertionExpr& expr) {
        expr.seq.visit(*this);
        handleMatchItems(expr.matchItems);
    }

    void visit(const ClockingAssertionExpr& expr) { expr.expr.visit(*this); }

    void visit(const BinaryAssertionExpr& expr, bool isRoot = false) {
        switch (expr.op) {
            case BinaryAssertionOperator::Or:
            case BinaryAssertionOperator::Intersect:
            case BinaryAssertionOperator::Throughout:
            case BinaryAssertionOperator::Within:
            case BinaryAssertionOperator::And: {
                // Union both sides, plus add in whatever has been
                // "sampled" on both sides.
                auto savedFlow = flow;
                sampled.clear();
                expr.left.visit(*this);

                auto leftSampled = std::move(sampled);
                sampled.clear();
                std::swap(savedFlow, flow);
                expr.right.visit(*this);

                // The resulting sampled set should be the union of both sides.
                auto rightSampled = sampled;
                sampled.insert(leftSampled.begin(), leftSampled.end());

                // To avoid infinite recursion when using a BlockedVarsVisitor to
                // compute flow (which we won't use at the root anyway) skip over
                // it here if isRoot is set.
                if (!isRoot)
                    computeFlowForBinaryOp(flow, savedFlow, expr);

                if (expr.op != BinaryAssertionOperator::Or) {
                    // The resulting blocked set should union the sampled intersection.
                    erase_if(leftSampled,
                             [&](const Symbol* sym) { return !rightSampled.contains(sym); });
                    blocked.insert(leftSampled.begin(), leftSampled.end());
                }
                break;
            }
            default:
                break;
        }
    }

    // These should never be called because this visitor is only used for sequences.
    void visit(const InvalidAssertionExpr&) {}
    void visit(const StrongWeakAssertionExpr&) {}
    void visit(const UnaryAssertionExpr&) {}
    void visit(const AbortAssertionExpr&) {}
    void visit(const ConditionalAssertionExpr&) {}
    void visit(const CaseAssertionExpr&) {}
    void visit(const DisableIffAssertionExpr&) {}

private:
    void handleMatchItems(std::span<const Expression* const> matchItems) {
        for (auto item : matchItems) {
            if (item->kind == ExpressionKind::Assignment) {
                auto& assign = item->as<AssignmentExpression>();
                if (auto sym = assign.left().getSymbolReference()) {
                    sampled.emplace(sym);
                    flow.emplace(sym);
                }
            }
        }
    }

    static bool isZeroRep(const std::optional<SequenceRepetition>& rep) {
        return rep && rep->range.min == 0 && rep->range.max == 0;
    }
};

static void computeFlowForBinaryOp(LocalSet& flow, const LocalSet& savedVars,
                                   const BinaryAssertionExpr& expr) {
    if (expr.op == BinaryAssertionOperator::Or) {
        // Definitely assigned variables are the intersection of the two sides.
        erase_if(flow, [&](const Symbol* sym) { return !savedVars.contains(sym); });
    }
    else {
        // Definitely assigned variables are the union of the two sides,
        // minus those that are "blocked" according to F.5.4.
        flow.insert(savedVars.begin(), savedVars.end());

        if (!flow.empty()) {
            BlockedVarsVisitor blockedVisitor;
            blockedVisitor.visit(expr, /* isRoot */ true);
            erase_if(flow, [&](const Symbol* sym) { return blockedVisitor.blocked.contains(sym); });
        }
    }
}

enum class VisitFlags { None = 0, RequireSequence = 1, InClockingBlock = 2 };
SLANG_BITMASK(VisitFlags, InClockingBlock)

// This visitor implements clock flow and resolution for assertion expressions.
// The requirements for this are scattered around the LRM. Some important parts are:
// - 16.13 describes multiclocked sequences and properties
// - 16.13.3 describes clock flow
// - 16.16.1 describes rules for determining semantic leading clocks
// - F.5.1 describes formal rewrite rules for clocks
//
// It also keeps track of which local variables are definitely assigned, for purposes of
// implementing the rules for when local variables are allowed to be referenced. These are
// specified in F.5.4 and 16.10.
struct AssertionVisitor {
    using VF = VisitFlags;
    using Clock = const TimingControl*;
    using ClockSet = SmallVector<Clock, 2>;
    using KnownSystemName = parsing::KnownSystemName;

    struct VisitResult {
        ClockSet clocks;
        Clock endClock = nullptr;
        bool isMulticlockedSeq = false;

        VisitResult() = default;
        VisitResult(Clock clock, bool isMulticlockedSeq, Clock endClock) :
            clocks{clock}, endClock(endClock), isMulticlockedSeq(isMulticlockedSeq) {}

        static VisitResult unionWith(const VisitResult& left, const VisitResult& right) {
            VisitResult result;
            result.clocks.reserve(left.clocks.size() + right.clocks.size());
            result.clocks.append_range(left.clocks);
            result.clocks.append_range(right.clocks);
            return result;
        }
    };

    struct SeqExprVisitor {
        AssertionVisitor& parent;
        Clock outerClock;
        bitmask<VF> flags;
        Clock lastEndClock = nullptr;
        const Expression* parentExpr = nullptr;

        SeqExprVisitor(AssertionVisitor& parent, Clock outerClock, bitmask<VF> flags) :
            parent(parent), outerClock(outerClock), flags(flags) {}

        template<typename T>
        void visit(const T& expr) {
            if constexpr (std::is_same_v<T, AssertionInstanceExpression>) {
                bool isForSequenceMethod = false;
                if (parentExpr && parentExpr->kind == ExpressionKind::Call) {
                    auto ksn = parentExpr->as<CallExpression>().getKnownSystemName();
                    isForSequenceMethod = ksn == KnownSystemName::Triggered ||
                                          ksn == KnownSystemName::Matched;
                }

                auto result = parent.visit(expr, outerClock, flags, isForSequenceMethod);
                if (!result.clocks.empty()) {
                    lastEndClock = result.endClock == nullptr ? result.clocks.back()
                                                              : result.endClock;
                }
                return;
            }

            if constexpr (HasVisitExprs<T, SeqExprVisitor>) {
                if constexpr (std::is_base_of_v<Expression, T>) {
                    parentExpr = &expr;
                }

                expr.visitExprs(*this);
                parentExpr = nullptr;

                if constexpr (std::is_same_v<T, CallExpression>) {
                    if (!parent.globalFutureSampledValueCall &&
                        SemanticFacts::isGlobalFutureSampledValueFunc(expr.getKnownSystemName())) {
                        parent.globalFutureSampledValueCall = &expr;
                        parent.checkGFSVC();
                    }

                    if (lastEndClock && outerClock) {
                        // The end clock of a sequence used with .triggered or .matched
                        // must match the outer clock.
                        if (!isSameClock(*outerClock, *lastEndClock)) {
                            parent.bad = true;
                            auto& diag = parent.context.addDiag(parent.parentSymbol,
                                                                diag::SeqMethodEndClock,
                                                                expr.sourceRange);
                            diag << expr.getSubroutineName();
                            diag.addNote(diag::NoteClockHere, outerClock->sourceRange);
                            diag.addNote(diag::NoteClockHere, lastEndClock->sourceRange);
                        }
                        lastEndClock = nullptr;
                    }
                }
            }
        }

        void visit(const NamedValueExpression& expr) {
            if (expr.symbol.kind == SymbolKind::LocalAssertionVar) {
                if (!parent.assignedVars.contains(&expr.symbol)) {
                    auto& diag = parent.context.addDiag(parent.parentSymbol,
                                                        diag::AssertionLocalUnassigned,
                                                        expr.sourceRange);
                    diag << expr.symbol.name;
                }
            }
        }
    };

    AnalysisContext& context;
    const AnalyzedProcedure* procedure;
    const Symbol& parentSymbol;
    SmallVector<ClockInference::ExpansionInstance> expansionStack;
    const CallExpression* globalFutureSampledValueCall = nullptr;
    LocalSet assignedVars;
    bool hasInferredClockCall = false;
    bool hasMatchItems = false;
    bool bad = false;

    AssertionVisitor(AnalysisContext& context, const AnalyzedProcedure* procedure,
                     const Symbol& parentSymbol) :
        context(context), procedure(procedure), parentSymbol(parentSymbol) {

        // If we're in a checker with an inferred clock arg, we will just assume
        // that we might have an inferred clock call somewhere.
        auto scope = parentSymbol.getParentScope();
        if (scope && scope->asSymbol().kind == SymbolKind::CheckerInstanceBody)
            hasInferredClockCall = true;
    }

    VisitResult visit(const InvalidAssertionExpr&, Clock, bitmask<VF>) {
        bad = true;
        return {};
    }

    VisitResult visit(const AssertionInstanceExpression& expr, Clock outerClock, bitmask<VF> flags,
                      bool isForSequenceMethod = false, bool isMaximalExpr = false) {
        if (expr.isRecursiveProperty)
            return {};

        const bool inClockingBlock = flags.has(VF::InClockingBlock);

        if (expr.type->isSequenceType())
            flags |= VF::RequireSequence;

        auto flowClock = outerClock;
        auto scope = expr.symbol.getParentScope();
        if (scope && scope->asSymbol().kind == SymbolKind::ClockingBlock) {
            // Outer clock comes from the clocking block.
            flowClock = &scope->asSymbol().as<ClockingBlockSymbol>().getEvent();
            flags |= VF::InClockingBlock;
        }

        SmallMap<const Symbol*, const Expression*, 4> outputArgRefs;
        SmallSet<const Symbol*, 4> sequenceMethodVars;

        for (auto& [formal, actual] : expr.arguments) {
            auto initExpr = std::get_if<const Expression*>(&actual);
            if (initExpr && formal->isLocalVar()) {
                // Inputs and inout local variable formal args should check that
                // any local vars passed in are definitely assigned.
                auto init = *initExpr;
                if (formal->direction != ArgumentDirection::Out)
                    visitExpr(*init);

                // Outputs and inouts can't reference the same local.
                if (formal->direction != ArgumentDirection::In) {
                    if (auto sym = init->getSymbolReference()) {
                        auto [it, inserted] = outputArgRefs.emplace(sym, init);
                        if (!inserted) {
                            auto& diag = context.addDiag(parentSymbol,
                                                         diag::AssertionFormalMultiAssign,
                                                         init->sourceRange);
                            diag << sym->name;
                            diag.addNote(diag::NotePreviousUsage, it->second->sourceRange);
                        }
                    }
                }
            }
            else if (initExpr && isForSequenceMethod) {
                // Local vars passed in to a sequence with 'triggered' applied are
                // considered to be not assigned.
                if (auto sym = (*initExpr)->getSymbolReference();
                    sym && sym->kind == SymbolKind::LocalAssertionVar) {
                    assignedVars.erase(sym);
                    sequenceMethodVars.emplace(sym);
                }
            }
        }

        expansionStack.push_back({expr, outerClock});
        hasInferredClockCall |= expansionStack.back().hasInferredClockArg;

        // Collect assigned local variables in the context of the instantiation.
        for (auto local : expr.localVars) {
            if (local->formalPort) {
                // Inputs and inouts are always initialized by the caller,
                // whereas outputs are never initialized.
                if (local->formalPort->direction != ArgumentDirection::Out)
                    assignedVars.emplace(local);
            }
            else if (auto init = local->getInitializer()) {
                visitExpr(*init);
                assignedVars.emplace(local);
            }
        }

        auto result = expr.body.visit(*this, flowClock, flags);

        // Output and inout formal args must have been definitely assigned
        // by the end of the called subsequence.
        for (auto local : expr.localVars) {
            if (local->formalPort) {
                if (local->formalPort->direction != ArgumentDirection::In) {
                    if (!assignedVars.contains(local)) {
                        auto& diag = context.addDiag(parentSymbol, diag::AssertionFormalUnassigned,
                                                     local->formalPort->location);
                        diag << local->name;
                    }
                }
            }
        }

        expansionStack.pop_back();

        // Delete local vars that we created for the instance context.
        for (auto local : expr.localVars)
            assignedVars.erase(local);

        // Output local variable formal args are now definitely assigned.
        for (auto& [formal, actual] : expr.arguments) {
            if (formal->isLocalVar() && formal->direction == ArgumentDirection::Out) {
                if (auto init = std::get_if<const Expression*>(&actual)) {
                    if (auto sym = (*init)->getSymbolReference())
                        assignedVars.emplace(sym);
                }
            }
        }

        // If this is a sequence method and it's not a maximal expression
        // then any local vars we passed in do not flow out.
        if (isForSequenceMethod && !isMaximalExpr) {
            for (auto local : sequenceMethodVars)
                assignedVars.erase(local);
        }

        // Named sequences and properties instantiated from within a clocking block
        // must be singly clocked and share the same clock as the clocking block.
        if (!bad && inClockingBlock && outerClock) {
            if (result.isMulticlockedSeq || result.clocks.size() != 1 ||
                !isSameClock(*outerClock, *result.clocks[0])) {

                bad = true;
                if (result.isMulticlockedSeq || result.clocks.size() != 1) {
                    context.addDiag(parentSymbol, diag::MulticlockedInClockingBlock,
                                    expr.sourceRange)
                        << expr.symbol.name;
                }
                else {
                    auto& diag = context.addDiag(parentSymbol, diag::DifferentClockInClockingBlock,
                                                 expr.sourceRange);
                    diag << expr.symbol.name;
                    diag.addNote(diag::NoteClockHere, outerClock->sourceRange);
                    diag.addNote(diag::NoteClockHere, result.clocks[0]->sourceRange);
                }
            }
        }

        return result;
    }

    VisitResult visit(const SimpleAssertionExpr& expr, Clock outerClock, bitmask<VF> flags) {
        LocalSet savedVars;
        if (isZeroOrMoreRep(expr.repetition)) {
            // Variable assignments don't flow out of this subexpression
            // when its repetition has a zero bound.
            savedVars = assignedVars;
        }

        std::optional<VisitResult> result;
        if (expr.expr.kind == ExpressionKind::AssertionInstance) {
            // If this is a direct sequence instance then we can return its result directly.
            result = visit(expr.expr.as<AssertionInstanceExpression>(), outerClock, flags);
        }
        else if (expr.expr.kind == ExpressionKind::Call) {
            // If this is a call to sequence method we don't require an outer clock,
            // so check for that case explicitly.
            auto& call = expr.expr.as<CallExpression>();
            if (auto ksn = call.getKnownSystemName();
                ksn == KnownSystemName::Triggered || ksn == KnownSystemName::Matched) {
                auto args = call.arguments();
                if (!args.empty() && args[0]->kind == ExpressionKind::AssertionInstance) {
                    result = visit(args[0]->as<AssertionInstanceExpression>(), outerClock, flags,
                                   /* isForSequenceMethod */ true, /* isMaximalExpr */ true);
                }
            }
        }

        if (!result) {
            // Visit the expression to find nested sequence instantiations due to
            // calls to .triggered and .matched. We will still require an outer clock
            // in the inheritedClock call below.
            SeqExprVisitor exprVisitor(*this, outerClock, flags);
            expr.expr.visit(exprVisitor);

            result = inheritedClock(expr, outerClock, flags | VF::RequireSequence);
        }

        if (isZeroOrMoreRep(expr.repetition))
            assignedVars = std::move(savedVars);

        return *result;
    }

    VisitResult visit(const SequenceConcatExpr& expr, Clock outerClock, bitmask<VF> flags) {
        Clock firstClock = nullptr;
        Clock endClock = nullptr;
        const AssertionExpr* lastExpr = nullptr;
        bool lastWasMulticlocked = false;
        bool isMulticlockedSeq = false;

        for (auto& elem : expr.elements) {
            auto result = elem.sequence->visit(*this, outerClock, flags | VF::RequireSequence);
            if (!result.clocks.empty()) {
                endClock = result.endClock == nullptr ? result.clocks.back() : result.endClock;
                if (!firstClock) {
                    firstClock = result.clocks[0];
                }
                else if (result.isMulticlockedSeq || !isSameClock(*firstClock, *result.clocks[0])) {
                    // When concatenating differently clocked sequences, the maximal single-clocked
                    // subsequences must not admit an empty match.
                    if (!lastWasMulticlocked)
                        requireOnlyNonEmptyMatch(*lastExpr);
                    if (!result.isMulticlockedSeq)
                        requireOnlyNonEmptyMatch(*elem.sequence);

                    isMulticlockedSeq = true;
                    if (elem.delay.min > 1 || elem.delay.max != elem.delay.min)
                        badMulticlockedSeq(*elem.sequence, *lastExpr, elem.delayRange);
                }
            }
            lastExpr = elem.sequence;
            lastWasMulticlocked = result.isMulticlockedSeq;
        }

        if (!firstClock)
            return {};

        return {firstClock, isMulticlockedSeq, endClock};
    }

    VisitResult visit(const SequenceWithMatchExpr& expr, Clock outerClock, bitmask<VF> flags) {
        LocalSet savedVars;
        if (isZeroOrMoreRep(expr.repetition)) {
            // Variable assignments don't flow out of this subexpression
            // when its repetition has a zero bound.
            savedVars = assignedVars;
        }

        auto result = expr.expr.visit(*this, outerClock, flags | VF::RequireSequence);
        handleMatchItems(expr.matchItems);

        if (isZeroOrMoreRep(expr.repetition))
            assignedVars = std::move(savedVars);

        return result;
    }

    VisitResult visit(const FirstMatchAssertionExpr& expr, Clock outerClock, bitmask<VF> flags) {
        auto result = expr.seq.visit(*this, outerClock, flags | VF::RequireSequence);
        handleMatchItems(expr.matchItems);
        return result;
    }

    VisitResult visit(const StrongWeakAssertionExpr& expr, Clock outerClock, bitmask<VF> flags) {
        return expr.expr.visit(*this, outerClock, flags | VF::RequireSequence);
    }

    VisitResult visit(const ClockingAssertionExpr& expr, Clock, bitmask<VF> flags) {
        // If we're inside a sequence or property instance that has an inferred
        // clocking argument we need to try to substitute calls to it from any
        // clocking expressions we find.
        auto clocking = &expr.clocking;
        if (hasInferredClockCall) {
            auto result = ClockInference::expand(context, parentSymbol, *clocking, expansionStack,
                                                 procedure);
            clocking = result.clock;
            if (result.diag) {
                bad = true;
                addExpansionNotes(*result.diag);
            }
        }

        if (clocking) {
            // Our current clock doesn't flow into the event expression,
            // so check it separately for explicit clocking of sequence instances
            // and calls to sampled value functions.
            NonProceduralExprVisitor visitor(context, parentSymbol);
            clocking->visit(visitor);
        }

        return expr.expr.visit(*this, clocking, flags);
    }

    VisitResult visit(const UnaryAssertionExpr& expr, Clock outerClock, bitmask<VF> flags) {
        auto result = visitProperty(expr.expr, outerClock, flags);
        if (expr.op == UnaryAssertionOperator::Not)
            return result;
        return inheritedClock(expr, outerClock, flags);
    }

    VisitResult visit(const AbortAssertionExpr& expr, Clock outerClock, bitmask<VF> flags) {
        auto result = visitProperty(expr.expr, outerClock, flags);
        if (!expr.isSync)
            return result;
        return inheritedClock(expr, outerClock, flags);
    }

    VisitResult visit(const BinaryAssertionExpr& expr, Clock outerClock, bitmask<VF> flags) {
        switch (expr.op) {
            case BinaryAssertionOperator::Intersect:
            case BinaryAssertionOperator::Throughout:
            case BinaryAssertionOperator::Within: {
                VisitResult lresult, rresult;
                handleBinarySeqOp(expr, outerClock, flags, lresult, rresult);
                return lresult;
            }
            case BinaryAssertionOperator::Until:
            case BinaryAssertionOperator::SUntil:
            case BinaryAssertionOperator::UntilWith:
            case BinaryAssertionOperator::SUntilWith:
                visitProperty(expr.left, outerClock, flags);
                visitProperty(expr.right, outerClock, flags);
                return inheritedClock(expr, outerClock, flags);
            case BinaryAssertionOperator::And:
            case BinaryAssertionOperator::Or: {
                // Clocks come from both sides.
                VisitResult lresult, rresult;
                if (flags.has(VF::RequireSequence)) {
                    handleBinarySeqOp(expr, outerClock, flags, lresult, rresult);
                }
                else {
                    lresult = visitProperty(expr.left, outerClock, flags);
                    rresult = visitProperty(expr.right, outerClock, flags);
                }
                return VisitResult::unionWith(lresult, rresult);
            }
            case BinaryAssertionOperator::Iff:
            case BinaryAssertionOperator::Implies: {
                // Clocks come from both sides.
                auto lresult = visitProperty(expr.left, outerClock, flags);
                auto rresult = visitProperty(expr.right, outerClock, flags);
                return VisitResult::unionWith(lresult, rresult);
            }
            case BinaryAssertionOperator::OverlappedImplication:
            case BinaryAssertionOperator::NonOverlappedImplication:
            case BinaryAssertionOperator::OverlappedFollowedBy:
            case BinaryAssertionOperator::NonOverlappedFollowedBy: {
                // Clocks come from just the left hand side.
                // Local variables *do* flow across the operator so we don't
                // use visitProperty here.
                auto lresult = expr.left.visit(*this, outerClock, flags | VF::RequireSequence);
                expr.right.visit(*this, outerClock, flags);
                return lresult;
            }
        }
        SLANG_UNREACHABLE;
    }

    VisitResult visit(const ConditionalAssertionExpr& expr, Clock outerClock, bitmask<VF> flags) {
        visitProperty(expr.ifExpr, outerClock, flags);
        if (expr.elseExpr)
            visitProperty(*expr.elseExpr, outerClock, flags);

        // Semantic leading clock is always inherited.
        return inheritedClock(expr, outerClock, flags);
    }

    VisitResult visit(const CaseAssertionExpr& expr, Clock outerClock, bitmask<VF> flags) {
        for (auto& item : expr.items)
            visitProperty(*item.body, outerClock, flags);

        if (expr.defaultCase)
            visitProperty(*expr.defaultCase, outerClock, flags);

        // Semantic leading clock is always inherited.
        return inheritedClock(expr, outerClock, flags);
    }

    VisitResult visit(const DisableIffAssertionExpr& expr, Clock outerClock, bitmask<VF> flags) {
        // Our current clock doesn't flow into the disable iff condition,
        // so check it separately for explicit clocking of sequence instances
        // and calls to sampled value functions.
        NonProceduralExprVisitor visitor(context, parentSymbol, /*isDisableCondition =*/true);
        expr.condition.visit(visitor);
        visitExpr(expr.condition);

        return expr.expr.visit(*this, outerClock, flags);
    }

    void checkGFSVC() {
        if (!bad && globalFutureSampledValueCall && hasMatchItems) {
            bad = true;

            auto& diag = context.addDiag(parentSymbol, diag::GFSVMatchItems,
                                         globalFutureSampledValueCall->sourceRange);
            diag << globalFutureSampledValueCall->getSubroutineName();
        }
    }

private:
    static std::string_view exprKindStr(bitmask<VF> flags) {
        return flags.has(VF::RequireSequence) ? "sequence"sv : "property"sv;
    }

    static bool isZeroOrMoreRep(const std::optional<SequenceRepetition>& rep) {
        return rep && rep->range.min == 0;
    }

    VisitResult inheritedClock(const AssertionExpr& expr, Clock outerClock, bitmask<VF> flags) {
        if (!outerClock) {
            if (!bad) {
                bad = true;

                SourceRange range;
                SLANG_ASSERT(expr.syntax);
                if (!expansionStack.empty())
                    range = expansionStack.front().expr->sourceRange;
                else
                    range = expr.syntax->sourceRange();

                auto& diag = context.addDiag(parentSymbol, diag::AssertionNoClock, range);
                diag << exprKindStr(flags);

                if (!expansionStack.empty()) {
                    for (size_t i = 1; i < expansionStack.size(); i++)
                        diag.addNote(diag::NoteRequiredHere, expansionStack[i].expr->sourceRange);
                    diag.addNote(diag::NoteRequiredHere, expr.syntax->sourceRange());
                }
            }
            return {};
        }
        return {outerClock, false, nullptr};
    }

    void badMulticlockedSeq(const AssertionExpr& left, const AssertionExpr& right,
                            SourceRange opRange) {
        if (!bad) {
            bad = true;

            SLANG_ASSERT(left.syntax);
            SLANG_ASSERT(right.syntax);

            auto leftRange = left.syntax->sourceRange();
            auto range = opRange;
            if (!range.start())
                range = leftRange;

            auto& diag = context.addDiag(parentSymbol, diag::InvalidMulticlockedSeqOp, range);
            diag << leftRange << right.syntax->sourceRange();
            addExpansionNotes(diag);
        }
    }

    void requireOnlyNonEmptyMatch(const AssertionExpr& expr) {
        if (!bad && expr.checkNondegeneracy().status.has(NondegeneracyStatus::AdmitsEmpty)) {
            bad = true;

            SLANG_ASSERT(expr.syntax);
            context.addDiag(parentSymbol, diag::MulticlockedSeqEmptyMatch,
                            expr.syntax->sourceRange());
        }
    }

    void addExpansionNotes(Diagnostic& diag) {
        for (auto it = expansionStack.rbegin(); it != expansionStack.rend(); it++) {
            auto& expr = *it->expr;
            diag.addNote(diag::NoteExpandedHere, expr.sourceRange);
        }
    }

    void handleMatchItems(std::span<const Expression* const> matchItems) {
        if (matchItems.empty())
            return;

        if (!hasMatchItems) {
            hasMatchItems = true;
            checkGFSVC();
        }

        for (auto expr : matchItems) {
            if (expr->kind == ExpressionKind::Assignment) {
                auto& assign = expr->as<AssignmentExpression>();
                visitExpr(assign.right());
                if (auto sym = assign.left().getSymbolReference())
                    assignedVars.emplace(sym);
            }
            else {
                visitExpr(*expr);
            }
        }
    }

    void handleBinarySeqOp(const BinaryAssertionExpr& expr, Clock outerClock, bitmask<VF> flags,
                           VisitResult& lresult, VisitResult& rresult) {
        auto savedVars = assignedVars;
        lresult = expr.left.visit(*this, outerClock, flags | VF::RequireSequence);

        std::swap(assignedVars, savedVars);
        rresult = expr.right.visit(*this, outerClock, flags | VF::RequireSequence);

        if (lresult.isMulticlockedSeq || rresult.isMulticlockedSeq ||
            (!lresult.clocks.empty() && !rresult.clocks.empty() &&
             !isSameClock(*lresult.clocks[0], *rresult.clocks[0]))) {
            badMulticlockedSeq(expr.left, expr.right, expr.opRange);
        }

        computeFlowForBinaryOp(assignedVars, savedVars, expr);
    }

    VisitResult visitProperty(const AssertionExpr& expr, Clock outerClock, bitmask<VF> flags) {
        auto savedVars = assignedVars;
        auto result = expr.visit(*this, outerClock, flags);

        assignedVars = std::move(savedVars);
        return result;
    }

    void visitExpr(const Expression& expr) {
        SeqExprVisitor exprVisitor(*this, nullptr, VisitFlags::None);
        expr.visit(exprVisitor);
    }
};

AnalyzedAssertion::AnalyzedAssertion(AnalysisContext& context, const TimingControl* contextualClock,
                                     const AnalyzedProcedure& procedure, const Statement& stmt,
                                     const Symbol* checkerInstance) {
    if (checkerInstance) {
        auto& inst = checkerInstance->as<CheckerInstanceSymbol>();
        checkerScope = &context.manager->analyzeScopeBlocking(inst.body, &procedure);

        NonProceduralExprVisitor visitor(context, inst);
        inst.visitExprs(visitor);

        for (auto& conn : inst.getPortConnections()) {
            if (conn.formal.kind == SymbolKind::FormalArgument && conn.actual.index() == 0)
                context.manager->noteDriver(*std::get<0>(conn.actual), inst);
        }
    }
    else {
        AssertionVisitor visitor(context, &procedure, *procedure.analyzedSymbol);

        auto& propSpec = stmt.as<ConcurrentAssertionStatement>().propertySpec;
        auto result = propSpec.visit(visitor, contextualClock, VisitFlags::None);

        if (!visitor.bad && result.clocks.size() > 1) {
            // There must be a unique semantic leading clock.
            auto firstClock = result.clocks[0];
            for (size_t i = 1; i < result.clocks.size(); i++) {
                if (!isSameClock(*firstClock, *result.clocks[i])) {
                    SLANG_ASSERT(propSpec.syntax);
                    auto& diag = context.addDiag(*procedure.analyzedSymbol, diag::NoUniqueClock,
                                                 propSpec.syntax->sourceRange());
                    diag.addNote(diag::NoteClockHere, firstClock->sourceRange);
                    diag.addNote(diag::NoteClockHere, result.clocks[i]->sourceRange);
                    break;
                }
            }
        }
    }
}

AnalyzedAssertion::AnalyzedAssertion(AnalysisContext& context, const TimingControl* contextualClock,
                                     const AnalyzedProcedure* procedure,
                                     const ast::Symbol& parentSymbol, const Expression& expr) {
    AssertionVisitor visitor(context, procedure, parentSymbol);
    visitor.visit(expr.as<AssertionInstanceExpression>(), contextualClock, VisitFlags::None);
}

} // namespace slang::analysis

#ifdef _MSC_VER
#    pragma warning(pop)
#endif
