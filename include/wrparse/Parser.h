/**
 * \file Parser.h
 *
 * \brief Main parsing interface
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
#ifndef WRPARSE_PARSER_H
#define WRPARSE_PARSER_H

#include <iosfwd>
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

#include <wrparse/Config.h>
#include <wrparse/Diagnostics.h>
#include <wrparse/Grammar.h>
#include <wrparse/SPPF.h>
#include <wrparse/Token.h>


namespace wr {
namespace parse {


class Lexer;


class WRPARSE_API Parser :
        public DiagnosticCounter,
        protected DiagnosticEmitter
{
public:
        using this_t = Parser;

        // opaque internal types
        class GLL;
        class GSS;

        struct FatalError {};

        enum { DEFAULT_ERROR_LIMIT = 20 };

        Parser();
        Parser(Lexer &lexer);
        virtual ~Parser();

        Parser &setLexer(Lexer &lexer);

        SPPFNode::Ptr parse(const NonTerminal &start);

        Lexer *lexer()                   { return lexer_; }
        TokenList &tokens()              { return tokens_; }
        const TokenList &tokens() const  { return tokens_; }

        Token *nextToken(const Token *pos = nullptr);
        Token *lastToken();

        virtual Parser &reset();

        Parser &enableDebug(bool enable);
        bool debugEnabled() const { return debug_; }

        virtual void onDiagnostic(const Diagnostic &d) override;
        size_t errorLimit() const noexcept { return error_limit_; }
        void setErrorLimit(size_t limit);

        /**
         * \brief Add receiver of diagnostic messages
         * \param [in] handler  reference to receiver object
         */
        void addDiagnosticHandler(DiagnosticHandler &handler)
                { DiagnosticEmitter::addDiagnosticHandler(handler); }

        /**
         * \brief Remove a receiver of diagnostic messages
         * \param [in] handler  reference to receiver object to be removed
         * \return `true` if successfully removed, `false` if not found
         */
        bool removeDiagnosticHandler(DiagnosticHandler &handler)
                { return DiagnosticEmitter::removeDiagnosticHandler(handler); }

private:
        friend ParseState;

        Lexer     *lexer_;
        TokenList  tokens_;
        bool       debug_;
        size_t     error_limit_;
};

//--------------------------------------

class WRPARSE_API ParseState
{
public:
        using this_t = ParseState;

        ~ParseState();

        const Parser &parser() const           { return parser_; }
        Parser &parser()                       { return parser_; }
        const NonTerminal &start() const       { return start_; }
        const Rule &rule() const               { return rule_; }
        const NonTerminal &nonTerminal() const { return *rule_.nonTerminal(); }
        SPPFNode::ConstPtr parsedNode() const  { return parsed_; }
        Token *input() const                   { return input_pos_; }

        /**
         * \brief Emit diagnostic message for current input token position
         * \param [in] category  message severity
         * \param [in] fmt       format string
         * \param [in] args  zero or more arguments to be formatted into message
         */
        template <typename ...Args>
        void emit(Diagnostic::Category category, const char *fmt, Args ...args)
        {
                parser_.emit({ category, input_pos_->offset(),
                               input_pos_->bytes(), input_pos_->line(),
                               input_pos_->column(), fmt,
                               std::forward<Args>(args)... });
        }

        /// \brief Emit diagnostic message `d`
        void emit(const Diagnostic &d) { parser_.emit(d); }

private:
        friend Parser;
        friend Parser::GLL;

        ParseState(const this_t &other);

        ParseState(Parser &parser, const NonTerminal &start, const Rule &rule,
                   Token *input_pos, SPPFNode::ConstPtr parsed = nullptr);

        Parser             &parser_;
        const NonTerminal  &start_;
        const Rule         &rule_;
        Token              *input_pos_;
        SPPFNode::ConstPtr  parsed_;
};


} // namespace parse
} // namespace wr


#endif  // !WRPARSE_PARSER_H
