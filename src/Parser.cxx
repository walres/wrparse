/**
 * \file Parser.cxx
 *
 * \brief Parser implementation
 *
 * \copyright
 * \parblock
 *
 *   Copyright 2014-2016 James S. Waller
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 * \endparblock
 */
#include <assert.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <wrutil/circ_fwd_list.h>

#include <wrutil/CityHash.h>
#include <wrutil/uiostream.h>
#include <wrutil/VarGuard.h>

#include <wrparse/Lexer.h>
#include <wrparse/Parser.h>


using namespace std;


namespace wr {
namespace parse {


enum { DEBUG_INDENT = 4 };

/**
 * \brief reference to a specific point in a grammar
 *
 * GrammarAddress is equivalent to a 'label' (L) or 'grammar slot' as described
 * by the GLL papers.
 * A value of \c nullptr represents L0 (= here, the main loop in parseMain()).
 */
using GrammarAddress = const Component *;

bool operator==(GrammarAddress addr, Rule::const_iterator i)
        { return addr == &*i; }

bool operator!=(GrammarAddress addr, Rule::const_iterator i)
        { return addr != &*i; }

//--------------------------------------

class Parser::GSS
{
public:
        struct Edge;

        class Node
        {
        public:
                using this_t = Node;

                Node() : input_pos_(nullptr) {}
                Node(const this_t &other) = delete;
                Node(this_t &&other) = default;

                Node(GrammarAddress return_address, Token *input_pos) :
                        return_addr_(return_address), input_pos_(input_pos) {}

                this_t &operator=(const this_t &other) = delete;
                this_t &operator=(this_t &&other) = default;

                explicit operator bool() const { return input_pos_ != nullptr; }

                GrammarAddress returnAddress() const { return return_addr_; }

                const Rule *fromRule() const { return return_addr_->rule(); }

                const NonTerminal *fromNonTerminal() const;

                Token *inputPos() const { return input_pos_; }

                using ChildList = circ_fwd_list<Edge>;

                const ChildList &children() const { return children_; }

                std::pair<const Edge *, bool>
                        addChild(const this_t &child,
                                 SPPFNode::Ptr sppf_node) const;

                bool operator==(const this_t &other) const
                        { return (return_addr_ == other.return_addr_)
                                && (input_pos_ == other.input_pos_); }

                bool operator!=(const this_t &other) const
                        { return (return_addr_ != other.return_addr_)
                                || (input_pos_ != other.input_pos_); }

                bool operator<(const this_t &other) const
                        { return (return_addr_ < other.return_addr_) ||
                                  ((return_addr_ == other.return_addr_)
                                  && (input_pos_ < other.input_pos_)); }

                size_t hash() const
                       { return stdHash(this, offsetof(this_t, children_) -
                                              offsetof(this_t, return_addr_)); }

                struct Hash
                {
                        size_t operator()(const Node &node) const
                                { return node.hash(); }
                };

        private:
                GrammarAddress     return_addr_;
                Token             *input_pos_;
                mutable ChildList  children_;  // not involved in hash()
        };

        struct Edge
        {
                const Node    *child_;
                SPPFNode::Ptr  sppf_node_;

                bool operator<(const Edge &other) const;
        };

        void clear() { nodes_.clear(); }

        template <typename ...Args>
        std::pair<const Node *, bool>
        emplace(
                Args &&...args
        )
        {
                auto inserted = nodes_.emplace(std::forward<Args>(args)...);
                return std::make_pair(&*inserted.first, inserted.second);
        }

private:
        using Nodes = std::unordered_set<Node, Node::Hash>;
        Nodes nodes_;
};

//--------------------------------------

const NonTerminal *
Parser::GSS::Node::fromNonTerminal() const
{
        return fromRule() ? fromRule()->nonTerminal() : nullptr;
}

//--------------------------------------

auto
Parser::GSS::Node::addChild(
        const this_t  &child,
        SPPFNode::Ptr  sppf_node
) const -> std::pair<const Edge *, bool>
{
        for (const Edge &existing: children_) {
                if (existing.child_ == &child) {
                        if (existing.sppf_node_ == sppf_node) {
                                return std::make_pair(&existing, false);
                        }
                }
        }

        children_.push_back({ &child, sppf_node });
        return std::make_pair(&children_.back(), true);
}

//--------------------------------------

bool
Parser::GSS::Edge::operator<(
        const Edge &other
) const
{
        return (child_ < other.child_) || ((child_ == other.child_)
                                           && (sppf_node_ < other.sppf_node_));
}

//--------------------------------------

class Parser::GLL
{
public:
        GLL(Parser &parser, const NonTerminal &start) :
                parser_(parser), start_(start), recovery_pos_(nullptr) {}

        SPPFNode::Ptr parseMain(Token *input_start);

#ifndef NDEBUG
        void gdb_R() const;
#endif

        bool sppfToDOTFile(const char *file_name) const;

private:
        struct PtrHash
        {
                size_t operator()(const void *ptr) const
                        { return stdHash(&ptr, sizeof(ptr)); }
        };

