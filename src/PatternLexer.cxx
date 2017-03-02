/**
 * \file PatternLexer.cxx
 *
 * \brief Regular expression-based lexer implementation
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
#include <array>
#include <assert.h>
#include <string>
#include <vector>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <wrutil/circ_fwd_list.h>
#include <wrutil/Format.h>
#include <wrutil/numeric_cast.h>
#include <wrparse/PatternLexer.h>


namespace wr {
namespace parse {


struct PatternLexer::Body
{
        using this_t         = Body;
        using PatternRefList = circ_fwd_list<Rule::Pattern *>;
        using Page           = std::array<PatternRefList, 256>;

        struct Match
        {
                size_t            end_pos_;
                Line              end_line_;
                Column            end_column_;
                Rule::Pattern    &pattern_;
                pcre2_match_data *re_match_;
                std::vector<int>  re_workspace_;
                TokenFlags        next_token_flags_;
                bool              matches_single_newline_;

                Match(
                        Rule::Pattern &pattern
                ) :
                        end_pos_ (0),
                        end_line_(0),
                        end_column_(0),
                        pattern_ (pattern),
                        re_match_(pcre2_match_data_create_from_pattern(
                                                (pcre2_code *) pattern_.re_,
                                                nullptr)),
                        re_workspace_(pattern.re_workspace_size_),
                        next_token_flags_(0),
                        matches_single_newline_(
                                        (pattern_.orig_str_ == R"(\R)")
                                        || (pattern_.orig_str_ == R"(\v)")
                                        || (pattern_.orig_str_ == "\n"))
                {
                }

                ~Match()
                {
                        pcre2_match_data_free(re_match_);
                }
        };

        using MatchList     = circ_fwd_list<Match>;
        using MatchIterator = MatchList::iterator;


        Body(PatternLexer &me, std::initializer_list<Rule> rules);

        void setMatchFirst(uint8_t char_val, Rule::Pattern &pattern);
        char32_t readChar();
        MatchIterator lex(Token &out_token);
        MatchIterator processMatch(MatchIterator prev, uint32_t pcre_options,
                                   bool &reprocess_end_char);


        PatternLexer        &me_;
        circ_fwd_list<Rule>  rules_;
        Page                 first_;
        PatternRefList       match_all_first_;
        MatchList            in_progress_;
        MatchIterator        last_incomplete_match_;
        std::string          buffer_;
        std::streamoff       base_offset_;
        size_t               buf_pos_,
                             match_start_;
        std::streamoff       last_read_bytes_;
        int                  last_read_lines_,
                             last_read_columns_;
};

//--------------------------------------

PatternLexer::Body::Body(
        PatternLexer                &me,
        std::initializer_list<Rule>  rules
) :
        me_                   (me),
        rules_                (rules),
        last_incomplete_match_(in_progress_.before_begin()),
        base_offset_          (me_.base_t::offset()),
        buf_pos_              (0),
        match_start_          (buf_pos_)
{
        int priority = 0;

        for (Rule &rule: rules_) {
                rule.priority_ = priority++;

                if (!rule.enabled_) {
                        continue;
                }

                for (Rule::Pattern &pattern: rule.patterns_) {
                        auto re     = static_cast<pcre2_code *>(pattern.re_);
                        auto status = static_cast<uint32_t>(-1);

                        if (pcre2_pattern_info(re, PCRE2_INFO_FIRSTCODETYPE,
                                               &status) == 0 && (status == 1)) {
                                uint32_t char_val;
        
                                if (pcre2_pattern_info(re,
                                                       PCRE2_INFO_FIRSTCODEUNIT,
                                                       &char_val) == 0) {
                                        setMatchFirst(
                                                numeric_cast<uint8_t>(char_val),
                                                pattern);
                                        continue;
                                }
                        }

                        const uint8_t *bitmap;

                        if ((pcre2_pattern_info(re, PCRE2_INFO_FIRSTBITMAP,
                                                &bitmap) != 0)
                                        || !bitmap || (bitmap[31] & 0x80)) {
                                match_all_first_.push_back(&pattern);
                                continue;
                        }

                        for (uint8_t c = 0; c < 255; ++c) {
                                if (bitmap[c >> 3] & (uint8_t(1) << (c & 7))) {
                                        setMatchFirst(c, pattern);
                                }
                        }
                }
        }
}

//--------------------------------------

void
PatternLexer::Body::setMatchFirst(
        uint8_t        char_val,
        Rule::Pattern &pattern
)
{
        first_[char_val].push_back(&pattern);
}

//--------------------------------------

char32_t
PatternLexer::Body::readChar()
{
        char32_t c;

        last_read_lines_ = 0;

        bool from_buffer = buf_pos_ < buffer_.size();

        if (from_buffer) {
                const uint8_t *pos, *end, *next_pos;
                pos = reinterpret_cast<uint8_t *>(&buffer_[buf_pos_]);
                end = reinterpret_cast<uint8_t *>(&buffer_[buffer_.size()]);
                c = utf8_char(pos, end, &next_pos);
                last_read_columns_ = 1;
                last_read_bytes_ = next_pos - pos;
                buf_pos_ += last_read_bytes_;
                me_.bumpOffset(last_read_bytes_);
                
        } else {
                auto orig_offset = me_.offset();
                c = me_.read();
                if (c == eof) {
                        last_read_columns_ = 0;
                } else {
                        utf8_append(buffer_, c);
                        buf_pos_ = buffer_.size();
                        last_read_columns_ = 1;
                        last_read_bytes_ = me_.offset() - orig_offset;
                }
        }

        auto flags = me_.nextTokenFlags();

        switch (c) {
        case U'\n': case U'\v': case U'\f': case 0x85: case 0x2028: case 0x2029:
                last_read_lines_ = 1;
                last_read_columns_ = -me_.column();
                flags = (flags & ~TF_SPACE_BEFORE) | TF_STARTS_LINE;
                break;
        default:
                if (isuspace(c)) {
                        flags |= TF_SPACE_BEFORE;
                } else {
                        flags &= ~(TF_SPACE_BEFORE | TF_STARTS_LINE);
                }
                break;
        }

        me_.setNextTokenFlags(flags);

        if (from_buffer) {
                me_.bumpLine(last_read_lines_);
                me_.bumpColumn(last_read_columns_);
        }

        return c;
}

//--------------------------------------

auto
PatternLexer::Body::lex(
        Token &out_token
) -> MatchIterator
{
        in_progress_.clear();
        last_incomplete_match_ = in_progress_.before_begin();
        match_start_ = buf_pos_;

        char32_t c = readChar();

        if (c == eof) {
                out_token.setKind(TOK_EOF);
                return in_progress_.end();
        }

        assert(c < wr::ucd::CODE_SPACE_SIZE);

        auto key = static_cast<uint8_t>(buffer_[buf_pos_ - last_read_bytes_]);
        auto i_first = first_[key].begin(),
             i_all = match_all_first_.begin();

        while (i_first || i_all) {
                if (!i_all || (i_first && ((*i_first)->rule_->priority_
                                           > (*i_all)->rule_->priority_))) {
                        in_progress_.emplace_back(**i_first);
                        ++i_first;
                } else {
                        in_progress_.emplace_back(**i_all);
                        ++i_all;
                }
        }

        last_incomplete_match_ = in_progress_.last();

        uint32_t pcre_options = PCRE2_NOTBOL | PCRE2_NOTEMPTY_ATSTART
                                             | PCRE2_PARTIAL_SOFT;
        bool     at_eof       = false;

        while (last_incomplete_match_) {
                auto prev           = in_progress_.before_begin();
                bool reprocess_char = false;

                do {
                        prev = processMatch(prev, pcre_options, reprocess_char);
                } while (prev != last_incomplete_match_);

                pcre_options |= PCRE2_DFA_RESTART;

                if (last_incomplete_match_) {
                        if (readChar() == eof) {
                                /* terminate matching and leave it
                                   for next call to return TOK_EOF token */
                                at_eof = true;
                                pcre_options = 0;
                        }
                } else if (reprocess_char && !at_eof) {
                        buf_pos_ -= last_read_bytes_;
                        me_.bumpOffset(last_read_bytes_);
                        me_.bumpLine(last_read_lines_);
                        me_.bumpColumn(last_read_columns_);
                }
        }

        return std::next(last_incomplete_match_);
}

