/**
 * \file SPPF.h
 *
 * \brief Data types for representation and traversal of Shared Packed Parse
 *      Forests (SPPFs)
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
 *
 * \detail Shared Packed Parse Forests represent all possible grammar
 *      traversals for a given sequence of input tokens, including ambiguities
 *      -- an extension of the concept of a parse tree. An SPPF is made up of
 *      \i nodes - objects of the \c SPPFNode class - each of which details
 *      what part of the input token stream they were matched against (zero or
 *      more tokens), what point in the grammar they stem from (for packed /
 *      intermediate nodes), which parsed production or token type they refer
 *      to (for nonterminal or terminal symbol nodes), zero or more children
 *      plus any auxiliary data the user wishes to attach to them.
 *
 *      SPPF nodes come in three basic incarnations:
 *
 *      \li <i>Symbol nodes</i> representing a matched terminal (one token of
 *              a given type) or a matched nonterminal (a set of zero or more
 *              tokens matched to a production). Terminal symbol nodes do not
 *              have child nodes. A symbol node is "labelled" with the terminal
 *              or nonterminal it matched along with the range of tokens it
 *              covers (which may be empty)
 *      \li <i>Intermediate nodes</i> representing a partially-matched
 *              production; these nodes are a necessary part of "binarising"
 *              the SPPF to ensure the GLL parsing algorithm is of cubic
 *              complexity at most. An intermediate node is "labelled" with
 *              a specific point in the grammar that it is associated with
 *              and the range of tokens covered by its children.
 *      \li <i>Packed nodes</i> representing one complete parse (a 'fork', if
 *              you will) of the entity represented by its parent. Packed nodes
 *              have at most two children and also serve for "binarising"
 *              the SPPF. A packde node is "labelled" with a specific point in
 *              the grammar (like an intermediate node) and a 'pivot' position
 *              within the token stream representing the point where its left
 *              child ends and right child begins (or where its only child
 *              begins, if there is just one).
 *
 *      Some important rules to remember:
 *
 *      \li Nonterminal symbol nodes (terminals never have children) and
 *              intermediate nodes may have packed children or other kinds
 *              of children, but never packed children mixed with other kinds.
 *      \li If a symbol node or intermediate node has packed children, there
 *              will be as many packed children as there were successful parses
 *              made by tracing distinct paths through the grammar for the
 *              same set of tokens. In short: more than one packed child means
 *              that ambiguities exist.
 *      \li With symbol or intermediate child nodes there will be a maximum
 *              of two children.
 *      \li Packed nodes only have symbol or intermediate nodes as children.
 *
 *      Put another way, symbol nodes correspond to each matched fragment of
 *      the grammar. Intermediate nodes tie multiple symbol nodes together and
 *      link them to the actual points in the grammar they were matched against.
 *      Packed nodes represent 'forks' taken through the grammar from the same
 *      point (only one if there were no ambiguities).
 *
 *      SPPF Traversal
 *
 *      Several classes are available for traversing SPPFs in different ways:
 *
 *      \li \c SPPFWalker - the most basic; passes through the SPPF nodes
 *              literally from a given start point, giving the user the ability
 *              to "walk left", "walk right" or "backtrack" the way they came
 *              (but never back past the start position).
 *      \li \c NonTerminalWalker - hides some of the complexity of the SPPF,
 *              in that it provides apparent iteration through the immediate
 *              nonterminal subcomponents of the starting node (if applicable).
 *      \li \c SubProductionWalker - based on NonTerminalWalker, differs from
 *              it in that it initially descends through nonterminal
 *              subcomponents that cover the same range of tokens as the
 *              starting node until it finds a subcomponent that covers a
 *              strict subset of the starting node. After this it behaves the
 *              same as NonTerminalWalker.
 */
#ifndef WRPARSE_SPPF_H
#define WRPARSE_SPPF_H

#include <iosfwd>
#include <string>

#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <wrutil/circ_fwd_list.h>

#include <wrparse/Config.h>
#include <wrparse/Grammar.h>
#include <wrparse/Token.h>


