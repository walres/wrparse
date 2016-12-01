/**
 * \file Grammar.cxx
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
#include <wrparse/Config.h>
#include <wrutil/CityHash.h>
#include <wrutil/numeric_cast.h>
#include <wrutil/string_view.h>
#include <wrutil/uiostream.h>

#include <wrparse/Grammar.h>
#include <wrparse/Lexer.h>


namespace wr {
namespace parse {


WRPARSE_API
Component::Component() :
        terminal_   (TOK_NULL),
        predicate_  (nullptr),
        rule_       (nullptr),
        is_terminal_(true),
        is_optional_(false)
{
}

//--------------------------------------

WRPARSE_API
Component::Component(
        TokenKind terminal,
        bool      is_optional,
        Predicate predicate
) :
        terminal_   (terminal),
        predicate_  (predicate),
        rule_       (nullptr),
        is_terminal_(true),
        is_optional_(is_optional)
{
}

//--------------------------------------

WRPARSE_API
Component::Component(
        const NonTerminal &nonterminal,
        bool               is_optional,
        Predicate          predicate
) :
        nonterminal_(&nonterminal),
        predicate_  (predicate),
        rule_       (nullptr),
        is_terminal_(false),
        is_optional_(is_optional)
{
}

//--------------------------------------

WRPARSE_API
Component::Component(
        Predicate predicate
) :
        terminal_   (TOK_NULL),
        predicate_  (predicate),
        rule_       (nullptr),
        is_terminal_(true),
        is_optional_(false)
{
}

//--------------------------------------

WRPARSE_API bool
Component::isRecursive() const
{
        return rule_ && (rule_->nonTerminal() == getAsNonTerminal());
}

//--------------------------------------

WRPARSE_API int
Component::index() const
{
        return rule_ ? rule_->indexOf(*this) : -1;
}

//--------------------------------------

WRPARSE_API bool
Component::operator==(
        const this_t &other
) const
{
        if (this == &other) {
                return true;
        } else if (predicate_ == other.predicate_) {
                if (is_terminal_) {
                        return terminal_ == other.terminal_;
                } else {
                        return nonterminal_ == other.nonterminal_;
                }
        }

        return false;
}

//--------------------------------------

WRPARSE_API bool
Component::operator!=(
        const this_t &other
) const
{
        return !operator==(other);
}

//--------------------------------------

WRPARSE_API bool
Component::operator<(
        const this_t &other
) const
{
        if (this == &other) {
                return false;
        } else if (is_terminal_ == other.is_terminal_) {
                if (is_optional_ == other.is_optional_) {
                        if (is_terminal_) {
                                return (terminal_ < other.terminal_)
                                        || ((terminal_ == other.terminal_)
                                            && (predicate_ < other.predicate_));
                        } else {
                                return (nonterminal_ < other.nonterminal_)
                                        || ((nonterminal_ == other.nonterminal_)
                                            && (predicate_ < other.predicate_));
                        }
                } else {
                        return is_optional_ < other.is_optional_;
                }
        } else {
                return is_terminal_ < other.is_terminal_;
        }
}

//--------------------------------------

WRPARSE_API void
Component::dump(
        std::ostream &to,
        const Lexer  &lexer
) const
{
        const char *sep = "", *suffix = "";

        if (is_optional_) {
                to << "opt(";
                suffix = ")";
        } else if (predicate_) {
                to << "pred(";
                suffix = ")";
        }

        if (is_terminal_) {
                to << lexer.tokenKindName(terminal_);
                sep = ", ";
        } else if (nonterminal_) {
                to << nonterminal_->name();
                sep = ", ";
        }

        if (predicate()) {
                to << sep << "...";
        }

        to << suffix;
}

//--------------------------------------

WRPARSE_API void
Component::gdb(
        const Lexer &lexer
) const
{
        dump(uerr, lexer);
        uerr << '\n';
        uerr.flush();
}

//--------------------------------------

WRPARSE_API
Rule::Rule(
        std::initializer_list<Component> init,
        bool                             enable
) :
        nonterminal_(nullptr),
        enabled_    (enable)
{
        reserve(init.size() + 1);
        base_t::operator=(init);
        emplace_back();  // push dummy component at end
        updateComponents();
}

//--------------------------------------

WRPARSE_API
Rule::Rule(
        const this_t &other
) :
        base_t      (other),
        nonterminal_(other.nonterminal_),
        enabled_   (other.enabled_)
{
        updateComponents();
}

//--------------------------------------

WRPARSE_API
Rule::Rule(
        this_t &&other
) :
        base_t      (std::move(other)),
        nonterminal_(other.nonterminal_),
        enabled_    (other.enabled_)
{
        updateComponents();
}

//--------------------------------------

WRPARSE_API auto
Rule::operator=(
        const Rule &other
) -> this_t &
{
        if (&other != this) {
                base_t::operator=(other);
                nonterminal_ = other.nonterminal_;
                enabled_ = other.enabled_;
                updateComponents();
        }
        return *this;
}

//--------------------------------------

WRPARSE_API auto
Rule::operator=(
        Rule &&other
) -> this_t &
{
        if (&other != this) {
                base_t::operator=(std::move(other));
                nonterminal_ = other.nonterminal_;
                enabled_ = other.enabled_;
                updateComponents();
        }
        return *this;
}

//--------------------------------------

void
Rule::updateComponents()
{
        for (Component &c: static_cast<base_t &>(*this)) {
                c.rule_ = this;
        }
}

//--------------------------------------

WRPARSE_API int
Rule::index() const
{
        return nonterminal_ ? nonterminal_->indexOf(*this) : -1;
}

//--------------------------------------

WRPARSE_API int
Rule::indexOf(
        const Component &c
) const
{
        ptrdiff_t i = &c - &(*this)[0];

        if ((i < 0) || i >= numeric_cast<ptrdiff_t>(size())) {
                i = -1;
        }

        return numeric_cast<int>(i);
}

//--------------------------------------

WRPARSE_API bool
Rule::isLeftRecursive() const
{
        return !empty() && (front().getAsNonTerminal() == nonterminal_);
}

//--------------------------------------

WRPARSE_API bool
Rule::isRecursive() const
{
        bool is = false;

        for (const Component &comp: *this) {
                if (comp.getAsNonTerminal() == nonterminal_) {
                        is = true;
                        break;
                }
        }

        return is;
}

//--------------------------------------

WRPARSE_API bool
Rule::isDelegate() const
{
        return !empty() && (begin()->getAsNonTerminal() != nullptr)
                        && (std::next(begin()) == end());
}

//--------------------------------------

WRPARSE_API bool
Rule::mustHide() const
{
        return nonterminal_
               && (nonterminal_->isTransparent()
                   || (isDelegate() && nonterminal_->hideIfDelegate()));
}

//--------------------------------------

WRPARSE_API bool
Rule::matchesEmpty() const
{
        bool result = true;

        for (const Component &comp: *this) {
                if (comp.isOptional()) {
                        ;
                } else if (comp.isTerminal()) {
                        result = false;
                        break;
                } else if (comp.isNonTerminal()) {
                        if (!comp.getAsNonTerminal()->matchesEmpty()) {
                                result = false;
                                break;
                        }
                }
        }

        return result;
}

//--------------------------------------

WRPARSE_API const Component &
Rule::back() const
{
        return empty() ? base_t::back() : *(end() - 1);
}

//--------------------------------------

WRPARSE_API void
Rule::dump(
        std::ostream &to,
        const Lexer  &lexer
) const
{
        for (const Component &comp: *this) {
                comp.dump(to, lexer);
                to << " ";
        }

        to << "[sz=" << std::distance(begin(), end())
           << ";lr=" << isLeftRecursive() << ";r="
           << isRecursive() << ";d=" << isDelegate() << ']';
}

//--------------------------------------

WRPARSE_API void
Rule::gdb(
        const Lexer &lexer
) const
{
        dump(uerr, lexer);
        uerr << '\n';
        uerr.flush();
}

//--------------------------------------

WRPARSE_API
NonTerminal::NonTerminal() :
        name_ (""),
        flags_(0)
{
}

//--------------------------------------

WRPARSE_API
NonTerminal::NonTerminal(
        const this_t &other
) :
        this_t()
{
        *this = other;
}

//--------------------------------------

WRPARSE_API
NonTerminal::NonTerminal(
        this_t &&other
) :
        this_t()
{
        *this = std::move(other);
}

//--------------------------------------

WRPARSE_API
NonTerminal::NonTerminal(
        const char * const name,
        bool               enable,
        Rules              rules,
        Flags              flags
) :
        name_                 (name),
        got_initial_terminals_(false),
        is_ll1_               (false), // until proven otherwise
        matches_empty_        (false), // ditto
        is_transparent_       ((flags & TRANSPARENT) != 0),
        hide_if_delegate_     ((flags & HIDE_IF_DELEGATE) != 0),
        keep_recursion_       ((flags & KEEP_RECURSION) != 0)
{
        if (enable) {
                base_t::operator=(std::move(rules));
                initRules();
        }
}

//--------------------------------------

WRPARSE_API auto
NonTerminal::operator=(
        const this_t &other
) -> this_t &
{
        if (&other != this) {
                name_ = other.name_;
                base_t::operator=(other);
                initRules();
                initial_terminals_ = other.initial_terminals_;
                flags_ = other.flags_;
                // don't copy actions
        }

        return *this;
}

//--------------------------------------

WRPARSE_API auto
NonTerminal::operator=(
        this_t &&other
) -> this_t &
{
        if (&other != this) {
                name_ = other.name_;
                other.name_ = "";
                base_t::operator=(std::move(other));
                initRules();
                initial_terminals_ = std::move(other.initial_terminals_);
                flags_ = other.flags_;
                pre_parse_actions_ = std::move(other.pre_parse_actions_);
                post_parse_actions_ = std::move(other.post_parse_actions_);
        }

        return *this;
}

//--------------------------------------

WRPARSE_API auto
NonTerminal::operator+=(
        const Rules &other
) -> this_t &
{
        size_t i = size();
        base_t::insert(base_t::end(), other.base_t::begin(),
                       other.base_t::end());
        initRules(i);
        initial_terminals_.clear();
        got_initial_terminals_ = false;
        return *this;
}

//--------------------------------------

WRPARSE_API auto
NonTerminal::operator+=(
        Rules &&other
) -> this_t &
{
        for (Rule &rule: other) {
                if (rule.isEnabled()) {
                        base_t::emplace(base_t::end(), std::move(rule))
                                ->nonterminal_ = this;
                }
        }
        initial_terminals_.clear();
        got_initial_terminals_ = false;
        return *this;
}

//--------------------------------------

WRPARSE_API bool
NonTerminal::matchesEmpty() const
{
        if (!got_initial_terminals_) {
                initialTerminals();  // initialises matches_empty_
        }
        return matches_empty_;
}

//--------------------------------------

WRPARSE_API bool
NonTerminal::isLL1() const
{
        if (!got_initial_terminals_) {
                initialTerminals();  // initialises is_ll1_
        }
        return is_ll1_;
}

//--------------------------------------

WRPARSE_API int
NonTerminal::indexOf(
        const Rule &rule
) const
{
        ptrdiff_t i = &rule - &(*this)[0];

        if ((i < 0) || i >= numeric_cast<ptrdiff_t>(size())) {
                i = -1;
        }

        return numeric_cast<int>(i);
}

//--------------------------------------

WRPARSE_API auto
NonTerminal::initialTerminals() const -> const Terminals &
{
        if (!got_initial_terminals_) {
                std::set<const this_t *> visited;
                initTerminalsAndLL1(visited);
        }

        return initial_terminals_;
}

//--------------------------------------

void
NonTerminal::initRules(
        size_t from_pos
)
{
        for (size_t count = size(); from_pos != count; ++from_pos) {
                base_t::operator[](from_pos).nonterminal_ = this;
        }
}

//--------------------------------------

auto
NonTerminal::initTerminalsAndLL1(
        std::set<const this_t *> &visited
) const -> InitTerminalsStatus
{
        if (visited.count(this)) {
                return InitTerminalsStatus::OK;
        }

        visited.insert(this);
        is_ll1_ = true;          // until proven otherwise
        matches_empty_ = false;  // ditto

        auto        status = InitTerminalsStatus::OK;
        RuleIndices lr_rules;  /* includes rules with 'hidden' left-recursion,
                                  i.e. where all components preceding the
                                  recursive one are optional or may be empty */

        for (const Rule &rule: *this) {
                if (!rule.isEnabled()) {
                        continue;
                }

                status = initTerminalsAndLL1(visited, rule);

                if (status == InitTerminalsStatus::IS_LR) {
                        lr_rules.push_back(
                                numeric_cast<size_t>(&rule - &(*this)[0]));
                } else if (status != InitTerminalsStatus::OK) {
                        lr_rules.clear();  // don't bother processing these
                        break;
                }
        }

        if (!lr_rules.empty()) {
                for (auto t: initial_terminals_) {
                        auto &rules = initial_terminals_[t.first];
                        rules.insert_after(rules.last(),
                                           lr_rules.begin(), lr_rules.end());
                }
        }

        got_initial_terminals_ = true;
        return status;
}

