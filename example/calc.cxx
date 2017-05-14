#include <limits>              // std::numeric_limits<double>::quiet_NaN()
#include <wrutil/Format.h>     // wr::formatStr()
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
        using base_t = wr::parse::PatternLexer;

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
                                 .setSpelling(storeMatchedIfMultiChar());
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
                                 .setSpelling(storeMatchedIfMultiChar());
                        }}
        })
        {
        }

        // core Lexer interface
        virtual const char *tokenKindName(wr::parse::TokenKind kind) const
                override
        {
                switch (kind) {
                default: case TOK_NULL: case TOK_EOF:
                        return base_t::tokenKindName(kind);

                case TOK_PLUS:     return "+";
                case TOK_MINUS:    return "-";
                case TOK_MULTIPLY: return "*";
                case TOK_DIVIDE:   return "/";
                case TOK_LPAREN:   return "(";
                case TOK_RPAREN:   return ")";
                case TOK_NUMBER:   return "number";
                case TOK_NEWLINE:  return "newline";
                }
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

                return static_cast<double>(wr::to_int<unsigned long long>(
                                                      spelling, nullptr, base));
        }
};

//--------------------------------------
/**
 * \brief The parser
 *
 * Defines the grammar and semantic actions for the language.
 * The wr::parse::Parser base class provides the 'glue' to the lexer and the
 * public API to effect parsing.
 */
class CalcParser : public wr::parse::Parser
{
public:
        CalcParser(CalcLexer &lexer);  // set up grammar and semantic actions

        static bool arithmeticAction(wr::parse::ParseState &state);
                                                        // semantic action
        // all nonterminals
        const wr::parse::NonTerminal primary_expr,
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

        // part one: grammar nonterminals and rules
        /* operator precedence is handled by using several linked nonterminals:
           one for + and -, another for * and / plus another for literal
           numbers and parenthesised subexpressions; the 'deeper' nonterminals
           take higher precedence */
        arithmetic_expr { "arithmetic-expression", {
                { multiply_expr },
                { arithmetic_expr, TOK_PLUS, multiply_expr },
                { arithmetic_expr, TOK_MINUS, multiply_expr },
        }},

        multiply_expr { "multiply-expression", {
                { unary_expr },
                { multiply_expr, TOK_MULTIPLY, unary_expr },
                { multiply_expr, TOK_DIVIDE, unary_expr }
        }, wr::parse::NonTerminal::HIDE_IF_DELEGATE },

        unary_expr { "unary-expression", {
                { primary_expr },
                { unary_op, unary_expr }
        }, wr::parse::NonTerminal::HIDE_IF_DELEGATE },

        unary_op { "unary-op", {
                { TOK_PLUS },
                { TOK_MINUS }
        }},

        primary_expr { "primary-expression", {
                { TOK_NUMBER },
                { TOK_LPAREN, arithmetic_expr, TOK_RPAREN }
        }}
{
        // part two: semantic actions
        arithmetic_expr.addPostParseAction(&arithmeticAction);
        multiply_expr.addPostParseAction(&arithmeticAction);
        unary_expr.addPostParseAction(&arithmeticAction);
        primary_expr.addPostParseAction(&arithmeticAction);
}


bool CalcParser::arithmeticAction(wr::parse::ParseState &state)
{
        wr::parse::SPPFNode::ConstPtr parsed = state.parsedNode();
        double                        result = 0;

        if (parsed->is(TOK_NUMBER)) {
                result = CalcLexer::valueOf(parsed->firstToken());
        } else {
                wr::parse::TokenKind operation = parsed->firstToken()->kind();

                for (const auto &term: wr::parse::nonTerminals(parsed)) {
                        if (!term.auxData()) {
                                continue;  // skip unary-op part of unary-expr
                        }

                        double operand = Result::getFrom(term)->value;

                        switch (operation) {
                        default:           result = operand; break;
                        case TOK_PLUS:     result += operand; break;
                        case TOK_MINUS:    result -= operand; break;
                        case TOK_MULTIPLY: result *= operand; break;
                        case TOK_DIVIDE:   result /= operand; break;
                        }

                        operation = term.lastToken()->next()->kind();
                }
        }

        parsed->setAuxData(new Result(result));
        return true;
};

//--------------------------------------

struct DiagnosticPrinter : public wr::parse::DiagnosticHandler
{
        virtual void onDiagnostic(const wr::parse::Diagnostic &d) override
        {
                wr::print(wr::uerr, "%s: %s at column %u\n",
                          d.describeCategory(), d.text(), d.column());
        }
};

//--------------------------------------

int main()
{
        CalcLexer         lexer(wr::uin);
        CalcParser        parser(lexer);
        DiagnosticPrinter diag_out;

        parser.addDiagnosticHandler(diag_out);

        wr::parse::NonTerminal calc_input = { "calc-input", {
                { parser.arithmetic_expr },
                { TOK_NEWLINE }
        }};

        int status = EXIT_SUCCESS;

        while (wr::uin.good()) {
                wr::parse::SPPFNode::Ptr result = parser.parse(calc_input);

                if (!result || parser.errorCount()) {
                        if (!wr::uin.eof()) {
                                status = EXIT_FAILURE;
                                parser.reset();  // clear any remaining tokens
                        }
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
