#include <array>
#include <assert.h>
#include <string>
#include <vector>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <wrutil/circ_fwd_list.h>
#include <wrutil/Format.h>
#include <wrparse/PatternLexer.h>


namespace wr {
namespace parse {


struct PatternLexer::Body
{
        using this_t         = Body;
        using PatternRefList = circ_fwd_list<Rule::Pattern *>;
        using Page           = std::array<PatternRefList, 256>;
        using FirstTable     = std::array<uint16_t, 4352>; // = (0x110000 / 256)

        struct Match
        {
                size_t            end_pos_;
                Rule::Pattern    &pattern_;
                pcre2_match_data *re_match_;
                std::vector<int>  re_workspace_;
                std::streamoff    bytes_;
                int               lines_,
                                  columns_;

                Match(
                        Rule::Pattern &pattern
                ) :
                        end_pos_     (0),
                        pattern_     (pattern),
                        re_match_    (pcre2_match_data_create_from_pattern(
                                                (pcre2_code *) pattern_.re_,
                                                nullptr)),
                        re_workspace_(pattern.re_workspace_size_)
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

        void setMatchFirst(char32_t char_val, Rule::Pattern &pattern);
        char32_t readChar();
        MatchIterator lex(Token &out_token);
        MatchIterator processMatch(MatchIterator prev, uint32_t pcre_options);


