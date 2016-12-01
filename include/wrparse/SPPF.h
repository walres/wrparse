/**
 * \file wrparse/SPPF.h
 *
 * \brief Data types for representation and traversal of Shared Packed
 *      Parse Forests (SPPFs)
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
 * \detail A Shared Packed Parse Forest (SPPF) represents all possible
 *      traversals of a grammar for a given sequence of input tokens, including
 *      ambiguities -- an extension of the concept of a parse tree. An SPPF is
 *      made up of \i nodes - objects of the \c SPPFNode class - each of which
 *      details what part of the input token stream they were matched against
 *      (zero or more tokens), what point in the grammar they stem from (for
 *      packed / intermediate nodes), which parsed nonterminal or token type
 *      they refer to (for nonterminal or terminal symbol nodes), zero or more
 *      children plus any auxiliary data the user wishes to attach to them.
 *
 *      SPPF nodes come in three basic incarnations:
 *
 *      \li <i>Symbol nodes</i> representing a matched terminal (one token of
 *              a given type) or a matched nonterminal (a set of zero or more
 *              tokens). Terminal symbol nodes do not have child nodes. A
 *              symbol node is "labelled" with the terminal or nonterminal it
 *              matched along with the range of tokens it covers (which may
 *              be empty)
 *      \li <i>Intermediate nodes</i> representing a partially-matched
 *              nonterminal; these nodes are a necessary part of "binarising"
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
 * \headerfile SPPF.h <wrparse/SPPF.h>
 *
 * Users of the wrparse library can associate data directly with SPPF nodes
 * by overriding the `AuxData` class to add extra members, then use
 * `SPPFNode::setAuxData()` to attach their `AuxData`-derived object to the
 * desired node. `SPPFNode::auxData()` is then used to recall the data.
 *
 * \note This class is reference-counted for automatic memory management;
 *      instances attached to `SPPFNode` objects must always be created on
 *      the heap and not on the stack.
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
 * \headerfile SPPF.h <wrparse/SPPF.h>
 *
 * Shared Packed Parse Forests represent all possible grammar traversals
 * for a given sequence of input tokens, including ambiguities -- an
 * extension of the concept of a parse tree. An SPPF is made up of *nodes* -
 * objects of the `SPPFNode` class - each of which details what part of the
 * input token stream they were matched against (zero or more tokens), what
 * part of the grammar they matched (for packed / intermediate nodes), which
 * parsed nonterminal or token type they refer to (for nonterminal or
 * terminal symbol nodes), zero or more children plus any auxiliary data the
 * user attaches to them.
 *
 * SPPF nodes come in three basic incarnations:
 *
 * * *Symbol* nodes represent a matched terminal (one token of a given
 *      type) or a matched nonterminal (a set of zero or more tokens).
 *      Terminal symbol nodes do not have child nodes. A symbol node is
 *      "labelled" with the terminal or nonterminal it matched along with
 *      the range of tokens it covers (which may be empty).
 *
 * * *Intermediate* nodes represent a partially-matched nonterminal. These
 *      nodes are a necessary part of "binarising" the SPPF to ensure the
 *      GLL parsing algorithm is of cubic complexity at most. An
 *      intermediate node is "labelled" with a specific point in the grammar
 *      that it is associated with and the range of tokens covered by its
 *      children.
 *
 * * *Packed* nodes representing one complete parse (a 'fork') of the entity
 *      represented by its parent. Packed nodes have at most two children
 *      and also serve for "binarising" the SPPF. A packed node is
 *      "labelled" with a specific point in the grammar (as is an
 *      intermediate node) and a 'pivot' position representing the point in
 *      the input token stream where its left child ends and its right child
 *      begins (or where its only child begins, if there is just one).
 *
 * Some important rules to remember:
 *
 * * Nonterminal symbol nodes and intermediate nodes may have packed
 *      children or other kinds of children, but never packed children mixed
 *      with other kinds.
 *
 * * Terminal symbol nodes never have children.
 *
 * * If a symbol node or intermediate node has packed children, there will
 *      be as many packed children as there were successful parses made by
 *      tracing distinct paths through the grammar for the same set of
 *      tokens. In short: more than one packed child means that ambiguities
 *      exist.
 *
 * * Symbol or intermediate child nodes have be a maximum of two children.
 *
 * * Packed nodes only have symbol or intermediate nodes as children.
 *
 * Put another way, symbol nodes correspond to each matched fragment of the
 * grammar. Intermediate nodes tie multiple symbol nodes together and link
 * them to the actual points in the grammar they were matched against.
 * Packed nodes represent 'forks' taken through the grammar from the same
 * point (only one if there were no ambiguities).
 *
 * ### SPPF Traversal
 *
 * Several classes are available for traversing SPPFs in different ways:
 *
 * * `SPPFWalker` - the most basic; passes through the SPPF nodes literally
 *      from a given start point, giving the user the ability to "walk
 *      left", "walk right" or "backtrack" the way they came (but never
 *      back past the start position).
 * * `NonTerminalWalker` - hides some of the complexity of the SPPF in that
 *      it provides iteration over the nonterminal subcomponents of the
 *      starting node.
 * * `SubProductionWalker` - based on `NonTerminalWalker`, differs from it
 *      in that it initially descends through nonterminal subcomponents
 *      that cover the same range of tokens as the starting node until it
 *      finds a subcomponent that covers a strict subset of the starting
 *      node. After this it behaves the same as `NonTerminalWalker`.
 */
