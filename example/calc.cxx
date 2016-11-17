#include <string>

#include <wrutil/ctype.h>
#include <wrutil/uiostream.h>

#include <wrparse/SPPF.h>
#include <wrparse/SPPFOutput.h>
#include <wrparse/Lexer.h>
#include <wrparse/Grammar.h>
#include <wrparse/Parser.h>

/**
 * \brief define token types
 *
 * These token type constants are used both by the lexer
 * and by the parser as terminal symbols.
 */
enum : wr::parse::TokenKind
/*
 * troubleshooting: rules won't compile? ensure token types enumeration has
 *      base wr::parse::TokenKind!
 */
{
        TOK_NULL = wr::parse::TOK_NULL,
        TOK_EOF  = wr::parse::TOK_EOF,
        TOK_PLUS = wr::parse::TOK_USER_MIN,
        TOK_MINUS,
        TOK_MULTIPLY,
        TOK_DIVIDE,
        TOK_LPAREN,
        TOK_RPAREN,
        TOK_NUMBER,
        TOK_NEWLINE,
        TOK_UNKNOWN
};

//--------------------------------------
/**
 * \brief calculator lexical analyser
 *
 * CalcLexer generates tokens from an incoming text stream for processing
 * by the parser.
 */
class CalcLexer : public wr::parse::Lexer
{
public:
        CalcLexer(std::istream &input);

        // core Lexer interface
        virtual wr::parse::Token &lex(wr::parse::Token &out_token);
        virtual const char *tokenKindName(wr::parse::TokenKind kind) const;

private:
        void lexNumber(wr::parse::Token &out_token);
                                        // helper function for lex()
        unsigned lexDigits(std::string &spelling);
                                        // helper function for lexNumber()

        void error(const wr::u8string_view &message)
        {
                wr::uerr << "error at line " << line() << " column " << column()
                         << ": " << message << '\n';
        }

        wr::parse::TokenFlags next_token_flags_;
};


CalcLexer::CalcLexer(std::istream &input) :
        Lexer            (input),
        next_token_flags_(wr::parse::TF_STARTS_LINE)
{
}


wr::parse::Token &CalcLexer::lex(wr::parse::Token &out_token)
{
        out_token.reset();  /* resets token's type to wr::parse::TOK_NULL,
                               offset and length zero and empty spelling */

        out_token.setOffset(offset());  // initialise token's offset

        char32_t c = read();

        while ((c != '\n') && (c != eof) && wr::isuspace(c)) {
                next_token_flags_ |= wr::parse::TF_SPACE_BEFORE;
                c = read();
        }

        out_token.setFlags(next_token_flags_);
        next_token_flags_ = 0;

        switch (c) {
        case U'\n':
                out_token.setKind(TOK_NEWLINE).setSpelling(u8"\n");
                next_token_flags_ |= wr::parse::TF_STARTS_LINE;
                break;

        case eof:  out_token.setKind(TOK_EOF); break;
        case U'+': out_token.setKind(TOK_PLUS).setSpelling(u8"+"); break;
        case U'-': out_token.setKind(TOK_MINUS).setSpelling(u8"-"); break;
        case U'*': out_token.setKind(TOK_MULTIPLY).setSpelling(u8"*"); break;
        case U'\u00d7':
                   out_token.setKind(TOK_MULTIPLY).setSpelling(u8"\u00d7");
                   break;
        case U'/': out_token.setKind(TOK_DIVIDE).setSpelling(u8"/"); break;
        case U'\u00f7':
                   out_token.setKind(TOK_DIVIDE).setSpelling(u8"\u00f7"); break;
        case U'(': out_token.setKind(TOK_LPAREN).setSpelling(u8"("); break;
        case U')': out_token.setKind(TOK_RPAREN).setSpelling(u8")"); break;
        case U'.': lexNumber(out_token); break;

        default:
                if (wr::isudigit(c)) {
                        lexNumber(out_token);
                } else {
                        out_token.setKind(TOK_UNKNOWN).setSpelling("\0");
                }
                break;
        }

        return out_token;
}