        PatternLexer        &me_;
        circ_fwd_list<Rule>  rules_;
        FirstTable           first_table_;
        std::vector<Page>    pages_;
        PatternRefList       match_all_first_;
        MatchList            in_progress_;
        MatchIterator        last_incomplete_match_;
        std::string          buffer_;
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
        pages_                (1),
        rules_                (rules),
        last_incomplete_match_(in_progress_.before_begin()),
        buf_pos_              (0),
        match_start_          (buf_pos_)
{
        first_table_.fill(0);

        int priority = 0;

        for (Rule &rule: rules_) {
                rule.priority_ = priority++;

                if (!rule.enabled_) {
                        continue;
                }

                for (Rule::Pattern &pattern: rule.patterns_) {
                        auto     re = static_cast<pcre2_code *>(pattern.re_);
                        uint32_t status = -1U;

                        if (pcre2_pattern_info(re, PCRE2_INFO_FIRSTCODETYPE,
                                               &status) == 0 && (status == 1)) {
                                int32_t char_val;
        
                                if (pcre2_pattern_info(re,
                                                       PCRE2_INFO_FIRSTCODEUNIT,
                                                       &char_val) == 0) {
                                        setMatchFirst(char_val, pattern);
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
                                if (bitmap[c >> 3] & (uint8_t(1) << (c & 3))) {
                                        setMatchFirst(c, pattern);
                                }
                        }
                }
        }
}

//--------------------------------------

void
PatternLexer::Body::setMatchFirst(
        char32_t       char_val,
        Rule::Pattern &pattern
)
{
        if (char_val >= wr::ucd::CODE_SPACE_SIZE) {
                throw std::logic_error(printStr("%s:%d: character %#x > max %x",
                                       __FILE__, __LINE__, char_val,
                                       wr::ucd::CODE_SPACE_SIZE - 1));
        }

        auto &page_no = first_table_[char_val >> 8];

        if (page_no == 0) {
                page_no = pages_.size();
                pages_.emplace_back();
        }

        Page &page = pages_.at(page_no);
        page[char_val & 0xff].push_back(&pattern);
}

//--------------------------------------

char32_t
PatternLexer::Body::readChar()
{
        std::istream &input = me_.input();
        int           c;
        size_t        orig_buf_pos = buf_pos_;

        if (buf_pos_ < buffer_.size()) {
                c = buffer_[buf_pos_++];
        } else {
                c = input.get();
                if (c == std::char_traits<char>::eof()) {
                        return base_t::eof;
                }
                buffer_ += c;
                ++buf_pos_;
        }

        last_read_lines_ = 0;
        last_read_columns_ = 1;

        char32_t result  = c;
        bool     invalid = false;

        switch ((c >> 4) & 0xf) {
        default:  // 0 - 7
                switch (c) {
                default:
                        break;
                case U'\n': case U'\v': case U'\f':
                        last_read_lines_ = 1;
                        last_read_columns_ = -me_.column();
                        break;
                }
                last_read_bytes_ = 1;
                return result;
        case 8: case 9: case 10: case 11:
                last_read_bytes_ = 1;
                invalid = true;
                break;
        case 12: case 13:       // 110xxxxx 10xxxxxx
                last_read_bytes_ = 2;
                result &= 0x1f;
                break;
        case 14:                // 1110xxxx 10xxxxxx 10xxxxxx
                last_read_bytes_ = 3;
                result &= 0xf;
                break;
        case 15:                // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
                last_read_bytes_ = 4;
                result &= 7;
                break;
        }

        int bytes = last_read_bytes_ - 1;

        for (; (buf_pos_ < buffer_.size()) && (bytes > 0); --bytes) {
                c = buffer_[buf_pos_++];
                result = (result << 6) | (c & 0x3f);
        }

        for (; bytes > 0; buffer_ += c, ++buf_pos_, --bytes) {
                c = input.get();
                if (((c & 0xc0) != 0x80)
                                || (c == std::char_traits<char>::eof())) {
                        invalid = true;
                        break;
                }
                result = (result << 6) | (c & 0x3f);
        }

        if (invalid) {
                buffer_.resize(orig_buf_pos);
                utf8_append(buffer_, INVALID_CHAR);
                buf_pos_ = buffer_.size();
                last_read_bytes_ = buf_pos_ - orig_buf_pos;
        }

        return result;
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

        auto &page_no = first_table_[c >> 8];
        auto  i_page  = pages_[page_no][c & 0xff].begin(),
              i_all   = match_all_first_.begin();

        while (i_page || i_all) {
                if (!i_all || (i_page && ((*i_page)->rule_->priority_
                                          > (*i_all)->rule_->priority_))) {
                        in_progress_.emplace_back(**i_page);
                        ++i_page;
                } else {
                        in_progress_.emplace_back(**i_all);
                        ++i_all;
                }
        }

        last_incomplete_match_ = in_progress_.last();

        uint32_t pcre_options = PCRE2_NOTBOL | PCRE2_NOTEMPTY_ATSTART;
        bool     at_eof       = false;

        while (last_incomplete_match_) {
                auto prev = in_progress_.before_begin();

                do {
                        prev = processMatch(prev, pcre_options);
                } while (prev != last_incomplete_match_);

                pcre_options |= PCRE2_DFA_RESTART | PCRE2_PARTIAL_SOFT;

                if (last_incomplete_match_) {
                        if (readChar() == eof) {
                                /* terminate matching and leave it
                                   for next call to return TOK_EOF token */
                                at_eof = true;
                                pcre_options = 0;
                        }
                } else if (!at_eof) {
                        buf_pos_ -= last_read_bytes_;
                }
        }

        return std::next(last_incomplete_match_);
}

//--------------------------------------

auto
PatternLexer::Body::processMatch(
        MatchIterator prev,
        uint32_t      pcre_options  // set to zero on EOF
) -> MatchIterator
{
        auto i      = std::next(prev);
        int  status;

        if (!pcre_options) {
                status = PCRE2_ERROR_NOMATCH;  // terminate matching
        } else do {
                status = pcre2_dfa_match(
                                static_cast<pcre2_code *>(i->pattern_.re_),
                                reinterpret_cast<PCRE2_SPTR>(&buffer_[0]),
                                buf_pos_, buf_pos_ - last_read_bytes_,
                                pcre_options, i->re_match_, nullptr,
                                &i->re_workspace_[0], i->re_workspace_.size());

                if (status == PCRE2_ERROR_DFA_WSSIZE) {
                        i->re_workspace_
                                .resize(i->pattern_.re_workspace_size_ *= 2);
                }
        } while (status == PCRE2_ERROR_DFA_WSSIZE);

        if (status >= 0) {  // matched - try to match more
                i->end_pos_ = buf_pos_;
                ++prev;
        } else if (status == PCRE2_ERROR_PARTIAL) {
                i->bytes_ += last_read_bytes_;
                i->lines_ += last_read_lines_;
                i->columns_ += last_read_columns_;
                ++prev;
        } else {  // no further matches
                auto existing = std::next(last_incomplete_match_);

                if (last_incomplete_match_ == i) {
                        last_incomplete_match_ = prev;
                }

                if (!i->end_pos_
                    || (existing && (existing->end_pos_ >= i->end_pos_))) {
                        /* 'i' did not match at all, or 'existing' points at
                           match of equal/greater length and higher priority */
                        in_progress_.erase_after(prev);
                } else if (i != in_progress_.last()) {
                        in_progress_.splice_after(in_progress_.last(),
                                                  in_progress_,
                                                  prev, std::next(prev, 2));
                }
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

                auto re = pcre2_compile(pattern.data(), pattern.bytes(),
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
                                size_t len;
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
                out_token.reset();
                out_token.setOffset(offset());

                auto match = body_->lex(out_token);

                if (match) {
                        bumpLine(match->lines_);
                        bumpColumn(match->columns_);
                        bumpOffset(match->bytes_);

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
                                body_->buffer_.erase(0, body_->buf_pos_);
                                body_->buf_pos_ = 0;
                        }
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