class WRPARSE_API SPPFNode :
        public boost::intrusive_ref_counter<SPPFNode>
{
public:
        using this_t = SPPFNode;
        using Ptr = boost::intrusive_ptr<this_t>;
                ///< Pointer to reference-counted mutable SPPF node
        using ConstPtr = boost::intrusive_ptr<const this_t>;
                ///< Pointer to reference-counted immutable SPPF node
        using ChildList = circ_fwd_list<Ptr>;
                ///< Type of an `SPPFNode`'s child list

        /// SPPF node types
        enum Kind
        {
                NONTERMINAL = 0,  ///< Nonterminal symbol node
                TERMINAL,         ///< Terminal symbol node
                PACKED,           ///< As implied
                INTERMEDIATE      ///< As implied
        };

        SPPFNode() = delete;
                ///< \brief Default construction prohibited
        SPPFNode(const this_t &) = delete;
                ///< \brief Copying prohibited

        /**
         * \brief Initialise with contents transferred from another object
         * \param [in,out] other  object to be transferred
         */
        SPPFNode(this_t &&other);

        /// \brief Initialise a nonterminal symbol node
        SPPFNode(const NonTerminal &nonterminal, Token *first_token,
                 Token &last_token);

        /// \brief Initialise a nonempty terminal symbol node
        SPPFNode(Token &terminal);

        /// \brief Initialise a packed node
        SPPFNode(const Component &slot, Token &pivot, bool empty);

        /// \brief Initialise an intermediate node
        SPPFNode(const Component &component, Token *first_token,
                 Token &last_token);

        ~SPPFNode();  ///< \brief Finalise object

        /// \brief Obtain an empty terminal symbol node
        static this_t emptyNode(Token &next);

        this_t &operator=(const this_t &other) = delete;
                ///< \brief Copying prohibited

        /**
         * \brief Transfer other object's contents to `*this`
         * \param [in,out] other  object to be transferred
         * \return reference to `*this` object
         */
        this_t &operator=(this_t &&other);

        /**
         * \name Auxiliary Data Management Functions
         *
         * These functions are marked `const` since they do not affect
         * parser operation.
         */
        ///@{
        /**
         * \brief Attach user-specific auxiliary data
         * \param [in] data
         *      pointer to auxiliary data, reference-counted hence shareable
         */
        void setAuxData(AuxData::Ptr data) const { aux_data_ = data; }

        /**
         * \brief Retrieve user-specific auxiliary data
         * \return pointer to auxiliary data previously set by a call to
         *      `setAuxData()` or `nullptr` if not set
         */
        AuxData::Ptr auxData() const { return aux_data_; }
        ///@}

        /**
         * \name Hierarchy Management Functions
         */
        ///@{
        /// \brief Retrieve mutable reference to the list of children
        ChildList &children()              { return children_; }
        /// \brief Retrieve immutable reference to the list of children
        const ChildList &children() const  { return children_; }

        /**
         * \brief Obtain pointer to mutable first child node
         * \return Pointer to first child node or `nullptr` if no children
         */        
        Ptr firstChild();

        /**
         * \brief Obtain pointer to immutable first child node
         * \return Pointer to first child node or `nullptr` if no children
         */
        ConstPtr firstChild() const;

        /**
         * \brief Obtain pointer to mutable last child node
         * \return Pointer to last child node or `nullptr` if no children
         */        
        Ptr lastChild();

        /**
         * \brief Obtain pointer to immutable last child node
         * \return Pointer to last child node or `nullptr` if no children
         */
        ConstPtr lastChild() const;

        /// \brief Determine whether node has any children
        bool hasChildren() const           { return !children_.empty(); }

        /// \brief Obtain number of children
        size_t countChildren() const       { return children_.size(); }

        /**
         * \brief Add a new child to this node
         *
         * If `other` is a packed node it will become the first child,
         * otherwise it will become the last child. This makes it easier
         * to ensure that post-parse actions will 'see' new ambiguous
         * matches.
         *
         * \param [in] other  the new child node
         *
         * \note `other` is not expected to already belong anywhere in
         *      this node's hierarchy
         *
         * \throw std::logic_error if `(&other == this)`
         */
        void addChild(Ptr other);
        ///@}

        /**
         * \name Matched Token Range Functions
         */
        ///@{
        /// \brief Obtain number of input tokens matched by node
        size_t countTokens() const;

        /// \brief Determine whether node matches an empty range of tokens
        bool empty() const                 { return !firstToken(); }

        /**
         * \brief Obtain pointer to first matched mutable token
         * \return If matched token range is nonempty, a pointer to the
         *      first token in that range
         * \return `nullptr` if the matched token range is empty
         */
        Token *firstToken();              // defined out-of-line below

        /**
         * \brief Obtain pointer to first matched immutable token
         * \return If matched token range is nonempty, a pointer to the
         *      first token in that range
         * \return `nullptr` if the matched token range is empty
         */
        const Token *firstToken() const;  // ditto

        /**
         * \brief Obtain pointer to last matched mutable token
         * \return If matched token range is nonempty, a pointer to the last
         *      token in that range
         * \return If matched token range is empty, a pointer to the token
         *      whose input position immediately follows this node
         */
        Token *lastToken()                 { return last_token_; }

        /**
         * \brief Obtain pointer to last matched immutable token
         * \return If matched token range is nonempty, a pointer to the last
         *       token in that range
         * \return If matched token range is empty, a pointer to the token
         *      whose input position immediately follows this node
         */
        const Token *lastToken() const     { return last_token_; }

        /**
         * \brief Obtain offset of first matched token from start of input
         * \return If matched token range is nonempty, the offset in bytes
         *      of the first matched token from the start of the input text
         * \return -1 if matched token range is empty
         */
        Token::Offset startOffset() const;

        /**
         * \brief Obtain offset of last matched token from start of input
         * \return If matched token range is nonempty, the offset in bytes
         *      of the last matched token from the start of the input text
         * \return If matched token range is empty, the offset in bytes of
         *      the token that immediately follows this node
         */
        Token::Offset endOffset() const;

        /// \brief Obtain number of bytes covered by input token range
        size_t size() const
                { return !empty() ? endOffset() - startOffset() : 0; }

        /// \brief Obtain copy of matched input content
        std::string content(int max_tokens = -1) const;
        ///@}

        /**
         * @name Informational, Debugging and Diagnostic Functions
         */
        ///@{
        /// \brief Obtain node's type
        Kind kind() const { return static_cast<Kind>(bits_ & 3); }

        /**
         * \brief Obtain pointer to linked nonterminal
         * \return For a nonterminal symbol node: a pointer to the
         *      `NonTerminal` object specified at initialisation time
         * \return For a packed or intermediate node: a pointer to the
         *      `NonTerminal` object which defines the `Rule` containing the
         *      grammar slot (`Component` object reference) specified at
         *      initialisation time
         * \return `nullptr` for terminal symbol node
         */
        const NonTerminal *nonTerminal() const;

        /**
         * \brief Obtain token type of terminal symbol node
         * \return For a nonempty terminal symbol node, the `TokenKind`
         *      describing the terminal type
         * \return `TOK_NULL` for all other node types
         */
        TokenKind terminal() const;

        /**
         * \brief Obtain enclosing rule from packed or intermediate node
         */
        const Rule *rule() const;

        /**
         * \brief Obtain referenced grammar slot from packed or
         *      intermediate node
         */
        const Component *component() const;

        /**
         * \brief Obtain a hash code for this node
         *
         * The hash code is generated from the node type, nonterminal,
         * grammar slot and matched token range. The list of children and
         * auxiliary user data are not involved.
         */
        const size_t hash() const;

        /**
         * \brief Compare two nodes for equality
         *
         * Two nodes are considered equal iff:
         *
         * * they are both nonterminal symbol nodes, they both refer to
         *   the same nonterminal and match the same range of input tokens
         * * they are both terminal symbol nodes and they both matched the
         *   same input token
         * * they are both packed symbol nodes, they both refer to the same
         *   grammar slot and have the same pivot
         * * they are both intermediate symbol nodes, they both refer to
         *   the same grammar slot and match the same range of input tokens
         *
         * \param [in] other  node on right-hand side of comparison operator
         *
         * \return `true` if the nodes are considered equal,
         *      `false` otherwise
         */
        bool operator==(const this_t &other) const;

        /**
         * \brief Compare two nodes for inequality
         * \param [in] other  node on right-hand side of comparison operator
         * \return `true` if the nodes are considered inequal,
         *      `false` otherwise
         * \see \c operator==()
         */
        bool operator!=(const this_t &other) const
                { return !operator==(other); }

        /**
         * \brief Output nonterminal symbol node hierarchy to the `wr::uerr`
         *      stream
         * \pre `*this` is a nonterminal symbol node
         */
        void dump() const;

        /**
         * Write this node's complete hierarchy to a Graphviz DOT file
         *
         * The file is created if it does not exist.
         *
         * \param [in] file_name  desired path of file
         * \return `true` if file successfully written, `false` otherwise
         */
        bool writeDOTGraphFile(const char *file_name) const;

        /**
         * Output this node's complete hierarchy in Graphviz DOT format
         * \param [in,out] output  data is written here
         */
        void writeDOTGraph(std::ostream &output) const;

        /**
         * \brief Helper function for `writeDOTGraph()`
         *
         * This is a recursive function that invokes `writeDOTNode()` for
         * this node and all children.
         *
         * \param [in,out] output  data is written here
         */
        void writeDOTNodes(std::ostream &output) const;

        /**
         * \brief Helper function for `writeDOTGraph()`
         *
         * Writes a single DOT node record for this SPPF node, plus DOT edge
         * records (but not node records) for its children.
         *
         * \param [in,out] output  data is written here
         */
        void writeDOTNode(std::ostream &output) const;
        ///@}

        bool isNonTerminal() const { return kind() == NONTERMINAL; }
                ///< \brief Test whether object is a nonterminal symbol node
        bool isTerminal() const { return kind() == TERMINAL; }
                ///< \brief Test whether object is a terminal symbol node
        bool isSymbol() const { return isNonTerminal() || isTerminal(); }
                ///< \brief Test whether object is any symbol node
        bool isPacked() const { return kind() == PACKED; }
                ///< \brief Test whether object is a packed node
        bool isIntermediate() const { return kind() == INTERMEDIATE; }
                ///< \brief Test whether object is an intermediate node

        bool is(const this_t &other) const;
                ///< \brief Test whether two nodes matched the same tokens
        bool is(TokenKind terminal) const;
                /**< \brief Test whether the node matched a single token of
                        the specified type */

        ///@{
        /**
         * \brief Test whether this node, or any nonterminal descendant
         *      matching the same range of tokens, matches the specified
         *      nonterminal
         * \param [in]  nonterminal  the nonterminal to search for
         * \param [out] out_pos      matching node, if found
         * \return `true` if any of the nodes searched matched
         *      `nonterminal`, `false` otherwise
         */
        bool is(const NonTerminal &nonterminal) const;
        bool is(const NonTerminal &nonterminal, Ptr &out_pos);
        bool is(const NonTerminal &nonterminal, ConstPtr &out_pos) const;
        ///@}

        ///@{
        /**
         * \brief Search the hierarchy below for the next nonterminal
         *      symbol node that matched the specified nonterminal
         *
         * The search is limited to the depth specified by `max_depth`.
         *
         * \param [in] nonterminal  the nonterminal to search for
         * \param [in] max_depth
         *      depth limit, where each increment counts as one level of
         *      nonterminal descendancy, not one level in the SPPF.
         *      A negative value indicates unlimited depth; zero
         *      indicates 'do not examine below `*this`'
         *
         * \return Pointer to target node if found, `nullptr` otherwise
         */
        Ptr find(const NonTerminal &nonterminal, int max_depth = -1);

        ConstPtr find(const NonTerminal &nonterminal, int max_depth = -1) const
            { return const_cast<this_t *>(this)->find(nonterminal, max_depth); }
        ///@}

        /// \brief Hash functor class
        struct Hash
        {
                /// \brief Obtain hash code from SPPF node reference
                size_t operator()(const SPPFNode &n) const { return n.hash(); }
                /// \brief Obtain hash node indirectly from SPPF node pointer
                size_t operator()(ConstPtr n) const        { return n->hash(); }
        };

        /// \brief Hash table key comparison functor
        struct IndirectEqual
        {
                /**
                 * \brief Compare two nodes' contents given their pointers
                 * \pre `a` and `b` are both non-null
                 */
                bool operator()(ConstPtr a, ConstPtr b) const
                        { return (*a) == (*b); }
        };

private:
        friend Parser;

        void takeTokens();
        void freeTokens();

        union // stores kind in lowest two bits
        {
                const NonTerminal *nonterminal_;  // for nonterminal symbol node
                const Component   *slot_;  // for intermediate or packed node
                uintptr_t          bits_;  // for easy access to low bits
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
/**
 * \brief Template class implementing basic SPPF traversal
 * \headerfile SPPF.h <wrparse/SPPF.h>
 * \see type `SPPFWalker`, type `SPPFConstWalker`,
 *      class `NonTerminalWalkerTemplate`, class `SubProductionWalkerTemplate`
 */
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

/**
 * \brief Instantiation of class `SPPFWalkerTemplate` for mutable SPPF nodes
 * \see class `SPPFWalkerTemplate`
 */
using SPPFWalker = SPPFWalkerTemplate<SPPFNode>;

/**
 * \brief Instantiation of class `SPPFWalkerTemplate` for immutable SPPF nodes
 * \see class `SPPFWalkerTemplate`
 */
using SPPFConstWalker = SPPFWalkerTemplate<const SPPFNode>;

//--------------------------------------
/**
 * \brief Template class implementing traversal of an SPPF node's
 *      nonterminal descendants
 * \headerfile SPPF.h <wrparse/SPPF.h>
 * \see function `nonTerminals()`, type `NonTerminalWalker`,
 *      type `NonTerminalConstWalker`, class `SPPFWalkerTemplate`,
 *      class `SubProductionWalkerTemplate`
 */
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

/**
 * \brief Instantiation of class `NonTerminalWalkerTemplate` for mutable
 *      SPPF nodes
 * \see function `nonTerminals()`, class `NonTerminalWalkerTemplate`
 */
using NonTerminalWalker = NonTerminalWalkerTemplate<SPPFNode>;
/**
 * \brief Instantiation of class `NonTerminalWalkerTemplate` for immutable
 *      SPPF nodes
 * \see function `nonTerminals()`, class `NonTerminalWalkerTemplate`
 */
using NonTerminalConstWalker = NonTerminalWalkerTemplate<const SPPFNode>;

//--------------------------------------
///@{
/**
 * \brief Obtain an object for traversing an `SPPFNode`'s nonterminal
 *      descendants
 * \param [in] under  root SPPF node to traverse from
 * \return a `NonTerminalWalkerTemplate` traversal object
 * \see type `NonTerminalWalker`, type `NonTerminalConstWalker`,
 *      class `NonTerminalWalkerTemplate`
 */
inline NonTerminalWalker nonTerminals(SPPFNode::Ptr under)
        { return NonTerminalWalker(under); }

inline NonTerminalConstWalker nonTerminals(SPPFNode::ConstPtr under)
        { return NonTerminalConstWalker(under); }

inline NonTerminalWalker nonTerminals(SPPFNode &under)
        { return NonTerminalWalker(&under); }

inline NonTerminalConstWalker nonTerminals(const SPPFNode &under)
        { return NonTerminalConstWalker(&under); }
///@}

///@{
/**
 * \brief Count the number of immediate nonterminal descendants under the
 *      specified SPPF node
 * \param [in] under  root SPPF node to search from
 * \return number of immediate nonterminal descendants
 */
WRPARSE_API size_t countNonTerminals(const SPPFNode &under);

inline size_t countNonTerminals(SPPFNode::ConstPtr under)
        { return countNonTerminals(*under); }
///@}

//--------------------------------------
/**
 * \brief Template class implementing traversal of an SPPF node's
 *      strict sub-productions
 * \headerfile SPPF.h <wrparse/SPPF.h>
 * \see function `subProductions()`, type `SubProductionWalker`,
 *      type `SubProductionConstWalker`, class `SPPFWalkerTemplate`,
 *      class `NonTerminalWalkerTemplate`
 */
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

/**
 * Instantiation of `SubProductionWalkerTemplate` for mutable SPPF nodes
 * \see function `subProductions()`, class `SubProductionWalkerTemplate`
 */
using SubProductionWalker = SubProductionWalkerTemplate<SPPFNode>;

/**
 * Instantiation of `SubProductionWalkerTemplate` for immutable SPPF nodes
 * \see function `subProductions()`, class `SubProductionWalkerTemplate`
 */
using SubProductionConstWalker = SubProductionWalkerTemplate<const SPPFNode>;

//--------------------------------------
///@{
/**
 * \brief Obtain an object for traversing an `SPPFNode`'s sub-productions
 * \param [in] under  root SPPF node to traverse from
 * \return a `SubProductionWalkerTemplate` traversal object
 * \see type `SubProductionWalker`, type `SubProductionConstWalker`,
 *      class `SubProductionWalkerTemplate`, function `nonTerminals()`
 */
inline SubProductionWalker subProductions(SPPFNode::Ptr under)
        { return SubProductionWalker(under); }

inline SubProductionConstWalker subProductions(SPPFNode::ConstPtr under)
        { return SubProductionConstWalker(under); }

inline SubProductionWalker subProductions(SPPFNode &under)
        { return SubProductionWalker(&under); }

inline SubProductionConstWalker subProductions(const SPPFNode &under)
        { return SubProductionConstWalker(&under); }
///@}


} // namespace parse
} // namespace wr


#endif // !WRPARSE_SPPF_H