//--------------------------------------

auto
NonTerminal::initTerminalsAndLL1(
        std::set<const this_t *> &visited,
        const Rule               &rule
) const -> InitTerminalsStatus
{
        // assume these conditions until proven otherwise
        bool rule_matches_empty        = true,
             depends_on_lone_predicate = false,
             subprod_indeterminate     = false;

        for (const Component &comp: rule) {
                const auto *other = comp.getAsNonTerminal();

                if (comp.isTerminal()) {
                        rule_matches_empty = rule_matches_empty
                                                && comp.isOptional();
                        TokenKind t = comp.getAsTerminal();

                        if (t == TOK_NULL) {
                                if (comp.predicate()) {
                                        depends_on_lone_predicate = true;
                                }
                        } else {
                                updateTerminalsAndLL1(t, rule);
                        }
                } else if (other) {
                        rule_matches_empty = rule_matches_empty
                                                && (comp.isOptional()
                                                    || other->matches_empty_);
                        if (other == this) {
                                is_ll1_ = false;
                                return InitTerminalsStatus::IS_LR;
                        }

                        if (!other->got_initial_terminals_) {
                                other->initTerminalsAndLL1(visited);
                        }

                        if (other->initial_terminals_.empty()) {
                                subprod_indeterminate = true;
                                break;
                        }

                        for (auto &i: other->initial_terminals_) {
                                updateTerminalsAndLL1(i.first, rule);
                        }
                }

                if (!rule_matches_empty) {
                        break;
                }
                // otherwise the next component must be examined
        }

        if (depends_on_lone_predicate || subprod_indeterminate) {
                is_ll1_ = false;
                initial_terminals_.clear();
                return InitTerminalsStatus::INDETERMINATE;
        } else if (rule_matches_empty) {
                updateTerminalsAndLL1(TOK_NULL, rule);
                matches_empty_ = true;
        }

        return InitTerminalsStatus::OK;
}