//--------------------------------------

auto
PatternLexer::Body::processMatch(
        MatchIterator prev,
        uint32_t      pcre_options,  // set to zero on EOF
        bool         &reprocess_end_char
) -> MatchIterator
{
        auto   i         = std::next(prev);
        int    status;
        size_t start_pos = buf_pos_ - last_read_bytes_;

        if (!pcre_options) {
                status = PCRE2_ERROR_NOMATCH;  // terminate matching
        } else do {
                status = pcre2_dfa_match(
                                static_cast<pcre2_code *>(i->pattern_.re_),
                                reinterpret_cast<PCRE2_SPTR>(&buffer_[0]),
                                buf_pos_, start_pos, pcre_options,
                                i->re_match_, nullptr,
                                &i->re_workspace_[0], i->re_workspace_.size());

                if (status == PCRE2_ERROR_DFA_WSSIZE) {
                        i->re_workspace_
                                .resize(i->pattern_.re_workspace_size_ *= 2);
                        start_pos = 0;
                        pcre_options &= ~PCRE2_DFA_RESTART;
                }
        } while (status == PCRE2_ERROR_DFA_WSSIZE);

        bool finish = false;

        if (status >= 0 || (status == PCRE2_ERROR_PARTIAL)) {
                if (status >= 0) {
                        // matched - try to match more
                        i->end_pos_ = buf_pos_;
                        i->end_line_ = me_.line();
                        i->end_column_ = me_.column();
                        i->next_token_flags_ = me_.nextTokenFlags();
                        /* HACK: if original pattern string is a simple newline,
                           finish now (friendlier for interactive lexing) */
                        finish = i->matches_single_newline_;
                }
        } else {
                finish = true;
        }

        if (finish) {  // no further matches possible for this pattern
                reprocess_end_char = i->end_pos_ && !i->matches_single_newline_;
                auto existing = std::next(last_incomplete_match_);

                if (last_incomplete_match_ == i) {
                        last_incomplete_match_ = prev;
                }

                if (!i->end_pos_
                    || (existing && (existing->end_pos_ >= i->end_pos_))) {
                        /* 'i' did not match at all, or 'existing' points at
                           match of equal/greater length and higher priority */
                        in_progress_.erase_after(prev);
                } else if (prev != last_incomplete_match_) {
                        in_progress_.splice_after(last_incomplete_match_,
                                                  in_progress_,
                                                  prev, std::next(prev, 2));
                }
        } else {
                ++prev;
        }

        return prev;
}