namespace wr {
namespace parse {


using TokenList = intrusive_circ_fwd_list<Token>;


class Parser;
class ParseState;

//--------------------------------------
/**
 * \brief Base class for external data attachable to an SPPF node
 *
 * Users of the wrparse library can associate data directly with SPPF nodes by
 * overriding the \c AuxData class to add extra members, then use
 * SPPFNode::setAuxData() to attach their AuxData-derived object to the desired
 * node. SPPFNode::auxData() can be used to recall this data afterwards.
 *
 * \note This class is reference-counted for automatic memory management;
 *      instances attached to \c SPPFNode objects must always be created on the
 *      heap and not on the stack.
 */
struct WRPARSE_API AuxData :
        public boost::intrusive_ref_counter<AuxData>
{
public:
        using Ptr = boost::intrusive_ptr<AuxData>;
        using ConstPtr = boost::intrusive_ptr<const AuxData>;

        virtual ~AuxData();
};

//--------------------------------------

template <typename T>
struct IntrusivePtrListTraits
{
        using node_type = T;
        using node_ptr_type = boost::intrusive_ptr<node_type>;
        using const_node_ptr_type = boost::intrusive_ptr<const node_type>;
        using value_type = node_type;
        using reference = node_type &;
        using const_reference = const node_type &;
        using pointer = node_ptr_type;
        using const_pointer = const_node_ptr_type;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using allocator_type = std::allocator<node_type>;
        using allocator_traits = std::allocator_traits<allocator_type>;

        static pointer get_value_ptr(node_ptr_type node)
                { return node; }

        static node_ptr_type next_node(node_ptr_type node)
                { return node->next(); }

        static void set_next_node(node_ptr_type node,
                                  node_ptr_type next)
                { node->next(next); }

        static void destroy_node(allocator_type &/* allocator */,
                                 node_ptr_type   node)
                { set_next_node(node, nullptr); }
};

//--------------------------------------
/**
 * \brief Shared Packed Parse Forest node
 */
class WRPARSE_API SPPFNode :
        public boost::intrusive_ref_counter<SPPFNode>
{
public:
        using this_t = SPPFNode;
        using Ptr = boost::intrusive_ptr<this_t>;
        using ConstPtr = boost::intrusive_ptr<const this_t>;
        using ChildList = circ_fwd_list<Ptr>;

        enum Kind { NONTERMINAL = 0, TERMINAL, PACKED, INTERMEDIATE };

        SPPFNode() = delete;
        SPPFNode(const this_t &other) = delete;
        SPPFNode(this_t &&other);
        SPPFNode(const Production &nonterminal, Token *first_token,
                 Token &last_token);

        SPPFNode(Token &terminal);
        SPPFNode(const Component &slot, Token &pivot, bool empty);
                                                        // create packed node
        SPPFNode(const Component &component, Token *first_token,
                 Token &last_token);  // create intermediate node

        ~SPPFNode();

        static this_t emptyNode(Token &next);

        this_t &operator=(const this_t &other) = delete;
        this_t &operator=(this_t &&r);

        Kind kind() const { return static_cast<Kind>(bits_ & 3); }

        const Production *nonTerminal() const;
        TokenKind terminal() const;
        const Rule *rule() const;
        const Component *component() const;
        void setAuxData(AuxData::Ptr data) const { aux_data_ = data; }
        AuxData::Ptr auxData() const             { return aux_data_; }
        ChildList &children()              { return children_; }
        const ChildList &children() const  { return children_; }
        Ptr firstChild();
        ConstPtr firstChild() const;
        Ptr lastChild();
        ConstPtr lastChild() const;
        bool hasChildren() const           { return !children_.empty(); }
        size_t countChildren() const       { return children_.size(); }
        size_t countTokens() const;
        bool empty() const                 { return !firstToken(); }

        Token *firstToken();              // defined out-of-line below
        const Token *firstToken() const;  // ditto

        Token *lastToken()                 { return last_token_; }
        const Token *lastToken() const     { return last_token_; }
        Token::Offset startOffset() const;
        Token::Offset endOffset() const;
        size_t size() const { return endOffset() - startOffset(); }

        std::string content(int max_tokens = -1) const;
        void dump() const;

        bool operator==(const this_t &other) const;
        bool operator!=(const this_t &other) const
                { return !operator==(other); }

        bool isNonTerminal() const { return kind() == NONTERMINAL; }
        bool isTerminal() const { return kind() == TERMINAL; }
        bool isSymbol() const { return isNonTerminal() || isTerminal(); }
        bool isPacked() const { return kind() == PACKED; }
        bool isIntermediate() const { return kind() == INTERMEDIATE; }

        bool is(const this_t &other) const;
        bool is(TokenKind terminal) const;
        bool is(const Production &nonterminal) const;
        bool is(const Production &nonterminal, Ptr &out_pos);
        bool is(const Production &nonterminal, ConstPtr &out_pos) const;

        Ptr find(const Production &nonterminal, int max_depth = -1);

        ConstPtr find(const Production &nonterminal, int max_depth = -1) const
            { return const_cast<this_t *>(this)->find(nonterminal, max_depth); }

        void addChild(Ptr other);

        const size_t hash() const;

        bool writeDOTGraphFile(const char *file_name) const;
        void writeDOTGraph(std::ostream &output) const;
        void writeDOTNodes(std::ostream &output) const;
        void writeDOTNode(std::ostream &output) const;

        struct Hash
        {
                size_t operator()(const SPPFNode &n) const { return n.hash(); }
                size_t operator()(ConstPtr n) const        { return n->hash(); }
        };

        struct IndirectEqual
        {
                bool operator()(ConstPtr a, ConstPtr b) const
                        { return (*a) == (*b); }
        };

private:
        friend Parser;

        void takeTokens();
        void freeTokens();

        union // stores kind in lowest two bits
        {
                const Production *nonterminal_;  // for nonterminal symbol node
                const Component  *slot_;  // for intermediate or packed node
                uintptr_t         bits_;  // for easy access to low bits
        };

        union // lowest bit signifies ownership of tokens if set
        {
                Token            *first_token_;  // null if empty
                uintptr_t         bits2_;  // for easy access to low bits
        };

        Token                    *last_token_;

        // following items not involved in hashing or comparison
        mutable AuxData::Ptr      aux_data_;
        mutable ChildList         children_;
};

//--------------------------------------

inline Token *
SPPFNode::firstToken()
{
        return reinterpret_cast<Token *>(bits2_ & ~static_cast<uintptr_t>(3));
}

//--------------------------------------

inline const Token *
SPPFNode::firstToken() const
{
        return reinterpret_cast<Token *>(bits2_ & ~static_cast<uintptr_t>(3));
}

//--------------------------------------

template <typename NodeT>
class WRPARSE_API SPPFWalkerTemplate
{
public:
        using this_t     = SPPFWalkerTemplate;
        using node_t     = NodeT;
        using node_ptr_t = boost::intrusive_ptr<node_t>;