        struct Popped
        {
                const GSS::Node *stack_head_;   // u in GLL paper
                SPPFNode::Ptr    parsed_node_;  // z in GLL paper

                bool operator<(const Popped &other) const;
        };

        using PoppedSet = std::set<Popped>;

        struct Descriptor
        {
                GrammarAddress   address_;    // L in GLL paper
                const GSS::Node *gss_head_;   // u in GLL paper
                Token           *input_pos_;  // j in GLL paper
                SPPFNode::Ptr    sppf_node_;  // w in GLL paper
                unsigned short   depth_;
                bool             advance_;
        };

        using DescriptorStack = std::vector<Descriptor>;

        struct VisitedItem
        {
                Token              *input_pos_;
                GrammarAddress      address_;    // L in GLL paper
                const GSS::Node    *gss_head_;   // u in GLL paper
                SPPFNode::ConstPtr  sppf_node_;  // w in GLL paper

                bool operator==(const VisitedItem &other) const
                        { return (input_pos_ == other.input_pos_)
                                  && (address_ == other.address_)
                                  && (gss_head_ == other.gss_head_)
                                  && (sppf_node_ == other.sppf_node_); }

                bool operator!=(const VisitedItem &other) const
                        { return (input_pos_ != other.input_pos_)
                                  || (address_ != other.address_)
                                  || (gss_head_ != other.gss_head_)
                                  || (sppf_node_ != other.sppf_node_); }

                size_t hash() const { return stdHash(this, sizeof(*this)); }

                struct Hash
                {
                        size_t operator()(const VisitedItem &item) const
                                { return item.hash(); }
                };
        };

        struct Mismatch
        {
                enum Kind : uint8_t
                {
                        NONE = 0,
                        NO_RULE,
                        TERMINAL_MISMATCH,
                        PREDICATE_FAILED,
                        POST_ACTION_FAILED
                };

                Descriptor d;
                Kind       kind;
        };

        using Mismatches = circ_fwd_list<Mismatch>;
        using VisitedItems = std::unordered_set<VisitedItem, VisitedItem::Hash>;
        using SPPFNodes = std::unordered_set<SPPFNode::Ptr, SPPFNode::Hash,
                                             SPPFNode::IndirectEqual>;


        const NonTerminal &getNonTerminal(const Descriptor &d) const;
        void report(const Mismatch &err);

        bool beginNonTerminal(const NonTerminal &nonterminal,
                              const GSS::Node *gss_head,
                              Token *input_pos, unsigned short depth);

        bool beginRule(const Rule &rule, const GSS::Node *gss_head,
                       Token *input_pos, unsigned short depth, bool immediate);

        void parse(Descriptor &d);

        bool endRule(Descriptor &d,
                     Mismatch::Kind mismatch_kind = Mismatch::NONE);

        bool visited(GrammarAddress address, const GSS::Node *gss_head,
                     Token *input_pos, SPPFNode::ConstPtr sppf_node) const;

        bool test(const Token *input_pos, const NonTerminal &nonterminal,
                  GrammarAddress trailing_terms) const;

        bool testFollow(const Token *input_pos,
                        GrammarAddress trailing_terms) const;

        void add(Descriptor d);

        void pop(const GSS::Node *gss_head, SPPFNode::Ptr parsed_node,
                 unsigned short depth);

        const GSS::Node *create(GrammarAddress return_address,
                                const GSS::Node *gss_head, Token *input_pos,
                                SPPFNode::Ptr sppf_node, unsigned short depth);

        static SPPFNode::Ptr hideRecursion(SPPFNode::Ptr parsed_node);
        static SPPFNode::Ptr
                hideDelegateOrTransparent(SPPFNode::Ptr parsed_node);

        std::pair<SPPFNode::Ptr, bool> getNode(SPPFNode::Ptr key);
        std::pair<SPPFNode::Ptr, bool> getPackedNode(SPPFNode::Ptr parent,
                                                     GrammarAddress slot,
                                                     Token *pivot, bool empty);
        SPPFNode::Ptr getNodeT(Token &terminal);
        SPPFNode::Ptr getNodeP(GrammarAddress slot,
                               SPPFNode::Ptr left, SPPFNode::Ptr right);
        SPPFNode::Ptr getEmptyNodeAt(Token &pos);