//--------------------------------------

WRPARSE_API
PatternLexer::Rule::Rule(
        u8string_view pattern,
        ExplicitBool  enable
) :
        Rule({ pattern }, nullptr, enable)
{
}

//--------------------------------------

WRPARSE_API
PatternLexer::Rule::Rule(
        std::initializer_list<u8string_view> patterns,
        Action                               action,
        ExplicitBool                         enable
) :
        enabled_(enable),
        action_ (std::move(action))
{
        const char *error_sep = "";

        for (const u8string_view &pattern: patterns) {
                int    pcre_error_code;
                size_t error_at;
                auto   re = pcre2_compile(pattern.data(), pattern.bytes(),
                                          PCRE2_DOTALL | PCRE2_MULTILINE
                                                       | PCRE2_UCP | PCRE2_UTF,
                                          &pcre_error_code, &error_at, nullptr);
                if (re) {
                        patterns_.emplace_back(this, pattern, re);
                } else if (pcre_error_code == PCRE2_ERROR_NOMEMORY) {
                        throw std::bad_alloc();
                } else {
                        std::string msg(128, '\0');
                        while (true) {
                                int len;
                                len = pcre2_get_error_message(pcre_error_code,
                                        (PCRE2_UCHAR *) &msg[0], msg.size());

                                if (len >= 0) {
                                        msg.resize(len);
                                        break;
                                }

                                msg.resize(msg.size() * 2);
                        }
                        throw std::runtime_error(
                                printStr("at offset %u in pattern \"%s\": %s",
                                         error_at, pattern, msg));
                }
        }
}

//--------------------------------------

WRPARSE_API
PatternLexer::Rule::Rule(
        u8string_view pattern,
        Action        action,
        ExplicitBool  enable
) :
        Rule({ pattern }, std::move(action), enable)
{
}

//--------------------------------------

WRPARSE_API
PatternLexer::Rule::Rule(
        std::initializer_list<u8string_view> patterns,
        ExplicitBool                         enable
) :
        Rule(patterns, nullptr, enable)
{
}

//--------------------------------------

WRPARSE_API
PatternLexer::Rule::Rule(
        const this_t &other
)
{
        *this = other;
}

//--------------------------------------

WRPARSE_API
PatternLexer::Rule::Rule(
        this_t &&other
)
{
        *this = std::move(other);
}

//--------------------------------------

WRPARSE_API auto
PatternLexer::Rule::operator=(
        const this_t &other
) -> this_t &
{
        if (&other != this) {
                enabled_ = other.enabled_;
                priority_ = other.priority_;
                patterns_ = other.patterns_;
                action_ = other.action_;
                updatePatterns();
        }

        return *this;
}

//--------------------------------------

WRPARSE_API auto
PatternLexer::Rule::operator=(
        this_t &&other
) -> this_t &
{
        if (&other != this) {
                enabled_ = other.enabled_;
                priority_ = other.priority_;
                patterns_ = std::move(other.patterns_);
                action_ = std::move(other.action_);
                updatePatterns();
        }

        return *this;
}

//--------------------------------------

