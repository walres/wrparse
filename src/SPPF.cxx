/**
 * \file SPPF.cxx
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
 */
#include <assert.h>
#include <fstream>
#include <stdexcept>
#include <wrutil/CityHash.h>
#include <wrutil/uiostream.h>

#include <wrparse/SPPF.h>
#include <wrparse/SPPFOutput.h>
#include <wrparse/Token.h>


using namespace std;


namespace wr {
namespace parse {


WRPARSE_API AuxData::~AuxData() = default;

//--------------------------------------

WRPARSE_API
SPPFNode::SPPFNode(
        this_t &&other
) :
        bits_      (other.bits_),
        bits2_     (other.bits2_),
        last_token_(other.last_token_),
        aux_data_  (std::move(other.aux_data_)),
        children_  (std::move(other.children_))
{
        other.bits2_ = 0;
}

//--------------------------------------

WRPARSE_API
SPPFNode::SPPFNode(
        const Production &nonterminal,
        Token            *first_token,
        Token            &last_token
) :
        nonterminal_(&nonterminal),
        first_token_(first_token),
        last_token_ (&last_token)
{
}

//--------------------------------------

WRPARSE_API
SPPFNode::SPPFNode(
        Token &terminal
) :
        bits_       (TERMINAL),
        first_token_(&terminal),
        last_token_ (&terminal)
{
}

//--------------------------------------

WRPARSE_API
SPPFNode::SPPFNode(
        const Component &slot,
        Token           &pivot,
        bool             empty
) :
        slot_       (&slot),
        first_token_(empty ? nullptr : &pivot),
        last_token_ (&pivot)
{
        bits_ |= PACKED;
}

//--------------------------------------

WRPARSE_API
SPPFNode::SPPFNode(
        const Component &slot,
        Token           *first_token,
        Token           &last_token
) :
        slot_       (&slot),
        first_token_(first_token),
        last_token_ (&last_token)
{
        bits_ |= INTERMEDIATE;
}

//--------------------------------------

WRPARSE_API
SPPFNode::~SPPFNode()
{
        if (bits2_ & 1) {
                // lowest bit of first_token_/bits2_ signifies ownership
                freeTokens();
        }
}

//--------------------------------------

auto
SPPFNode::emptyNode(
        Token &next
) -> this_t
{
        SPPFNode node(next);
        node.first_token_ = nullptr;  // signifies empty token
        return node;
}

//--------------------------------------

WRPARSE_API void
SPPFNode::takeTokens()
{
        bits2_ |= 1;
        if (!empty()) {
                last_token_->next(nullptr);
        }
}

//--------------------------------------

WRPARSE_API void
SPPFNode::freeTokens()
{
        for (Token *next, *i = firstToken(); i; i = next) {
                next = i != last_token_ ? i->next() : nullptr;
                delete i;
        }
}

//--------------------------------------

WRPARSE_API auto
SPPFNode::operator=(
        this_t &&other
) -> this_t &
{
        if (&other != this) {
                bits_ = other.bits_;
                if (bits2_ & 1) {
                        freeTokens();
                }
                bits2_ = other.bits2_;
                other.bits2_ = 0;
                aux_data_ = std::move(other.aux_data_);
                children_ = std::move(other.children_);
        }

        return *this;
}

//--------------------------------------

WRPARSE_API const Production *
SPPFNode::nonTerminal() const
{
        switch (kind()) {
        case NONTERMINAL:
                return nonterminal_;
        case INTERMEDIATE: case PACKED:
                return rule()->production();
        case TERMINAL:
                return nullptr;
        }
}

//--------------------------------------

WRPARSE_API TokenKind
SPPFNode::terminal() const
{
        if (!empty() && (kind() == TERMINAL)) {
                return firstToken()->kind();
        } else {
                return TOK_NULL;
        }
}

//--------------------------------------

WRPARSE_API const Rule *
SPPFNode::rule() const
{
        switch (kind()) {
        case NONTERMINAL: case TERMINAL:
                return nullptr;
        default:
                return component()->rule();
        }
}

//--------------------------------------

WRPARSE_API const Component *
SPPFNode::component() const
{
        switch (kind()) {
        case PACKED: case INTERMEDIATE:
                return reinterpret_cast<const Component *>(
                        bits_ & ~uintptr_t(3));
        default:
                return nullptr;
        }
}

//--------------------------------------

WRPARSE_API auto
SPPFNode::firstChild() -> Ptr
{
        return children_.empty() ? nullptr : children_.front();
}

//--------------------------------------

WRPARSE_API auto
SPPFNode::firstChild() const -> ConstPtr
{
        return children_.empty() ? nullptr : children_.front();
}

//--------------------------------------

WRPARSE_API auto
SPPFNode::lastChild() -> Ptr
{
        return children_.empty() ? nullptr : children_.back();
}

//--------------------------------------

WRPARSE_API auto
SPPFNode::lastChild() const -> ConstPtr
{
        return children_.empty() ? nullptr : children_.back();
}

//--------------------------------------

WRPARSE_API size_t
SPPFNode::countTokens() const
{
        size_t count = 0;

        if (!empty()) {
                ++count;
                for (auto i = firstToken(); i != last_token_; i = i->next()) {
                        ++count;
                }
        }

        return count;
}

//--------------------------------------

WRPARSE_API Token::Offset
SPPFNode::startOffset() const
{
        return empty() ? last_token_->offset() : firstToken()->offset();
}

//--------------------------------------

WRPARSE_API Token::Offset
SPPFNode::endOffset() const
{
        Token::Offset offset = last_token_->offset();

        if (!empty()) {
                offset += last_token_->bytes();
        }

        if (!children_.empty() && lastChild()->empty()) {
                offset = std::max(offset, lastChild()->endOffset());
        }

        return offset;
}

//--------------------------------------

WRPARSE_API string
SPPFNode::content(
        int max_tokens
) const
{
        const Token *t,
                    *next    = firstToken(),
                    *last    = last_token_;
        int          i       = 0;
        string       content;

        if (next) do {
                if ((max_tokens >= 0) && (i >= max_tokens)) {
                        content += "...";
                        break;
                }

                t = next;
                next = next->next();

                if ((i > 0)
                    && (t->flags() & (TF_SPACE_BEFORE | TF_STARTS_LINE))) {
                        content += ' ';
                }

                u8string_view spelling(t->spelling());
                content.append(spelling.char_data(), spelling.bytes());
                ++i;
        } while (t != last);

        return content;
}

//--------------------------------------

WRPARSE_API void
SPPFNode::dump() const
{
        uerr << *this;
}

//--------------------------------------

WRPARSE_API bool
SPPFNode::operator==(
        const this_t &other
) const
{
        return (&other == this) ||
                ((bits_ == other.bits_) && (firstToken() == other.firstToken())
                 && (last_token_ == other.last_token_));
}

//--------------------------------------

WRPARSE_API bool
SPPFNode::is(
        const this_t &other
) const
{
        return (firstToken() == other.firstToken())
                && (last_token_ == other.last_token_);
}

//--------------------------------------

WRPARSE_API bool
SPPFNode::is(
        TokenKind terminal
) const
{
        return firstToken() && (firstToken() == last_token_)
                            && (firstToken()->is(terminal));
}

//--------------------------------------

template <typename NodeT> inline bool
sppfNodeIs(
        NodeT                       &node,
        const Production            &nonterminal,
        boost::intrusive_ptr<NodeT> *out_pos
)
{
        if (node.nonTerminal() == &nonterminal) {
                if (out_pos) {
                        *out_pos = &node;
                }
                return true;
        }

        for (auto walker = nonTerminals(node); walker && walker->is(node);
                                               walker.reset(walker.node())) {
                if (walker->nonTerminal() == &nonterminal) {
                        if (out_pos) {
                                *out_pos = walker.node();
                        }
                        return true;
                }
        }

        return false;
}

//--------------------------------------

WRPARSE_API bool
SPPFNode::is(
        const Production &nonterminal
) const
{
        return sppfNodeIs<const this_t>(*this, nonterminal, nullptr);
}

//--------------------------------------

WRPARSE_API bool
SPPFNode::is(
        const Production &nonterminal,
        Ptr              &out_pos
)
{
        return sppfNodeIs(*this, nonterminal, &out_pos);
}

//--------------------------------------

WRPARSE_API bool
SPPFNode::is(
        const Production &nonterminal,
        ConstPtr         &out_pos
) const
{
        return sppfNodeIs(*this, nonterminal, &out_pos);
}

//--------------------------------------

WRPARSE_API auto
SPPFNode::find(
        const Production &nonterminal,
        int               max_depth
) -> Ptr
{
        Ptr found;

        if (!this->is(nonterminal, found) && max_depth) {
                for (this_t &child: subProductions(*this)) {
                        if (child.is(nonterminal, found)) {
                                return found;
                        }
                }

                if (--max_depth) {
                        for (this_t &child: subProductions(*this)) {
                                found = child.find(nonterminal, max_depth);
                                if (found) {
                                        break;
                                }
                        }
                }
        }

        return found;
}

//--------------------------------------

WRPARSE_API void
SPPFNode::addChild(
        Ptr other
)
{
        if (other == this) {
                throw std::logic_error("SPPFNode::appendChild(): other == this");
        } else if (other->isPacked() && !children_.empty()) {
                // ambiguous match - prepend instead to ensure it gets looked at
                children_.push_front(other);
        } else {
                children_.push_back(other);
        }
}

//--------------------------------------

WRPARSE_API const size_t
SPPFNode::hash() const
{
        struct
        {
                uintptr_t    bits;
                const Token *first_token,
                            *last_token;
        } key = { bits_, firstToken(), lastToken() };

        return stdHash(&key, sizeof(key));
}


//--------------------------------------

WRPARSE_API bool
SPPFNode::writeDOTGraphFile(
        const char *file_name
) const
{
        std::ofstream output(file_name);

        if (output.is_open()) {
                writeDOTGraph(output);
                return true;
        } else {
                return false;
        }
}

//--------------------------------------

WRPARSE_API void
SPPFNode::writeDOTGraph(
        std::ostream &output
) const
{
        output << "digraph {\n"
               << "    graph [ordering=out]\n";

        writeDOTNodes(output);

        output << "}\n";
}

//--------------------------------------

WRPARSE_API void
SPPFNode::writeDOTNodes(
        std::ostream &output
) const
{
        writeDOTNode(output);

        for (ConstPtr child: children()) {
                child->writeDOTNodes(output);
        }
}

//--------------------------------------

WRPARSE_API void
SPPFNode::writeDOTNode(
        std::ostream &output
) const
{
        output << 'N' << this << " [label=\"" << this;

        switch (kind()) {
        case TERMINAL:
                output << "\\n" << startOffset() << ' ';
                if (empty()) {
                        output << "(empty)";
                } else {
                        output << '\'' << firstToken()->spelling() << '\'';
                }
                output << '"';
                break;
        case NONTERMINAL:
                output << "\\n" << nonTerminal()->name() << "\\n";
                if (empty()) {
                        output << "(empty @ " << startOffset() << ')';
                } else {
                        output << startOffset() << " '"
                               << firstToken()->spelling()
                               << "' - " << endOffset() << " '"
                               << lastToken()->spelling() << '\'';
                }
                output << '"';
                break;
        case INTERMEDIATE:
                output << "\\n";
                if (nonTerminal()) {
                        output << nonTerminal()->name() << '.'
                               << rule()->index();
                        if (component()) {
                                 output << '[' << component()->index() << ']';
                        }
                } else {
                        output << '?';
                }

                output << "\\n";

                if (empty()) {
                        output << "(empty @ " << startOffset() << ')';
                } else {
                        output << startOffset() << " '"
                               << firstToken()->spelling() << "' - "
                               << endOffset() << " '"
                               << lastToken()->spelling() << '\'';
                }
                output << "\";shape=box";
                break;
        case PACKED:
                output << "\";shape=point";
                break;
        }

        output << "]\n   ";

        for (ConstPtr child: children()) {
                output << " N" << this << " -> N" << child;
                if (child->isPacked()) {
                        output << " [headlabel=\"" << child << "\"]";
                }
                output << ';';
        }
}

//--------------------------------------

template <typename NodeT> WRPARSE_API auto
SPPFWalkerTemplate<NodeT>::node() const -> node_ptr_t
{
        return trail_.empty() ? start_ : *trail_.front();
}

//--------------------------------------

template <typename NodeT> WRPARSE_API bool
SPPFWalkerTemplate<NodeT>::walkLeft(
        node_ptr_t stop_at
)
{
        auto pos = node();

        if (pos->hasChildren()) {
                trail_.push_front(pos->children().begin());
                return true;
        } else {
                auto prev = pos;

                while ((pos != stop_at) && backtrack()) {
                        pos = node();
                        if (pos->children().front() != prev) {
                                trail_.push_front(pos->children().begin());
                                return true;
                        }
                        prev = pos;
                }

                return false;
        }
}

//--------------------------------------

template <typename NodeT> WRPARSE_API bool
SPPFWalkerTemplate<NodeT>::walkRight(
        node_ptr_t stop_at
)
{
        auto pos = node();

        if (pos->hasChildren()) {
                auto i = pos->children().begin();

                if (!(*i)->isPacked()) {
                        if (i != pos->children().last()) {
                                ++i;
                        }
                }

                trail_.push_front(i);
                return true;
        } else {
                auto prev = pos;

                while ((pos != stop_at) && backtrack()) {
                        pos = node();
                        auto i = pos->children().begin();

                        if (i != pos->children().last()) {
                                ++i;
                        }

                        if (*i != prev) {
                                trail_.push_front(i);
                                return true;
                        }

                        prev = pos;
                }

                return false;
        }
}

//--------------------------------------

template <typename NodeT> WRPARSE_API bool
SPPFWalkerTemplate<NodeT>::backtrack()
{
        if (trail_.empty()) {
                return false;
        }

        trail_.pop_front();
        return true;
}

//--------------------------------------

template <typename NodeT> WRPARSE_API void
SPPFWalkerTemplate<NodeT>::reset(
        node_ptr_t new_start
)
{
        assert(new_start);
        trail_.clear();
        start_ = new_start;
}

//--------------------------------------

template <typename NodeT> WRPARSE_API void
SPPFWalkerTemplate<NodeT>::extend(
        this_t &&other
)
{
        if (other.start() != node()) {
                throw std::logic_error("SPPFWalkerTemplate::extend(): other.start() != node()");
        }

        trail_.splice_after(trail_.before_begin(), std::move(other.trail_));
}

//--------------------------------------

template class SPPFWalkerTemplate<SPPFNode>;
template class SPPFWalkerTemplate<const SPPFNode>;

//--------------------------------------

template <typename NodeT> WRPARSE_API
NonTerminalWalkerTemplate<NodeT>::NonTerminalWalkerTemplate(
        const base_t &other
) :
        base_t (other),
        finish_(start())
{
}

//--------------------------------------

template <typename NodeT> WRPARSE_API
NonTerminalWalkerTemplate<NodeT>::NonTerminalWalkerTemplate(
        base_t &&other
) :
        base_t (std::move(other)),
        finish_(start())
{
}

//--------------------------------------

template <typename NodeT> WRPARSE_API auto
NonTerminalWalkerTemplate<NodeT>::operator=(
        const base_t &other
) -> this_t &
{
        if (&other != this) {
                base_t::operator=(other);
                finish_ = start();
        }
        return *this;
}

//--------------------------------------

template <typename NodeT> WRPARSE_API auto
NonTerminalWalkerTemplate<NodeT>::operator=(
        base_t &&other
) -> this_t &
{
        if (&other != this) {
                base_t::operator=(std::move(other));
                finish_ = start();
        }
        return *this;
}

//--------------------------------------

template <typename NodeT> WRPARSE_API auto
NonTerminalWalkerTemplate<NodeT>::operator++() -> this_t &
{
        auto pos = node();

        do {
                if (pos == start()) {
                        if (!base_t::walkLeft(finish_)) {
                                break;
                        }
                } else if (pos->isSymbol()) {
                        while (true) {
                                if (!base_t::backtrack()) {
                                        return *this;
                                }

                                auto prev = pos;
                                pos = node();

                                /* 1. packed children represent separate parses,
                                      multiple such children represent
                                      ambiguities (not a binarised part of the
                                      structure)
                                   2. don't walkRight() if only one child
                                      (result same as walkLeft())
                                   3. don't walkRight() if prev was not the
                                      first (i.e. left-hand) child, this means
                                      we have backtracked up from the right */
                                if (!prev->isPacked()
                                           && (pos->children().begin()
                                                != pos->children().last())
                                           && (prev == pos->children().front())) {
                                        base_t::walkRight(finish_);
                                        break;
                                }
                        }
                } else if (!base_t::walkLeft(pos)) {
                        if (!base_t::walkRight(finish_)) {
                                return *this;
                        }
                }

                pos = node();
        } while ((pos != finish_) && !pos->isNonTerminal());

        return *this;
}

//--------------------------------------

template <typename NodeT> WRPARSE_API auto
NonTerminalWalkerTemplate<NodeT>::operator++(int) -> this_t
{
        this_t orig(*this);
        ++(*this);
        return orig;
}

//--------------------------------------

template class NonTerminalWalkerTemplate<SPPFNode>;
template class NonTerminalWalkerTemplate<const SPPFNode>;

//--------------------------------------

WRPARSE_API size_t
countNonTerminals(
        const SPPFNode &under
)
{
        size_t count = 0;
        for (auto &i: nonTerminals(under)) {
                (void) i;  // silence unused variable warning
                ++count;
        }
        return count;
}

//--------------------------------------

template <typename NodeT> WRPARSE_API
SubProductionWalkerTemplate<NodeT>::SubProductionWalkerTemplate(
        const base_t &other
) :
        base_t(other)
{
        bypassIdenticalChildren();
}

//--------------------------------------

template <typename NodeT> WRPARSE_API
SubProductionWalkerTemplate<NodeT>::SubProductionWalkerTemplate(
        base_t &&other
) :
        base_t(std::move(other))
{
        bypassIdenticalChildren();
}

//--------------------------------------

template <typename NodeT> WRPARSE_API
SubProductionWalkerTemplate<NodeT>::SubProductionWalkerTemplate(
        node_ptr_t start,
        node_ptr_t finish
) :
        base_t(start, finish)
{
        bypassIdenticalChildren();
}

//--------------------------------------

template <typename NodeT> WRPARSE_API auto
SubProductionWalkerTemplate<NodeT>::operator=(
        const base_t &other
) -> this_t &
{
        if (&other != this) {
                base_t::operator=(other);
                bypassIdenticalChildren();
        }
        return *this;
}

//--------------------------------------

template <typename NodeT> WRPARSE_API auto
SubProductionWalkerTemplate<NodeT>::operator=(
        base_t &&other
) -> this_t &
{
        if (&other != this) {
                base_t::operator=(std::move(other));
                bypassIdenticalChildren();
        }
        return *this;
}

//--------------------------------------

template <typename NodeT> WRPARSE_API void
SubProductionWalkerTemplate<NodeT>::reset(
        node_ptr_t new_start
)
{
        base_t::reset(new_start);
        bypassIdenticalChildren();
}

//--------------------------------------

template <typename NodeT> WRPARSE_API void
SubProductionWalkerTemplate<NodeT>::bypassIdenticalChildren()
{
        if (*this && node()->is(*start())) {
                auto pos = node();
                for (base_t child(pos); child && child->is(*pos);
                                        child.reset(pos = node())) {
                        this->extend(std::move(child));
                }
        }
}

//--------------------------------------

template class SubProductionWalkerTemplate<SPPFNode>;
template class SubProductionWalkerTemplate<const SPPFNode>;


} // namespace parse
} // namespace wr