void CalcLexer::lexNumber(wr::parse::Token &out_token)
{
        std::string spelling;
        wr::utf8_append(spelling, lastRead());

        // read integer part unless first character was decimal point
        if (lastRead() != U'.') {
                lexDigits(spelling);
                if (peek() == U'.') {  // consume upcoming decimal point
                        read();
                }
        } else if (!wr::isudigit(peek())) {
                // syntax error - must be at least one digit either side of '.'
                error("expected number after '.'");
                return;
        }

        // read fractional part if it exists
        if (lastRead() == U'.') {
                if (lexDigits(spelling) == 0) {
                        wr::utf8_append(spelling, U'0');
                }
        }

        // read exponent if it exists and is complete
        if (wr::toulower(lastRead()) == U'e') {
                char32_t c = peek();
                unsigned count = 1;
                switch (c) {
                case '+': case '-':
                        wr::utf8_append(spelling, read());
                        ++count;
                        c = peek();
                        // fall through
                default:
                        if (wr::isudigit(c)) {
                                wr::utf8_append(spelling, 'e');
                                if (count > 1) {  // append sign
                                        wr::utf8_append(spelling, lastRead());
                                }
                                lexDigits(spelling);
                        } else {
                                /* incomplete - no number after 'e' -
                                   treat as separate from number */
                                backtrack(count);
                        }
                        break;
                }
        }

        out_token.setKind(TOK_NUMBER).setSpelling(store(spelling));
}


unsigned CalcLexer::lexDigits(std::string &spelling)
{
        unsigned count = 0;

        for (; wr::isudigit(peek()); ++count) {
                wr::utf8_append(spelling, read());
        }

        return count;
}


const char *CalcLexer::tokenKindName(wr::parse::TokenKind kind) const
{
        switch (kind) {
        case TOK_NULL:     return "NULL";
        case TOK_EOF:      return "EOF";
        case TOK_PLUS:     return "PLUS";
        case TOK_MINUS:    return "MINUS";
        case TOK_MULTIPLY: return "MULTIPLY";
        case TOK_DIVIDE:   return "DIVIDE";
        case TOK_LPAREN:   return "LPAREN";
        case TOK_RPAREN:   return "RPAREN";
        case TOK_NUMBER:   return "NUMBER";
        case TOK_NEWLINE:  return "NEWLINE";
        default:           return "UNKNOWN";
        }
}

//--------------------------------------
/**
 * \brief the calculator parser
 *
 * Defines the productions, grammar and semantic actions for the language.
 * The wr::parse::Parser base class provides the 'glue' to the lexer and the
 * public API to initiate parsing.
 */
class CalcParser : public wr::parse::Parser
{
public:
        CalcParser(CalcLexer &lexer);  // set up grammar and semantic actions

        // all productions
        const wr::parse::Production primary_expr,
                                    unary_expr,
                                    unary_op,
                                    arithmetic_expr,
                                    multiply_expr;

        /**
         * \brief result data calculated for and attached to each SPPF node
         */
        struct Result : wr::parse::AuxData
        {
                using this_t = Result;

                double value;

                Result(double v) : value(v) {}

                static this_t *getFrom(const wr::parse::SPPFNode &node)
                        { return static_cast<this_t *>(node.auxData().get()); }
        };
};