        Parser            &parser_;
        const NonTerminal &start_;
        GSS                gss_;
        SPPFNodes          sppf_nodes_;
        SPPFNode::Ptr      matched_;      // longest top-level match
        PoppedSet          popped_;       // P in GLL paper
        DescriptorStack    in_progress_;  // R in GLL paper
        VisitedItems       visited_;      // U in GLL paper
        const Token       *recovery_pos_;
        Mismatches         poss_errors_;
};

//--------------------------------------

bool
Parser::GLL::Popped::operator<(
        const Popped &other
) const
{
        return (stack_head_ < other.stack_head_)
                || ((stack_head_ == other.stack_head_)
                        && (parsed_node_ < other.parsed_node_));
}

//--------------------------------------

#ifndef NDEBUG

void
Parser::GLL::gdb_R() const
{
        ulog << "in_progress_ (R):";

        if (in_progress_.empty()) {
                ulog << " (empty)\n" << std::endl;
                return;
        }

        ulog << '\n';

        for (const Descriptor &d: in_progress_) {
                ulog << setw(DEBUG_INDENT) << "" << getNonTerminal(d).name();

                if (d.address_) {
                        const Rule &rule = *d.address_->rule();
                        ulog << '.' << rule.index()
                             << '[' << d.address_->index() << ']';
                }

                ulog << " @ " << d.input_pos_->offset() << '\n';
        }

        ulog.flush();
}

#endif // !NDEBUG

//--------------------------------------

bool
Parser::GLL::sppfToDOTFile(
        const char *file_name
) const
{
        std::ofstream output(file_name);

        if (!output.is_open()) {
                return false;
        }

        output << "digraph {\n"
               << "    graph [ordering=out]\n";

        for (SPPFNode::ConstPtr node: sppf_nodes_) {
                output << "    ";
                node->writeDOTNode(output);
                output << '\n';

                /* output packed node children which are not stored
                   in sppf_nodes_ */
                for (SPPFNode::ConstPtr child: node->children()) {
                        if (child->isPacked()) {
                                output << "    ";
                                child->writeDOTNode(output);
                                output << '\n';
                        }
                }
        }

        output << "}\n";
        return true;
}

//--------------------------------------

const NonTerminal &
Parser::GLL::getNonTerminal(
        const Descriptor &d
) const
{
        if (d.address_) {
                return *d.address_->rule()->nonTerminal();
        } else {
                return start_;
        }
}

//--------------------------------------

SPPFNode::Ptr
Parser::GLL::parseMain(
        Token *input_start
)
{
        if (!input_start) {
                input_start = parser_.nextToken();
        }

        gss_.clear();
        sppf_nodes_.clear();
        matched_ = nullptr;
        popped_.clear();
        in_progress_.clear();
        visited_.clear();

        const GSS::Node *u1 = gss_.emplace(GrammarAddress(), input_start).first,
                        *u0 = gss_.emplace().first;

        u1->addChild(*u0, nullptr);
        if (!beginNonTerminal(start_, u1, input_start, 0)) {
                recovery_pos_ = input_start;
                poss_errors_.push_front(Mismatch {
                        { nullptr, u1, input_start, nullptr, 0, false },
                        Mismatch::NO_RULE
                });
        }

        /*
         * L0: main parsing loop
         */
        while (!in_progress_.empty()) {
                Descriptor d = in_progress_.back();
                in_progress_.pop_back();
                parse(d);
        }

        if (!matched_ && recovery_pos_ && !poss_errors_.empty()) {
                report(poss_errors_.front());
                poss_errors_.pop_front();

                /* TODO: re-synchronise input token stream from recovery_pos_
                         and re-enter main loop */
        }

        sppf_nodes_.clear();
        return std::move(matched_);
}

//--------------------------------------

void
Parser::GLL::report(
        const Mismatch &err
)
{
        std::set<TokenKind>  expected_terminals;
        const NonTerminal   *nonterm = nullptr;

        switch (err.kind) {
        default: case Mismatch::NONE:  // unexpected
        case Mismatch::PREDICATE_FAILED:  // semantic error
        case Mismatch::POST_ACTION_FAILED:  // ditto
                return;
        case Mismatch::NO_RULE:
                if (err.d.address_) {
                        nonterm = err.d.address_->getAsNonTerminal();
                } else {
                        nonterm = &start_;
                }
                for (auto term: nonterm->firstSet()) {
                        if (term.first != TOK_EOF) {
                                expected_terminals.insert(term.first);
                        }
                }
                break;
        case Mismatch::TERMINAL_MISMATCH:
                expected_terminals.insert(err.d.address_
                                                ->getAsTerminal());
                break;
        }

        size_t       count = expected_terminals.size();
        std::string  expect;
        const char  *sep = "";

        for (const auto &term: expected_terminals) {
                expect += sep;
                u8string_view term_name
                                = parser_.lexer()->tokenKindName(term);

                if (term_name.size() == 1) {
                        expect += "'";
                        if (term_name == "'") {
                                expect += "\\";
                        }
                        expect += term_name.char_data();
                        expect += "'";
                } else {
                        expect += term_name.char_data();
                }

                if (--count > 1) {
                        sep = ", ";
                } else {
                        sep = " or ";
                }
        }

        parser_.emit(Diagnostic::ERROR, *recovery_pos_, "expected %s", expect);
}

//--------------------------------------
/*
 * roughly equivalent to the code(A, j) fragments in section 4.3
 * of the original "GLL Parsing" paper
 */
bool
Parser::GLL::beginNonTerminal(
        const NonTerminal &nonterminal,
        const GSS::Node   *gss_head,
        Token             *input_pos,
        unsigned short     depth
)
{
        auto   &terminals = nonterminal.firstSet();
        size_t  count = 0;

        if (terminals.empty()) {
                for (const Rule &rule: nonterminal) {
                        if (beginRule(rule, gss_head, input_pos,
                                      depth, false)) {
                                ++count;
                        }
                }
        } else {
                auto i = terminals.find(input_pos->kind());

                if (i != terminals.end()) {
                        if (!nonterminal.matchesEmpty() &&
                                      (i->second.begin() == i->second.last())) {
                                size_t ir = i->second.front();
                                if (beginRule(nonterminal[ir], gss_head,
                                              input_pos, depth, true)) {
                                        return true;
                                }
                        } else for (size_t ir: i->second) {
                                if (beginRule(nonterminal[ir], gss_head,
                                              input_pos, depth, false)) {
                                        ++count;
                                }
                        }
                }

                if (nonterminal.matchesEmpty()) {
                        i = terminals.find(TOK_NULL);
                        if (i != terminals.end()) {
                                for (size_t ir: i->second) {
                                        if (beginRule(nonterminal[ir], gss_head,
                                                     input_pos, depth, false)) {
                                                ++count;
                                        }
                                }
                        }
                }
        }

        if (parser_.debugEnabled() && !count) {
                ulog << setw(depth * DEBUG_INDENT) << ""
                     << "NORULE " << nonterminal.name()
                     << " @ " << input_pos->offset() << std::endl;
        }

        return count > 0;
}

//--------------------------------------

bool
Parser::GLL::beginRule(
        const Rule      &rule,
        const GSS::Node *gss_head,
        Token           *input_pos,
        unsigned short   depth,
        bool             immediate
)
{
        ParseState state(parser_, start_, rule, input_pos);

        if (!rule.nonTerminal()->invokePreParseActions(state)) {
                return false;
        }

        Descriptor d = { &rule[0], gss_head, input_pos, nullptr, depth, false };

        if (immediate) {
                parse(d);
        } else {
                add(d);
        }

        return true;
}

//--------------------------------------

void
Parser::GLL::parse(
        Descriptor &d
)
{
        if (!d.address_) {  // null address equivalent to L0
                return;
        }

        const Rule &rule = *d.address_->rule();

        if (parser_.debugEnabled() && (d.address_ != rule.end())) {
                if (d.advance_) {
                        // do this now to report the correct input offset
                        d.input_pos_ = parser_.nextToken(d.input_pos_);
                        d.advance_ = false;
                }

                ulog << setw(d.depth_ * DEBUG_INDENT) << "";

                // these variables make setting conditional breakpoints easy
                auto *nonterminal = rule.nonTerminal();
                auto  i_rule      = rule.index(),
                      i_comp      = d.address_->index();
                auto  offset      = d.input_pos_->offset();

                if (i_comp == 0) {
                        ulog << "ENTER  ";
                } else {
                        ulog << "RESUME ";
                }
                ulog << nonterminal->name() << '.' << i_rule
                     << '[' << i_comp << "] @ " << offset << std::endl;
        }

        for (; d.address_ != rule.end(); ++d.address_) {
                if (d.advance_) {
                        d.input_pos_ = parser_.nextToken(d.input_pos_);
                        d.advance_ = false;
                }

                const Component &step = *d.address_;

                if (d.address_->predicate()) {
                        ParseState state(parser_, start_, rule,
                                         d.input_pos_, d.sppf_node_);

                        bool result = d.address_->predicate()(state);

                        if (!result && !d.address_->isOptional()) {
                                endRule(d, Mismatch::PREDICATE_FAILED);
                                return;
                        }
                }

                if (d.address_->isTerminal()) {
                        TokenKind terminal = d.address_->getAsTerminal();

                        if ((terminal == TOK_NULL)
                                        || (terminal == d.input_pos_->kind())) {
                                auto t_node = getNodeT(*d.input_pos_);
                                if ((d.address_ == rule.begin())
                                    && std::next(d.address_) != rule.end()) {
                                        /* rule.size() >= 2
                                           and *rule.begin() is a terminal */
                                        d.sppf_node_ = t_node;
                                } else {
                                        d.sppf_node_ = getNodeP(d.address_,
                                                                d.sppf_node_,
                                                                t_node);
                                }
                                d.advance_ = true;
                        } else if (!step.isOptional()) {
                                endRule(d, Mismatch::TERMINAL_MISMATCH);
                                return;
                        } else {
                                d.sppf_node_ = getNodeP(d.address_,
                                                d.sppf_node_,
                                                getEmptyNodeAt(*d.input_pos_));
                        }
                } else if (step.isNonTerminal()) {
                        GrammarAddress return_addr = std::next(d.address_);
                        auto           nonterminal = step.getAsNonTerminal();

                        bool skip_optional = step.isOptional()
                                        && !nonterminal->matchesEmpty()
                                        && !visited(return_addr, d.gss_head_,
                                                    d.input_pos_, d.sppf_node_),
                             ok = false;

                        if (test(d.input_pos_, *nonterminal, return_addr)) {
                                auto *new_gss_head = create(d.address_,
                                                            d.gss_head_,
                                                            d.input_pos_,
                                                            d.sppf_node_,
                                                            d.depth_ + 1);

                                ok = beginNonTerminal(*nonterminal,
                                                    new_gss_head, d.input_pos_,
                                                    d.depth_ + 1);
                        } else if (parser_.debugEnabled()) {
                                ulog << setw(d.depth_ * DEBUG_INDENT) << ""
                                     << "NORULE " << nonterminal->name()
                                     << " @ " << d.input_pos_->offset()
                                     << std::endl;
                        }

                        ok = ok || skip_optional;

                        if (!ok) {
                                endRule(d, Mismatch::NO_RULE);
                                return;
                        }

                        if (!skip_optional) {
                                /* must return directly to main loop
                                   (equivalent of goto L0 in GLL paper) */
                                break;
                        } /* else optional nonterminal that doesn't match empty:
                             attempt path omitting that nonterminal */

                        d.sppf_node_ = getNodeP(d.address_, d.sppf_node_,
                                                getEmptyNodeAt(*d.input_pos_));
                }
        }

        if (d.address_ == rule.end()) {  // complete
                if (endRule(d)) {
                        pop(d.gss_head_, d.sppf_node_, d.depth_);
                }
        }
}

//--------------------------------------

bool
Parser::GLL::endRule(
        Descriptor     &d,
        Mismatch::Kind  mismatch_kind
)
{
        assert(d.address_);

        const char *dbg_prefix = nullptr;
        const Rule &rule       = *d.address_->rule();

        if (!mismatch_kind) {
                ParseState state(parser_, start_, rule, d.input_pos_,
                                 d.sppf_node_);
                if (!rule.nonTerminal()->invokePostParseActions(state)) {
                        mismatch_kind = Mismatch::POST_ACTION_FAILED;
                        dbg_prefix = "XCFAIL ";
                }
        }

        if (mismatch_kind) {
                if (!recovery_pos_ ||
                          (d.input_pos_->offset() >= recovery_pos_->offset())) {
                        recovery_pos_ = d.input_pos_;
                        poss_errors_.emplace_front(
                                                Mismatch { d, mismatch_kind });
                }
        }

        if (parser_.debugEnabled()) {
                // these variables make setting conditional breakpoints easy
                bool log_comp_ix = false;
                auto i_rule      = rule.index(),
                     i_comp      = rule.indexOf(*d.address_);
                auto offset      = d.input_pos_->offset();

                if (!mismatch_kind) {
                        dbg_prefix = "FINISH ";
                        offset = d.sppf_node_->endOffset();
                } else if (!dbg_prefix) {
                        dbg_prefix = "FAIL   ";
                        log_comp_ix = true;
                }

                ulog << setw(d.depth_ * DEBUG_INDENT) << "" << dbg_prefix
                     << rule.nonTerminal()->name() << '.' << i_rule;

                if (log_comp_ix) {
                        ulog << '[' << i_comp << ']';
                }

                ulog << " @ " << offset << std::endl;
        }

        return !mismatch_kind;
}
//--------------------------------------

bool
Parser::GLL::visited(
        GrammarAddress      address,
        const GSS::Node    *gss_head,
        Token              *input_pos,
        SPPFNode::ConstPtr  sppf_node
) const
{
        return visited_.count(
                VisitedItem { input_pos, address, gss_head, sppf_node }) > 0;
}

//--------------------------------------

bool
Parser::GLL::test(
        const Token       *input_pos,
        const NonTerminal &nonterminal,
        GrammarAddress     trailing_terms
) const
{
        return nonterminal.firstSet().empty()
                || nonterminal.firstSet().count(input_pos->kind())
                || (nonterminal.matchesEmpty()
                        && testFollow(input_pos, trailing_terms));
}

//--------------------------------------

bool
Parser::GLL::testFollow(
        const Token    *input_pos,
        GrammarAddress  trailing_terms
) const
{
        if (!trailing_terms) {
                return true;
        }

        const Rule &rule = *trailing_terms->rule();

        for (; trailing_terms != rule.end(); ++trailing_terms) {
                const Component &comp = *trailing_terms;

                if (comp.isTerminal()) {
                        if (comp.getAsTerminal() == input_pos->kind()) {
                                return true;
                        } else if (!comp.isOptional()) {
                                return false;
                        }
                } else if (comp.isNonTerminal()) {
                        return test(input_pos, *comp.getAsNonTerminal(),
                                    ++trailing_terms);
                } else if (!comp.isOptional()) {
                        return true;  // don't know, err on side of caution
                }
        }

        return true;
}

//--------------------------------------

void
Parser::GLL::add(
        Descriptor d
)
{
        assert(d.gss_head_);
        assert(d.input_pos_);

        // if {L, u, w} not in Uj (visited_[j]) add {L, u, w} to Uj
        bool ok = visited_.emplace(VisitedItem {
                d.input_pos_, d.address_, d.gss_head_, d.sppf_node_ }).second;

        if (ok) {
                in_progress_.push_back(d);  // add {L, u, i, w} to R
        } else if (parser_.debugEnabled()) {
                ulog << setw(d.depth_ * DEBUG_INDENT) << "" << "IGNORE ";

                if (d.address_) {
                        const Rule &rule = *d.address_->rule();
                        auto *nonterminal = rule.nonTerminal();
                        auto  i_rule = rule.index(),
                              i_comp = d.address_->index();
                        ulog << nonterminal->name() << '.' << i_rule
                             << '[' << i_comp << ']';
                } else {
                        ulog << start_.name();
                }

                ulog << " @ " << d.input_pos_->offset() << std::endl;
        }
}

//--------------------------------------

void
Parser::GLL::pop(
        const GSS::Node *gss_head,     // a.k.a. 'u'
        SPPFNode::Ptr    parsed_node,  // a.k.a. 'i'
        unsigned short   depth         // a.k.a. 'z'
)
{
        assert(gss_head);
        assert(parsed_node);

        popped_.insert(Popped { gss_head, parsed_node });

        for (const GSS::Edge &gss_edge: gss_head->children()) {
                GrammarAddress  return_address = gss_head->returnAddress();
                SPPFNode::Ptr   y;

                if (return_address) {
                        y = getNodeP(return_address, gss_edge.sppf_node_,
                                     hideDelegateOrTransparent(parsed_node));
                        ++return_address;
                } else {  // top-level match
                        if (!matched_ || (parsed_node->lastToken()->offset()
                                          > matched_->lastToken()->offset())) {
                                matched_ = parsed_node;
                        } /* else match is too short (so ignore it)
                             or equal length (will already be set) */
                }

                add({ return_address, gss_edge.child_, parsed_node->lastToken(),
                        y, static_cast<unsigned short>(depth - 1),
                        !parsed_node->empty() });
        }
}

//--------------------------------------

auto
Parser::GLL::create(
        GrammarAddress   return_address,
        const GSS::Node *gss_head,
        Token           *input_pos,
        SPPFNode::Ptr    sppf_node,
        unsigned short   depth
) -> const GSS::Node *
{
        assert(return_address);
        assert(gss_head);
        assert(input_pos);

        auto gss_insert = gss_.emplace(return_address, input_pos);
                // if there is not already a GSS node labelled (L, i) create one
        const GSS::Node *v = gss_insert.first;
                // let v be the GSS node labelled (L, i)

        if (v->addChild(*gss_head, sppf_node).second && !gss_insert.second) {
                // new edge to pre-existing GSS head node
                auto i = popped_.lower_bound({ v, nullptr });
                // for all (v, z) in P (a.k.a. popped_)
                for (; (i != popped_.end()) && (i->stack_head_ == v); ++i) {
                        // apply previously-parsed node(s) down the new edge
                        auto y = getNodeP(return_address, sppf_node,
                                hideDelegateOrTransparent(i->parsed_node_));

                        add({ std::next(return_address), gss_head,
                                i->parsed_node_->lastToken(), y,
                                static_cast<unsigned short>(depth - 1),
                                !i->parsed_node_->empty() });
                }
        }

        return v;
}

//--------------------------------------

SPPFNode::Ptr
Parser::GLL::hideDelegateOrTransparent(
        SPPFNode::Ptr parsed_node
) // static
{
        if (!parsed_node) {
                return nullptr;
        }

        SPPFNode::Ptr child  = parsed_node->firstChild(),
                      result = parsed_node;

        if (child && child->isPacked() && (child == parsed_node->lastChild())) {
                const Rule *child_rule = child->rule();
                if (child_rule && child_rule->mustHide()) {
                        if (child->firstChild() == child->lastChild()) {
                                child = child->firstChild();
                                if (child->isSymbol()) {
                                        result = child;
                                }
                        }
                }
        }

        return result;
}

//--------------------------------------

std::pair<SPPFNode::Ptr, bool>
Parser::GLL::getNode(
        SPPFNode::Ptr key
)
{
        auto i      = sppf_nodes_.find(key);
        bool insert = false;

        if (i == sppf_nodes_.end()) {
                insert = true;
                sppf_nodes_.insert(key);
        } else {
                key = *i;
        }

        return std::make_pair(key, insert);
}

//--------------------------------------

std::pair<SPPFNode::Ptr, bool>
Parser::GLL::getPackedNode(
        SPPFNode::Ptr   parent,
        GrammarAddress  slot,
        Token          *pivot,
        bool            empty
)
{
        for (SPPFNode::Ptr &child: parent->children()) {
                if (child->isPacked() && (child->component() == slot)) {
                        if (empty && child->empty()
                                  && (child->lastToken() == pivot)) {
                                return std::make_pair(child, false);
                        } else if (!empty && !child->empty()
                                          && (child->firstToken() == pivot)) {
                                return std::make_pair(child, false);
                        }
                }
        }

        return std::make_pair(new SPPFNode(*slot, *pivot, empty), true);
}

//--------------------------------------

SPPFNode::Ptr
Parser::GLL::getNodeT(
        Token &terminal
)
{
        return getNode(new SPPFNode(terminal)).first;
}

//--------------------------------------

SPPFNode::Ptr
Parser::GLL::getNodeP(
        GrammarAddress slot,
        SPPFNode::Ptr  left,
        SPPFNode::Ptr  right
)
{
        assert(slot);
        assert(right);

        const Rule &rule = *slot->rule();

        assert(slot != rule.end());

        bool   on_last_slot = std::next(slot) == rule.end();
        Token *left_extent;

        if (left) {
                if (!left->empty()) {
                        left_extent = left->firstToken();
                } else if (!right->empty()) {
                        left_extent = left->lastToken();
                } else {
                        left_extent = nullptr;  // completely empty
                }
        } else {
                if (!right->empty()) {
                        left_extent = right->firstToken();
                } else {
                        left_extent = nullptr;
                }
        }

        Token *right_extent, *pivot;

        if (right->empty()) {
                pivot = right->lastToken();
                if (left) {
                        right_extent = left->lastToken();
                } else {
                        right_extent = right->lastToken();
                }
        } else {
                pivot = right->firstToken();
                right_extent = right->lastToken();
        }

        SPPFNode::Ptr ret;

        if (on_last_slot) {
                ret = getNode(new SPPFNode(*rule.nonTerminal(), left_extent,
                                           *right_extent)).first;
        } else {
                ret = getNode(new SPPFNode(*slot, left_extent,
                                           *right_extent)).first;

                if (!left && right->isNonTerminal()
                          && (right->nonTerminal() == rule.nonTerminal())
                          && slot->isRecursive()
                          && !rule.nonTerminal()->keepRecursion()) {
                        for (auto child: right->children()) {
                                ret->addChild(child);
                        }
                        return ret;
                }
        }

        auto packed = getPackedNode(ret, slot, pivot, right->empty());

        if (packed.second) {
                if (left) {
                        packed.first->addChild(left);
                }
                packed.first->addChild(right);
                ret->addChild(packed.first);
        }

        return ret;
}

//--------------------------------------

SPPFNode::Ptr
Parser::GLL::getEmptyNodeAt(
        Token &pos
)
{
        return getNode(new SPPFNode(SPPFNode::emptyNode(pos))).first;
}

//--------------------------------------
/**
 * \brief Initialise an instance of \c Parser
 */
WRPARSE_API
Parser::Parser() :
        lexer_      (nullptr),
        debug_      (false),
        error_limit_(DEFAULT_ERROR_LIMIT)
{
}

//--------------------------------------
/**
 * \brief Initialise an instance of \c Parser with the specified \c lexer
 */
WRPARSE_API
Parser::Parser(
        Lexer &lexer
) :
        Parser()
{
        setLexer(lexer);
}

//--------------------------------------

WRPARSE_API
Parser::~Parser()
{
        if (lexer_) {
                lexer_->removeDiagnosticHandler(*this);
        }
}

//--------------------------------------

WRPARSE_API Parser &
Parser::setLexer(
        Lexer &lexer
)
{
        if (lexer_ != &lexer) {
                if (lexer_) {
                        lexer_->removeDiagnosticHandler(*this);
                }
                lexer_ = &lexer;
                lexer_->addDiagnosticHandler(*this);
        }
        return *this;
}

//--------------------------------------
/**
 * \brief Fetches the token after \c pos, reading a token from the lexer if
 * required
 *
 * \retval (when \c { pos != nullptr }) the location of the token
 *         immediately following \c pos
 * \retval (when \c { pos == nullptr }) the location of the first token
 *         parsed since the Parser object was created or since the most recent
 *         call to clear(), whichever happened later
 */
WRPARSE_API Token *
Parser::nextToken(
        const Token *pos  ///< the token before the one requested
)
{
        Token *next;

        if (tokens_.empty() || (pos == static_cast<Token *>(tokens_.last()))) {
                next = tokens_.emplace_back().node();
                size_t orig_error_count = errorCount();
                for (int strike = 0; lexer_->lex(*next).is(TOK_NULL); ) {
                        /* lexer error; emit() will eventually throw due to
                           fatal error once error limit reached */
                        if (errorCount() == orig_error_count) {
                                if (++strike == 3) {
                                        emit(Diagnostic::FATAL_ERROR, *next,
                                             "lexer not returning any tokens");
                                }
                        } else {
                                strike = 0;
                                orig_error_count = errorCount();
                        }
                }
        } else if (pos) {
                next = const_cast<Token *>(pos)->next();
        } else {
                next = tokens_.begin().node();
        }

        return next;
}

//--------------------------------------

WRPARSE_API Token *
Parser::lastToken()
{
        return tokens_.empty() ? nullptr : &tokens_.back();
}

//--------------------------------------

WRPARSE_API Parser &
Parser::reset()
{
        tokens_.clear();
        diagnostics_.clear();
        DiagnosticCounter::reset();
        return *this;
}

//--------------------------------------

WRPARSE_API Parser &
Parser::enableDebug(
        bool enable
)
{
        debug_ = enable;
        return *this;
}

//--------------------------------------

WRPARSE_API SPPFNode::Ptr
Parser::parse(
        const NonTerminal &start
)
try {
        if (!lexer_) {
                throw std::logic_error("Parser::parse(): no lexer set\n");
        } else if (start.empty()) {
                return nullptr;
        } else if (fatalErrorCount()) {
                return nullptr;
        } else if (nextToken()->is(TOK_EOF)) {
                return nullptr;
        }

        GLL gll(*this, start);

        // clear recorded diagnostics on scope exit
        struct OnExit
        {
                OnExit(EmittedDiagnostics::Set &diags) : diagnostics_(diags) {}
                ~OnExit() { diagnostics_.clear(); }

                EmittedDiagnostics::Set &diagnostics_;
        } on_exit(diagnostics_);

        SPPFNode::Ptr result = gll.parseMain(tokens_.begin().node());

        if (result) {
                TokenList::iterator next = tokens_.begin(),
                                    first,
                                    end  = tokens_.end();

                if (!result->empty()) {
                        first = tokens_.make_iterator(result->firstToken());
                } else {  // last token denotes position in stream
                        first = tokens_.make_iterator(result->lastToken());
                }

                for (; next != end; ++next) {
                        if (next == first) {
                                break;
                        }
                }

                if (next == end) {
                        throw std::logic_error(
                                "Parser::parse(): cannot find result position in token stream");
                }

                // delete any tokens before result (should be none but...)
                tokens_.erase_after(tokens_.before_begin(), next);

                tokens_.detach_after(
                        tokens_.before_begin(),
                        std::next(tokens_.make_iterator(
                                                result->lastToken())));

                result->takeTokens();  // result now owns its tokens
        }

        return result;
} catch (const Diagnostic &) {  // fatal error
        return nullptr;
}

//--------------------------------------

WRPARSE_API void
Parser::onDiagnostic(
        const Diagnostic &d
)
{
        emit(d);

        if (d.category() >= Diagnostic::ERROR) {
                if (errorCount() == error_limit_) {
                        emit(Diagnostic::FATAL_ERROR, d.offset(), 0, d.line(),
                             d.column(), "error limit (%u) reached, aborting",
                             error_limit_);
                }
        }
}

//--------------------------------------

WRPARSE_API void
Parser::setErrorLimit(
        size_t limit
)
{
        error_limit_ = limit;
}

//--------------------------------------

WRPARSE_API void
Parser::emit(
        const Diagnostic &d
)
{
        if (diagnostics_.insert({ d.id(), d.offset() }).second) {
                DiagnosticEmitter::emit(d);
                DiagnosticCounter::onDiagnostic(d);
                if (d.category() >= Diagnostic::FATAL_ERROR) {
                        throw d;
                }
        }
}

//--------------------------------------

WRPARSE_API
ParseState::ParseState(
        Parser              &parser,
        const NonTerminal   &start,
        const Rule          &rule,
        Token               *input_pos,
        SPPFNode::ConstPtr   parsed
) :
        parser_    (parser),
        start_     (start),
        rule_      (rule),
        input_pos_ (input_pos),
        parsed_    (parsed)
{
}

//--------------------------------------

ParseState::ParseState(const this_t &other) = default;
WRPARSE_API ParseState::~ParseState() = default;


} // namespace parse
} // namespace wr

