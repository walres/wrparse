#include <wrutil/TestManager.h>
#include <wrparse/Token.h>


namespace wr {
namespace parse {


class TokenTests : public TestManager
{
public:
        using this_t = TokenTests;
        using base_t = TestManager;

        TokenTests(int argc, const char **argv) :
                base_t("parse::Token", argc, argv) {}

        int runAll();

        static void defaultConstruct(),
                    setSingleCharSpelling(),
                    setMultiCharSpelling(),
                    setEmptySpelling(),
                    copyConstructEmpty(),
                    copyConstructSingleChar(),
                    copyConstructMultiChar(),
                    copyAssignEmpty(),
                    copyAssignSingleChar(),
                    copyAssignMultiChar();
};


} // namespace parse
} // namespace wr

//--------------------------------------

int
main(
        int          argc,
        const char **argv
)
{
        return wr::parse::TokenTests(argc, argv).runAll();
}

//--------------------------------------

int
wr::parse::TokenTests::runAll()
{
        run("defaultConstruct", 1, defaultConstruct);
        run("setSingleCharSpelling", 1, setSingleCharSpelling);
        run("setMultiCharSpelling", 1, setMultiCharSpelling);
        run("setEmptySpelling", 1, setEmptySpelling);
        run("copyConstructEmpty", 1, copyConstructEmpty);
        run("copyConstructSingleChar", 1, copyConstructSingleChar);
        run("copyConstructMultiChar", 1, copyConstructMultiChar);
        run("copyAssignEmpty", 1, copyAssignEmpty);
        run("copyAssignSingleChar", 1, copyAssignSingleChar);
        run("copyAssignMultiChar", 1, copyAssignMultiChar);
        return failed() ? EXIT_FAILURE : EXIT_SUCCESS;
}

//--------------------------------------

void
wr::parse::TokenTests::defaultConstruct() // static
{
        Token t;

        if (t.kind() != TOK_NULL) {
                throw TestFailure("t.kind() returned %u, expected %u (TOK_NULL)",
                                  static_cast<unsigned>(t.kind()),
                                  static_cast<unsigned>(TOK_NULL));
        }

        if (t.offset() != 0) {
                throw TestFailure("t.offset() returned %u, expected 0",
                                  t.offset());
        }

        if (t.line() != 0) {
                throw TestFailure("t.line() returned %u, expected 0", t.line());
        }

        if (t.column() != 0) {
                throw TestFailure("t.column() returned %u, expected 0",
                                  t.column());
        }

        if (t.bytes() != 0) {
                throw TestFailure("t.bytes() returned %u, expected 0",
                                  t.bytes());
        }

        if (t.flags() != 0) {
                throw TestFailure("t.flags() returned %#.04x, expected 0",
                                  t.flags());
        }

        if (t.next() != nullptr) {
                throw TestFailure("t.next() returned %p, expected nullptr",
                                  t.next());
        }

        if (t.next_.tag() != 1) {
                throw TestFailure("t.next_.tag() returned %u, expected 1",
                                  t.next_.tag());
        }

        auto spelling = t.spelling();

        if (!spelling.empty()) {
                throw TestFailure("t.spelling() returned \"%s\", expected empty",
                                  spelling);
        }
}

//--------------------------------------

void
wr::parse::TokenTests::setSingleCharSpelling() // static
{
        Token t;

        static const auto SPELLING = "x";

        t.setSpelling(SPELLING);

        if (t.next_.tag() != 0) {
                throw TestFailure("t.next_.tag() returned %u, expected 0",
                                  t.next_.tag());
        }

        auto spelling = t.spelling();

        if (spelling != SPELLING) {
                throw TestFailure("t.spelling() returned \"%s\", expected \"%s\"",
                                  spelling, SPELLING);
        }

        if (spelling.char_data() != t.spelling_.buf_) {
                throw TestFailure("return value of t.spelling() should reference internal buffer");
        }
}

//--------------------------------------

void
wr::parse::TokenTests::setMultiCharSpelling() // static
{
        Token t;

        static const auto SPELLING = "xyz";

        t.setSpelling(SPELLING);

        if (t.next_.tag() != 1) {
                throw TestFailure("t.next_.tag() returned %u, expected 1",
                                  t.next_.tag());
        }

        auto spelling = t.spelling();

        if (spelling != SPELLING) {
                throw TestFailure("t.spelling() returned \"%s\", expected \"%s\"",
                                  spelling, SPELLING);
        }

        if (spelling.char_data() != SPELLING) {
                throw TestFailure("return value of t.spelling() should reference input string");
        }
}

//--------------------------------------

