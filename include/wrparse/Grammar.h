/**
 * \file Grammar.h
 *
 * \brief Classes and functions for defining language grammars
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
#ifndef WRPARSE_GRAMMAR_H
#define WRPARSE_GRAMMAR_H

#include <algorithm>
#include <iosfwd>
#include <map>
#include <set>
#include <type_traits>
#include <vector>

#include <wrutil/circ_fwd_list.h>
#include <wrparse/Config.h>
#include <wrparse/Token.h>


namespace wr {
namespace parse {


class Lexer;       // see Lexer.h
class ParseState;  // see Parser.h
class Rule;        // see below
class Production;  // see below

/**
 * \brief Grammar rule component data type
 */
class WRPARSE_API Component
{
public:
        using this_t = Component;
        using Predicate = bool (*)(ParseState &);

        Component();

        /* it must not be possible to implicitly convert bool to Component
           otherwise prerequisites at the end of rule specifications will be
           misinterpreted */

        template <typename T,
                  typename U = typename std::enable_if<
                                    std::is_enum<T>::value
                                            && (sizeof(T) <= sizeof(TokenKind)),
                                    std::nullptr_t>::type>
        Component(
                T         terminal,
                bool      is_optional = false,
                Predicate predicate   = nullptr,
                U                     = {}
        ) :
                this_t(TokenKind(terminal), is_optional, predicate)
        {
        }

        explicit Component(TokenKind terminal, bool is_optional = false,
                           Predicate predicate = nullptr);

        Component(const Production &nonterminal, bool is_optional = false,
                  Predicate predicate = nullptr);

        Component(Predicate predicate);
        Component(const this_t &other) = default;
        Component(this_t &&other) = default;

        this_t &operator=(const this_t &other) = default;
        this_t &operator=(this_t &&other) = default;

        Predicate predicate() const { return predicate_; }
        Rule *rule()                { return rule_; }
        const Rule *rule() const    { return rule_; }
        bool isTerminal() const     { return is_terminal_; }
        bool isNonTerminal() const  { return !is_terminal_; }
        bool isOptional() const     { return is_optional_; }
        bool isRecursive() const;
        int index() const;

        TokenKind getAsTerminal() const
                { return isTerminal() ? terminal_ : TOK_NULL; }

        const Production *getAsNonTerminal() const
                { return isTerminal() ? nullptr : nonterminal_; }

        bool operator==(const this_t &other) const;
        bool operator!=(const this_t &other) const;
        bool operator<(const this_t &other) const;

        void dump(std::ostream &to, const Lexer &lexer) const;
        void gdb(const Lexer &lexer) const;

private:
        friend Rule;
        union
        {
                TokenKind         terminal_;
                const Production *nonterminal_;
        };
        Predicate  predicate_;
        Rule      *rule_;
        uint8_t    is_terminal_ : 1,
                   is_optional_ : 1;
};

static_assert(alignof(Component) >= 4,
              "Component requires alignment of 4 or more");

//--------------------------------------
/**
 * \brief Grammar rule data type
 */
class WRPARSE_API Rule :
        std::vector<Component>
{
        using base_t = std::vector<Component>;
public:
        using this_t = Rule;
        using const_iterator = base_t::const_iterator;

        Rule(std::initializer_list<Component> init, bool enable = true);

        Rule(std::initializer_list<Component> init, const this_t *&out_ptr,
             bool enable = true) : this_t(init, enable) { out_ptr = this; }

        Rule(const this_t &other);
        Rule(this_t &&other);

        this_t &operator=(const this_t &other);
        this_t &operator=(this_t &&other);

        const Production *production() const { return production_; }

        int index() const;
        int indexOf(const Component &c) const;
        bool isLeftRecursive() const;
        bool isRecursive() const;
        bool isDelegate() const;
        bool isEnabled() const              { return enabled_; }
        bool mustHide() const;
        bool matchesEmpty() const;

        bool empty() const           { return base_t::size() == 1; }
        size_t size() const          { return base_t::size() - 1; }
        const_iterator begin() const { return base_t::begin(); }
        const_iterator end() const   { return base_t::end() - 1; }
                                                // dummy component at end

        const Component &operator[](size_t pos) const
                { return base_t::operator[](pos); }

        const Component &front() const { return base_t::front(); }
        const Component &back() const;

        void dump(std::ostream &to, const Lexer &lexer) const;
        void gdb(const Lexer &lexer) const;

        bool operator==(const this_t &rhs) const { return this == &rhs; }
        bool operator!=(const this_t &rhs) const { return this != &rhs; }

private:
        friend Production;

        using iterator = base_t::iterator;

        iterator begin() { return base_t::begin(); }
        iterator end()   { return base_t::end(); }

        void updateComponents();

        const Production *production_;
        bool              enabled_ : 1;
};

static_assert(alignof(Rule) >= 4, "Rule requires alignment of 4 or more");

using Rules = std::vector<Rule>;
using RuleIndices = circ_fwd_list<size_t>;

