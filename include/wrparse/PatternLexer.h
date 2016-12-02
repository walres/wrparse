/**
 * \file wrparse/PatternLexer.h
 *
 * \brief Regular expression-based lexer
 *
 * \copyright
 * \parblock
 *
 *   Copyright 2016 James S. Waller
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
#ifndef WRPARSE_PATTERN_LEXER_H
#define WRPARSE_PATTERN_LEXER_H

#include <functional>
#include <initializer_list>
#include <type_traits>
#include <wrutil/circ_fwd_list.h>
#include <wrparse/Lexer.h>


namespace wr {
namespace parse {


class WRPARSE_API PatternLexer : public Lexer
{
public:
        using base_t = Lexer;
        using this_t = PatternLexer;
        using Action = std::function<void (Token &token)>;

        struct Body;  // opaque internal type

        class WRPARSE_API Rule
        {
        public:
                /* avoid ambiguity errors when bool used directly in
                   constructors below */
                struct ExplicitBool
                {
                        bool value_;

                        ExplicitBool(bool value) : value_(value) {}

                        operator bool() const { return value_; }
                };

                using this_t = Rule;

                Rule(u8string_view pattern, ExplicitBool enable = true);

                Rule(std::initializer_list<u8string_view> patterns,
                     ExplicitBool enable = true);

                Rule(u8string_view pattern, Action action,
                        ExplicitBool enable = true);

                Rule(std::initializer_list<u8string_view> patterns,
                        Action action, ExplicitBool enable = true);

                Rule(const this_t &other);
                Rule(this_t &&other);

                this_t &operator=(const this_t &other);
                this_t &operator=(this_t &&other);

                int priority() const { return priority_; }

        private:
                friend PatternLexer;
                friend PatternLexer::Body;

                struct Pattern
                {
                        using this_t = Pattern;

                        std::string orig_str_;  // original pattern string
                        Rule *rule_;  // the enclosing rule
                        void *re_;    /* the compiled regular expression;
                                         hide implementation details */
                        size_t re_workspace_size_;

                        Pattern();
                        Pattern(Rule *rule, const u8string_view &orig_str,
                                void *re);
                        Pattern(const this_t &other);
                        Pattern(this_t &&other);

                        ~Pattern();

                        void free();

                        this_t &operator=(const this_t &other);
                        this_t &operator=(this_t &&other);
                };

                void updatePatterns();


                bool                   enabled_;
                int                    priority_;
                circ_fwd_list<Pattern> patterns_;
                Action                 action_;
        };

        PatternLexer(std::initializer_list<Rule> rules);
        PatternLexer(std::istream &input, std::initializer_list<Rule> rules);

        virtual Token &lex(Token &out_token) override;

        wr::u8string_view matched() const;

        std::streamoff offset() const;
                        /**< \brief Obtain number of bytes read from
                                the beginning of the input */

protected:
        wr::u8string_view storeMatched() { return store(matched()); }

private:
        Body *body_;
};


} // namespace parse
} // namespace wr


#endif // !WRPARSE_PATTERN_LEXER_H