CalcParser::CalcParser(CalcLexer &lexer) :
        Parser(lexer),

        // part one: grammar productions / rules
        /* operator precedence is handled by using two separate productions:
           one for + and -, the other for * and /, the latter taking
           precedence in this case (similar to C-like languages) */
        arithmetic_expr { "arithmetic-expr", {
                { multiply_expr },
                { arithmetic_expr, TOK_PLUS, multiply_expr },
                { arithmetic_expr, TOK_MINUS, multiply_expr },
        }},

        multiply_expr { "multiply-expr", {
                { unary_expr },
                { multiply_expr, TOK_MULTIPLY, unary_expr },
                { multiply_expr, TOK_DIVIDE, unary_expr }
        }, wr::parse::Production::HIDE_IF_DELEGATE },

        unary_expr { "unary-expr", {
                { primary_expr },
                { unary_op, unary_expr }
        }, wr::parse::Production::HIDE_IF_DELEGATE },

        unary_op { "unary-op", {
                { TOK_PLUS },
                { TOK_MINUS }
        }},

        primary_expr { "primary-expr", {
                { TOK_NUMBER },
                { TOK_LPAREN, arithmetic_expr, TOK_RPAREN }
        }}
{
        // part two: semantic actions
        arithmetic_expr.addPostParseAction([](wr::parse::ParseState &state) {
                wr::parse::SPPFNode::ConstPtr parsed    = state.parsedNode();
                double                        result    = 0;
                wr::parse::TokenKind          operation = TOK_PLUS;

                for (const auto &term: wr::parse::subProductions(parsed)) {
                        double operand = Result::getFrom(term)->value;
                        if (operation == TOK_MINUS) {
                                operand = -operand;
                        }
                        result += operand;
                        operation = term.lastToken()->next()->kind();
                }

                parsed->setAuxData(new Result(result));
                return true;
        });

        multiply_expr.addPostParseAction([](wr::parse::ParseState &state) {
                if (state.rule()->mustHide()) {  // delegated to unary_expr
                        return true;
                }

                wr::parse::SPPFNode::ConstPtr parsed    = state.parsedNode();
                double                        result    = 0;
                wr::parse::TokenKind          operation = TOK_NULL;

                for (const auto &term: wr::parse::subProductions(parsed)) {
                        double operand = Result::getFrom(term)->value;
                        if (operation == TOK_MULTIPLY) {
                                result *= operand;
                        } else if (operation == TOK_DIVIDE) {
                                result /= operand;
                        } else {
                                result = operand;
                        }
                        operation = term.lastToken()->next()->kind();
                }

                parsed->setAuxData(new Result(result));
                return true;
        });

        unary_expr.addPostParseAction([](wr::parse::ParseState &state) {
                if (!state.rule()->mustHide()) {
                        auto parsed = state.parsedNode();
                        auto walker = wr::parse::nonTerminals(parsed);
                        if (!++walker) {  // skip sign
                                return false;
                        }
                        double value = Result::getFrom(*walker)->value;
                        if (parsed->firstToken()->is(TOK_MINUS)) {
                                value = -value;
                        }
                        parsed->setAuxData(new Result(value));
                } // else delegated to primary-expr
                return true;
        });

        primary_expr.addPostParseAction([](wr::parse::ParseState &state) {
                wr::parse::SPPFNode::ConstPtr parsed = state.parsedNode();

                if (state.rule()->index() == 0) {
                        parsed->setAuxData(new Result(wr::to_float<double>(
                                            parsed->firstToken()->spelling())));
                } else {  // parenthesised expression
                        auto walker = wr::parse::nonTerminals(parsed);
                        if (!walker) {
                                return false;
                        }
                        parsed->setAuxData(Result::getFrom(*walker));
                }
                return true;
        });
}

//--------------------------------------

int main()
{
        CalcLexer  lexer (wr::uin);
        CalcParser parser(lexer);
        int        status = EXIT_SUCCESS;

        wr::parse::Production calc_input = { "calc-input", {
                { parser.arithmetic_expr },
                { TOK_NEWLINE },
                { TOK_EOF }
        }};

        while (wr::uin.good() && !parser.nextToken()->is(TOK_EOF)) {
                wr::parse::SPPFNode::Ptr result = parser.parse(calc_input);

                if (!result) {
                        wr::uerr << "parse failed\n";
                        status = EXIT_FAILURE;
                        parser.reset();  // clear any remaining tokens
                } else {
                        auto expr = result->find(parser.arithmetic_expr);
                        if (expr) {
                                wr::uout << CalcParser::Result::
                                                getFrom(*expr)->value << '\n';
                        }
                }

                lexer.clearStorage();  // clear gathered token spellings
        }

        if (!wr::uin.good() && !wr::uin.eof()) {
                status = EXIT_FAILURE;
        }

        return status;
}