/*

Error handling?

Lexer Errors
        1. Parser requests next token from Lexer, if successful then continue
                as normal skipping the steps below
        2. On error, Lexer reports the error location and message back to
                Parser, which passes it to client MessageHandler. Lexer records
                error position, if position equals previous error position then
                Lexer emits additional fatal error.
        3. For each error reported by Lexer during request for next token,
                Parser receives a call back from Lexer in which Parser
                increments error count; if error is not fatal but updated error
                count is greater than configured maximum then Parser emits
                fatal error to client MessageHandler
        4. Lexer returns token of type TOK_NULL to Parser; if any fatal errors
                were emitted then Parser stops
        5. Go back to step 1

Parser Errors
        Situation 1: No initial match upon beginning parse of non-optional
                     nonterminal

        Situation 2: Initial matches made upon beginning parse of non-optional
                     nonterminal, but all invoked pre-parse actions returned
                     false

        Situation 3: Non-optional terminal did not match while parsing rule

        Situation 4: Predicate function returned false on non-optional
                     empty (TOK_NULL) component

        Situation 5: Predicate function returned false before attempting to
                     match non-optional terminal

        Situation 6: Predicate function returned false before attempting to
                     match non-optional nonterminal

        Situation 7: Rule completely matched but post-parse action
                     returned false
*/