void
PatternLexer::Rule::updatePatterns()
{
        for (Pattern &pattern: patterns_) {
                pattern.rule_ = this;
        }
}

//--------------------------------------

enum { DEFAULT_RE_WORKSPACE_SIZE = 20 };

PatternLexer::Rule::Pattern::Pattern() :
        rule_             (nullptr),
        re_               (nullptr),
        re_workspace_size_(DEFAULT_RE_WORKSPACE_SIZE)
{
}

//--------------------------------------

PatternLexer::Rule::Pattern::Pattern(
        Rule                *rule,
        const u8string_view &orig_str,
        void                *re
) :
        rule_             (rule),
        orig_str_         (orig_str.to_string()),
        re_               (re),
        re_workspace_size_(DEFAULT_RE_WORKSPACE_SIZE)
{
}

//--------------------------------------

PatternLexer::Rule::Pattern::Pattern(
        const this_t &other
) :
        this_t()
{
        *this = other;
}

//--------------------------------------

PatternLexer::Rule::Pattern::Pattern(
        this_t &&other
) :
        this_t()
{
        *this = std::move(other);
}

//--------------------------------------

PatternLexer::Rule::Pattern::~Pattern()
{
        free();
}

//--------------------------------------

void
PatternLexer::Rule::Pattern::free()
{
        pcre2_code_free(static_cast<pcre2_code *>(re_));
}

//--------------------------------------

auto
PatternLexer::Rule::Pattern::operator=(
        const this_t &other
) -> this_t &
{
        if (&other != this) {
                free();
                rule_ = other.rule_;
                orig_str_ = other.orig_str_;
                re_workspace_size_ = other.re_workspace_size_;

                if (other.re_) {
                        re_ = pcre2_code_copy(
                                        static_cast<pcre2_code *>(other.re_));
                        if (!re_) {
                                throw std::bad_alloc();
                        }
                }
        }

        return *this;
}

//--------------------------------------

auto
PatternLexer::Rule::Pattern::operator=(
        this_t &&other
) -> this_t &
{
        if (&other != this) {
                free();
                rule_ = other.rule_;
                orig_str_ = std::move(other.orig_str_);
                re_workspace_size_ = other.re_workspace_size_;
                std::swap(re_, other.re_);
        }

        return *this;
}

//--------------------------------------

WRPARSE_API
PatternLexer::PatternLexer(
        std::initializer_list<Rule> rules
) :
        body_(new Body(*this, rules))
{
}

//--------------------------------------

WRPARSE_API
PatternLexer::PatternLexer(
        std::istream                &input,
        std::initializer_list<Rule>  rules
) :
        base_t(input),
        body_ (new Body(*this, rules))
{
}

//--------------------------------------

WRPARSE_API Token &
PatternLexer::lex(
        Token &out_token
)
{
        bool repeat;

        do {
                repeat = false;
                base_t::lex(out_token);  // initialise token

                auto match = body_->lex(out_token);

                if (match) {
                        setOffset(body_->base_offset_
                               + numeric_cast<std::streamoff>(match->end_pos_));
                        setLine(match->end_line_);
                        setColumn(match->end_column_);
                        body_->buf_pos_ = match->end_pos_;
                        setNextTokenFlags(match->next_token_flags_);

                        const Rule::Pattern &pattern = match->pattern_;

                        if (pattern.rule_->action_) {
                                out_token.setSpelling(pattern.orig_str_);
                                                        // provisional spelling
                                pattern.rule_->action_(out_token);
                        }

                        repeat = (out_token.kind() == TOK_NULL);
                                // ignore; lex another token

                        // keep buffer size under control
                        if ((body_->buffer_.size() - body_->buf_pos_) < 4) {
                                body_->base_offset_ +=
                                        numeric_cast<std::streamoff>(
                                                body_->buf_pos_);
                                body_->buffer_.erase(0, body_->buf_pos_);
                                body_->buf_pos_ = 0;
                        }
                } else if (!out_token.is(TOK_EOF)) {
                        // out_token contains original offset/line/column
                        body_->buf_pos_ -= offset() - out_token.offset();
                        setOffset(out_token.offset());
                        setLine(out_token.line());
                        setColumn(out_token.column());
                        char c = body_->readChar();
                        emit(Diagnostic::ERROR, out_token.offset(),
                             utf8_seq_size(c), out_token.line(),
                             out_token.column(), "Illegal character '%c'", c);
                }
        } while (repeat);

        return out_token;
}

//--------------------------------------

WRPARSE_API wr::u8string_view
PatternLexer::matched() const
{
        return { &body_->buffer_[body_->match_start_],
                 body_->buf_pos_ - body_->match_start_ };
}


} // namespace parse
} // namespace wr