        SPPFWalkerTemplate() = default;
        SPPFWalkerTemplate(node_ptr_t start) : start_(start) {}

        explicit operator bool() const { return !trail_.empty(); }

        node_t &operator*() const     { return *node(); }
        node_ptr_t operator->() const { return node(); }

        node_ptr_t node() const;
        node_ptr_t start() const { return start_; }
        bool walkLeft(node_ptr_t stop_at = nullptr);
        bool walkRight(node_ptr_t stop_at = nullptr);
        bool backtrack();

        void reset(node_ptr_t new_start);
        void reset() { reset(start_); }

        bool operator==(const this_t &other) const
                { return node() == other.node(); }

        bool operator!=(const this_t &other) const
                { return node() != other.node(); }

protected:
        void extend(this_t &&other);

private:
        using child_iter_t
                = decltype(static_cast<node_t *>(0)->children().begin());

        node_ptr_t                  start_;  ///< start point
        circ_fwd_list<child_iter_t> trail_;  ///< route taken from start point
};

// instantiated by library
extern template class SPPFWalkerTemplate<SPPFNode>;
extern template class SPPFWalkerTemplate<const SPPFNode>;

using SPPFWalker = SPPFWalkerTemplate<SPPFNode>;
using SPPFConstWalker = SPPFWalkerTemplate<const SPPFNode>;

//--------------------------------------

template <typename NodeT>
class WRPARSE_API NonTerminalWalkerTemplate :
        protected SPPFWalkerTemplate<NodeT>
{
public:
        using this_t = NonTerminalWalkerTemplate;
        using base_t = SPPFWalkerTemplate<NodeT>;
        using node_t = typename base_t::node_t;
        using node_ptr_t = typename base_t::node_ptr_t;

        NonTerminalWalkerTemplate() = default;

        NonTerminalWalkerTemplate(const this_t &other) = default;
        NonTerminalWalkerTemplate(this_t &&other) = default;
        NonTerminalWalkerTemplate(const base_t &other);
        NonTerminalWalkerTemplate(base_t &&other);

        NonTerminalWalkerTemplate(node_ptr_t start, node_ptr_t finish) :
                base_t(start), finish_(finish) { ++(*this); }

        NonTerminalWalkerTemplate(node_ptr_t start) : this_t(start, start) {}

        this_t &operator=(const this_t &other) = default;
        this_t &operator=(this_t &&other) = default;
        this_t &operator=(const base_t &other);
        this_t &operator=(base_t &&other);

        explicit operator bool() const
                { return base_t::operator bool() && node() != finish_; }

        node_t &operator*() const     { return *node(); }
        node_ptr_t operator->() const { return node(); }

        this_t &operator++();
        this_t operator++(int);  // NB: potentially expensive!

        node_ptr_t node() const   { return base_t::node(); }
        node_ptr_t start() const  { return base_t::start(); }
        node_ptr_t finish() const { return finish_; }
        this_t begin() const      { return *this; }
        this_t end() const        { return this_t(start(), finish_, 0); }

        void reset(node_ptr_t new_start)
                { base_t::reset(new_start); ++(*this); }

        void reset() { reset(start()); }

        bool operator==(const this_t &other) const
                { return node() == other.node(); }

        bool operator!=(const this_t &other) const
                { return node() != other.node(); }

private:
        NonTerminalWalkerTemplate(node_ptr_t start, node_ptr_t finish, int) :
                base_t(start), finish_(finish) {}  // helper for end()

        node_ptr_t finish_;
};

// instantiated by library
extern template class NonTerminalWalkerTemplate<SPPFNode>;
extern template class NonTerminalWalkerTemplate<const SPPFNode>;

using NonTerminalWalker = NonTerminalWalkerTemplate<SPPFNode>;
using NonTerminalConstWalker = NonTerminalWalkerTemplate<const SPPFNode>;

//--------------------------------------

inline NonTerminalWalker nonTerminals(SPPFNode::Ptr under)
        { return NonTerminalWalker(under); }

inline NonTerminalConstWalker nonTerminals(SPPFNode::ConstPtr under)
        { return NonTerminalConstWalker(under); }

inline NonTerminalWalker nonTerminals(SPPFNode &under)
        { return NonTerminalWalker(&under); }

inline NonTerminalConstWalker nonTerminals(const SPPFNode &under)
        { return NonTerminalConstWalker(&under); }

WRPARSE_API size_t countNonTerminals(const SPPFNode &under);

inline size_t countNonTerminals(SPPFNode::ConstPtr under)
        { return countNonTerminals(*under); }

//--------------------------------------

template <typename NodeT>
class WRPARSE_API SubProductionWalkerTemplate :
        protected NonTerminalWalkerTemplate<NodeT>
{
public:
        using this_t = SubProductionWalkerTemplate;
        using base_t = NonTerminalWalkerTemplate<NodeT>;
        using node_t = typename base_t::node_t;
        using node_ptr_t = typename base_t::node_ptr_t;

        SubProductionWalkerTemplate() = default;

        SubProductionWalkerTemplate(const this_t &other) = default;
        SubProductionWalkerTemplate(this_t &&other) = default;
        SubProductionWalkerTemplate(const base_t &other);
        SubProductionWalkerTemplate(base_t &&other);
        SubProductionWalkerTemplate(node_ptr_t start, node_ptr_t finish);
        SubProductionWalkerTemplate(node_ptr_t start) : this_t(start, start) {}

        this_t &operator=(const this_t &other) = default;
        this_t &operator=(this_t &&other) = default;
        this_t &operator=(const base_t &other);
        this_t &operator=(base_t &&other);

        explicit operator bool() const { return base_t::operator bool(); }

        node_t &operator*() const     { return *node(); }
        node_ptr_t operator->() const { return node(); }

        this_t &operator++()   { base_t::operator++(); return *this; }
        this_t operator++(int) { return base_t::operator++(0); }
                // NB: potentially expensive!

        node_ptr_t node() const   { return base_t::node(); }
        node_ptr_t start() const  { return base_t::start(); }
        node_ptr_t finish() const { return base_t::finish(); }
        this_t begin() const      { return *this; }
        this_t end() const        { return base_t::end(); }

        void reset(node_ptr_t new_start);
        void reset() { reset(start()); }

        bool operator==(const this_t &other) const
                { return base_t::operator==(other); }

        bool operator!=(const this_t &other) const
                { return base_t::operator!=(other); }

private:
        void bypassIdenticalChildren();
};

// instantiated by library
extern template class SubProductionWalkerTemplate<SPPFNode>;
extern template class SubProductionWalkerTemplate<const SPPFNode>;

using SubProductionWalker = SubProductionWalkerTemplate<SPPFNode>;
using SubProductionConstWalker = SubProductionWalkerTemplate<const SPPFNode>;

//--------------------------------------

inline SubProductionWalker subProductions(SPPFNode::Ptr under)
        { return SubProductionWalker(under); }

inline SubProductionConstWalker subProductions(SPPFNode::ConstPtr under)
        { return SubProductionConstWalker(under); }

inline SubProductionWalker subProductions(SPPFNode &under)
        { return SubProductionWalker(&under); }

inline SubProductionConstWalker subProductions(const SPPFNode &under)
        { return SubProductionConstWalker(&under); }


} // namespace parse
} // namespace wr


#endif // !WRPARSE_SPPF_H