void
wr::parse::TokenTests::setEmptySpelling() // static
{
        Token t;

        t.setSpelling("x");
        t.setSpelling("");

        if (t.next_.tag() != 1) {
                throw TestFailure("t.next_.tag() returned %u, expected 1",
                                  t.next_.tag());
        }

        auto spelling = t.spelling();

        if (!spelling.empty()) {
                throw TestFailure("t.spelling() returned \"%s\", expected empty string",
                                  spelling);
        }
}

//--------------------------------------

void
wr::parse::TokenTests::copyConstructEmpty() // static
{
        Token t;
        Token t2(t);
}

//--------------------------------------

void
wr::parse::TokenTests::copyConstructSingleChar() // static
{
        Token t;

        static const auto SPELLING = "x";

        t.setSpelling(SPELLING);

        Token t2(t);

        auto t2_spelling = t2.spelling();

        if (t2_spelling != SPELLING) {
                throw TestFailure("t2.spelling() returned \"%s\", expected \"%s\"",
                                 t2_spelling, SPELLING);
        }

        if (t2.next_.tag() != 0) {
                throw TestFailure("t2.next_.tag() returned %u, expected 0",
                                  t2.next_.tag());
        }

        if (t2_spelling.char_data() != t2.spelling_.buf_) {
                throw TestFailure("return value of t2.spelling() should reference internal buffer");
        }
}

//--------------------------------------

void
wr::parse::TokenTests::copyConstructMultiChar() // static
{
        Token t;

        static const auto SPELLING = "hello";

        t.setSpelling(SPELLING);

        Token t2(t);

        auto t2_spelling = t2.spelling();

        if (t2_spelling != SPELLING) {
                throw TestFailure("t2.spelling() returned \"%s\", expected \"%s\"",
                                 t2_spelling, SPELLING);
        }

        if (t2.next_.tag() != 1) {
                throw TestFailure("t2.next_.tag() returned %u, expected 1",
                                  t2.next_.tag());
        }

        if (t2_spelling.char_data() != SPELLING) {
                throw TestFailure("return value of t2.spelling() should reference input string");
        }
}

//--------------------------------------

void
wr::parse::TokenTests::copyAssignEmpty() // static
{
        Token t, empty;
        t.setKind(TOK_USER_MIN).setLine(1).setColumn(1);
        t.setOffset(999UL).setSpelling("x");
        t = empty;

        auto t_spelling = t.spelling();

        if (!t_spelling.empty()) {
                throw TestFailure("t.spelling() returned \"%s\", expected empty string",
                                 t_spelling);
        }

        if (t.next_.tag() != 1) {
                throw TestFailure("t.next_.tag() returned %u, expected 1",
                                  t.next_.tag());
        }

        if (t.kind() != TOK_NULL) {
                throw TestFailure("t.kind() returned %d, expected %d",
                                  static_cast<int>(t.kind()),
                                  static_cast<int>(TOK_NULL));
        }

        if (t.offset() != 0) {
                throw TestFailure("t.offset() returned %u, expected 0",
                                  t.offset());
        }

        if (t.line() != 0) {
                throw TestFailure("t.line() returned %u, expected 0", t.line());
        }

        if (t.column() != 0) {
                throw TestFailure("t.column() returned %u, expected 0",
                                  t.column());
        }
}

//--------------------------------------

void
wr::parse::TokenTests::copyAssignSingleChar() // static
{
        Token t;

        static const auto SPELLING = "x";

        t.setSpelling(SPELLING);

        Token t2;
        t2 = t;

        auto t2_spelling = t2.spelling();

        if (t2_spelling != SPELLING) {
                throw TestFailure("t2.spelling() returned \"%s\", expected \"%s\"",
                                 t2_spelling, SPELLING);
        }

        if (t2.next_.tag() != 0) {
                throw TestFailure("t2.next_.tag() returned %u, expected 0",
                                  t2.next_.tag());
        }

        if (t2_spelling.char_data() != t2.spelling_.buf_) {
                throw TestFailure("return value of t2.spelling() should reference internal buffer");
        }
}

//--------------------------------------

void
wr::parse::TokenTests::copyAssignMultiChar() // static
{
        Token t;

        static const auto SPELLING = "hello";

        t.setSpelling(SPELLING);

        Token t2;
        t2 = t;

        auto t2_spelling = t2.spelling();

        if (t2_spelling != SPELLING) {
                throw TestFailure("t2.spelling() returned \"%s\", expected \"%s\"",
                                 t2_spelling, SPELLING);
        }

        if (t2.next_.tag() != 1) {
                throw TestFailure("t2.next_.tag() returned %u, expected 1",
                                  t2.next_.tag());
        }

        if (t2_spelling.char_data() != SPELLING) {
                throw TestFailure("return value of t2.spelling() should reference input string");
        }
}

#if 0
//--------------------------------------

void
wr::parse::TokenTests::() // static
{
}

#endif