//--------------------------------------

void
NonTerminal::updateTerminalsAndLL1(
        TokenKind   t,
        const Rule &rule
) const
{
        auto &rule_indices = initial_terminals_[t];
        is_ll1_ = is_ll1_ && rule_indices.empty();
        rule_indices.push_back(rule.index());
}

//--------------------------------------

WRPARSE_API void
NonTerminal::dump(
        std::ostream &to,
        const Lexer  &lexer
) const
{
        to << name() << ":\n";

        for (const Rule &rule: *this) {
                to << "    ";
                rule.dump(to, lexer);
                to << '\n';
        }

        if (initial_terminals_.empty()) {
                to << "Initial terminals undetermined\n";
        } else {
                to << "Initial terminals:\n";

                for (const auto i: initial_terminals_) {
                        to << "    " << lexer.tokenKindName(i.first) << '\n';
                }
        }
}

//--------------------------------------

WRPARSE_API void
NonTerminal::gdb(
        const Lexer &lexer
) const
{
        dump(uerr, lexer);
        uerr.flush();
}

//--------------------------------------

WRPARSE_API bool
NonTerminal::removeAction(
        Action      target,
        ActionList &from
) // static
{
        for (auto prev = from.before_begin(); prev != from.last(); ++prev) {
                if (*std::next(prev) == target) {
                        from.erase_after(prev);
                        return true;
                }
        }
        return false;
}

//--------------------------------------

WRPARSE_API bool
NonTerminal::invokeActions(
        const ActionList &in,
        ParseState       &state
) // static
{
        bool ok = true;

        for (auto &action: in) {
                ok = (*action)(state) && ok;
        }

        return ok;
}


} // namespace parse
} // namespace wr