//--------------------------------------
/**
 * \brief Grammar production data type
 *
 * Productions are the primary element of all grammars. They represent a named
 * set of one or more rules, specifiable as nonterminal rule components and
 * are used as the starting point for a parsing operation.
 *
 * \see class `Rule`, class `Component`
 */
class WRPARSE_API Production :
        std::vector<Rule>
{
public:
        using this_t = Production;
        using base_t = std::vector<Rule>;
        using const_iterator = base_t::const_iterator;

        enum
        {
                TRANSPARENT      = 1U,
                HIDE_IF_DELEGATE = 1U << 1,
                KEEP_RECURSION   = 1U << 2
        };

        /* used below to prevent an initialiser list for a single
           one-terminal-item rule being misinterpreted as the flags
           constructor argument (happens with clang 3.5 which misreports
           the ambiguity in operator= not the constructor) */
        struct Flags
        {
                Flags() : flags_(0) {}
                Flags(unsigned flags) : flags_(flags) {}

                operator unsigned int() const { return flags_; }

                unsigned flags_;
        };

        using Action = bool (*)(ParseState &);

        Production();
        Production(const this_t &other);
        Production(this_t &&other);
        Production(const char * const name) : this_t(name, true, {}, 0) {}
        Production(const char * const name, bool enable, Rules rules,
                   Flags flags = {});

        Production(const char * const name, Rules rules, Flags flags = {}) :
                this_t(name, true, std::move(rules), flags) {}

        ~Production() = default;

        this_t &operator=(const this_t &other);
        this_t &operator=(this_t &&other);

        this_t &operator+=(const Rules &other);
        this_t &operator+=(Rules &&other);

        const char *name() const    { return name_; }
        bool isTransparent() const  { return is_transparent_; }
        bool hideIfDelegate() const { return hide_if_delegate_; }
        bool keepRecursion() const  { return keep_recursion_; }
        bool matchesEmpty() const;
        bool isLL1() const;
        int indexOf(const Rule &rule) const;

        bool empty() const           { return base_t::empty(); }
        size_t size() const          { return base_t::size(); }
        const_iterator begin() const { return base_t::begin(); }
        const_iterator end() const   { return base_t::end(); }

        const Rule &operator[](size_t pos) const
                { return base_t::operator[](pos); }

        const Rule &front() const { return base_t::front(); }
        const Rule &back() const  { return base_t::back(); }

        using Terminals = std::map<TokenKind, RuleIndices>;

        const Terminals &initialTerminals() const;

        bool operator==(const this_t &rhs) const { return this == &rhs; }
        bool operator!=(const this_t &rhs) const { return this != &rhs; }

        void addPreParseAction(Action action) const
                { pre_parse_actions_.push_back(std::move(action)); }
        void addPostParseAction(Action action) const
                { post_parse_actions_.push_front(std::move(action)); }
        bool removePreParseAction(Action action) const
                { return removeAction(action, pre_parse_actions_); }
        bool removePostParseAction(Action action) const
                { return removeAction(action, post_parse_actions_); }
        bool invokePreParseActions(ParseState &state) const
                { return invokeActions(pre_parse_actions_, state); }
        bool invokePostParseActions(ParseState &state) const
                { return invokeActions(post_parse_actions_, state); }

        void dump(std::ostream &to, const Lexer &lexer) const;
        void gdb(const Lexer &lexer) const;

private:
        void initRules(size_t from_pos = 0);

        enum class InitTerminalsStatus { OK, IS_LR, INDETERMINATE };

        InitTerminalsStatus initTerminalsAndLL1(
                        std::set<const this_t *> &visited) const;

        InitTerminalsStatus initTerminalsAndLL1(
                        std::set<const this_t *> &visited,
                        const Rule &rule) const;

        void updateTerminalsAndLL1(TokenKind t, const Rule &rule) const;

        using ActionList = circ_fwd_list<Action>;

        static bool removeAction(Action target, ActionList &from);
        static bool invokeActions(const ActionList &in, ParseState &state);


        const char *      name_;
        mutable Terminals initial_terminals_;

        union {
                struct {
                        mutable bool got_initial_terminals_ : 1,
                                     is_ll1_                : 1,
                                     matches_empty_         : 1;
                        bool         is_transparent_        : 1,
                                     hide_if_delegate_      : 1,
                                     keep_recursion_        : 1;
                };
                uint8_t flags_;
        };

        mutable ActionList pre_parse_actions_,
                           post_parse_actions_;
};

static_assert(alignof(Production) >= 4,
              "Production requires alignment of 4 or more");

//--------------------------------------

inline Component opt(TokenKind terminal)
        { return Component(terminal, true); }

inline Component opt(const Production &nonterminal)
        { return Component(nonterminal, true); }

inline Component pred(TokenKind terminal, Component::Predicate predicate)
        { return Component(terminal, false, predicate); }

inline Component pred(const Production &nonterminal,
                      Component::Predicate predicate)
        { return Component(nonterminal, false, predicate); }


} // namespace parse
} // namespace wr


#endif // !WRPARSE_GRAMMAR_H
