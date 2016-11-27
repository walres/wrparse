#include <algorithm>  // std::copy_n()
#include <cstdlib>    // atof(), strtoull()
#include <limits>     // quiet_NaN()
#include <wrutil/uiostream.h>  // wr::uin
#include <wrparse/PatternLexer.h>
#include <wrparse/Grammar.h>
#include <wrparse/Parser.h>
#include <wrparse/SPPF.h>
#include <wrparse/SPPFOutput.h>

/**
 * \brief The token types
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
        TOK_NEWLINE,
        TOK_NUMBER
};

//--------------------------------------
/**
 * \brief The lexical analyser
 *
 * `CalcLexer` generates tokens from an incoming text stream for processing
 * by the parser object, `CalcParser`.
 */
class CalcLexer : public wr::parse::PatternLexer
{
public:
        CalcLexer(std::istream &input) : PatternLexer(input, {
                { R"(\+)",
                        [](wr::parse::Token &t) {
                                t.setKind(TOK_PLUS).setSpelling("+");
                        }},
                { "-", [](wr::parse::Token &t) { t.setKind(TOK_MINUS); }},
                { R"(\*)",
                        [](wr::parse::Token &t) {
                                t.setKind(TOK_MULTIPLY).setSpelling("*");
                        }},
                { u8"\u00d7",  // matches Unicode multiply sign
                        [](wr::parse::Token &t) { t.setKind(TOK_MULTIPLY); }},
                {{ "/", u8"\u00f7" },  // match '/' or Unicode division sign
                        [](wr::parse::Token &t) { t.setKind(TOK_DIVIDE); }},
                { R"(\()",
                        [](wr::parse::Token &t) {
                                t.setKind(TOK_LPAREN).setSpelling("(");
                        }},
                { R"(\))",
                        [](wr::parse::Token &t) {
                                t.setKind(TOK_RPAREN).setSpelling(")");
                        }},
                { R"(\R)",  // any Unicode newline sequence
                        [this](wr::parse::Token &t) {
                                t.setKind(TOK_NEWLINE)
                                 .setSpelling(storeMatched());
                        }},                        
                { R"([\t\f\x0b\p{Zs}]+)" },
                        /* ignore non-newline whitespace
                           NB: '\x0b' used instead of '\v' above
                           (in this context '\v' means "any vertical whitespace"
                           not "vertical tab") */
                { { R"(\d+(\.\d*)?([Ee][+-]?\d+)?)",  // decimal number
                    R"(\.\d+([Ee][+-]?\d+)?)",        // ditto, starting with .
                    "0b[10]+",                        // binary integer
                    "0x[[:xdigit:]]+" },              // hexadecimal integer
                        [this](wr::parse::Token &t) {
                                t.setKind(TOK_NUMBER)
                                 .setSpelling(storeMatched());
                        }}
        })
        {
        }

        /**
         * \brief get the numeric value of a `TOK_NUMBER` token
         * \param [in] t  the input token
         * \return numeric value expressed by `t`
         * \return `NaN` if `t` is null or not of type `TOK_NUMBER`
         */
        static double valueOf(const wr::parse::Token *t)
        {
                if (!t || !t->is(TOK_NUMBER)) {
                        return std::numeric_limits<double>::quiet_NaN();
                }

                wr::u8string_view spelling(t->spelling());
                int               base = 10;

                if (spelling.has_prefix("0b")) {
                        base = 2;
                } else if (spelling.has_prefix("0x")) {
                        base = 16;
                } else {
                        return wr::to_float<double>(spelling);
                }

                return wr::to_int<unsigned long long>(spelling, nullptr, base);
        }
};

//--------------------------------------
/**
 * \brief The parser
 *
 * Defines the grammar and semantic actions for the language.
 * The wr::parse::Parser base class provides the 'glue' to the lexer plus the
 * public API to effect parsing.
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
         * \brief result data calculated for and attached to SPPF nodes
         */
        struct Result : wr::parse::AuxData
        {
                double value;

                Result(double v) : value(v) {}

                static Result *getFrom(const wr::parse::SPPFNode &node)
                        { return static_cast<Result *>(node.auxData().get()); }
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
                if (state.rule().index() == 0) {  // is unary_expr
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
                if (state.rule().index() != 0) {  // is not primary-expr
                        auto parsed = state.parsedNode();
                        auto i = wr::parse::nonTerminals(parsed);
                        if (!++i) {  // skip sign
                                return false;
                        }
                        double result = Result::getFrom(*i)->value;
                        if (parsed->firstToken()->is(TOK_MINUS)) {
                                result = -result;
                        }
                        parsed->setAuxData(new Result(result));
                }
                return true;
        });

        primary_expr.addPostParseAction([](wr::parse::ParseState &state) {
                wr::parse::SPPFNode::ConstPtr parsed = state.parsedNode();

                if (parsed->is(TOK_NUMBER)) {
                        parsed->setAuxData(new Result(
                                    CalcLexer::valueOf(parsed->firstToken())));
                } else {  // parenthesised expression
                        auto i = wr::parse::nonTerminals(parsed);
                        if (!i) {
                                return false;
                        }
                        parsed->setAuxData(Result::getFrom(*i));
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
                        wr::uerr << "parse failed" << std::endl;
                        status = EXIT_FAILURE;
                        parser.reset();  // clear any remaining tokens
                } else if (result->is(parser.arithmetic_expr)) {
                        auto expr = result->find(parser.arithmetic_expr);
                        wr::uout << CalcParser::Result::getFrom(*expr)->value
                                 << std::endl;
                }

                lexer.clearStorage();  // clear gathered token spellings
        }

        if (!wr::uin.good() && !wr::uin.eof()) {
                status = EXIT_FAILURE;
        }

        return status;
}
