/**
 * \file Lexer.cxx
 *
 * \brief Basic lexer infrastructure
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
#include <assert.h>
#include <string.h>
#include <stdexcept>
#include <wrutil/optional.h>
#include <wrutil/uiostream.h>
#include <wrutil/UnicodeData.h>  // wr::INVALID_CHAR
#include <wrparse/Lexer.h>


namespace wr {
namespace parse {


WRPARSE_API
Lexer::Lexer(
        nullptr_t
) :
        input_         (nullptr),
        line_          (0),
        column_        (0),
        offset_        (0),
        hist_begin_    (0),
        hist_pos_      (-1),
        hist_end_      (-1),
        first_free_buf_(storage_.end())
{
}

//--------------------------------------

WRPARSE_API
Lexer::Lexer(
        int line,
        int column
) :
        this_t(uin, line, column)
{
}

//--------------------------------------

WRPARSE_API
Lexer::Lexer(
        std::istream &input,
        int           line,
        int           column
) :
        this_t()
{
        reset(input, line, column);
}

//--------------------------------------

WRPARSE_API
Lexer::Lexer(
        this_t &&other
) :
        input_(nullptr)
{
        operator=(std::move(other));
}

//--------------------------------------

WRPARSE_API Lexer::~Lexer() = default;

//--------------------------------------

WRPARSE_API const char *
Lexer::tokenKindName(
        TokenKind kind
) const
{
        switch (kind) {
        case TOK_NULL: return "NULL";
        case TOK_EOF:  return "EOF";
        default:       return "?";
        }
}

//--------------------------------------

WRPARSE_API auto
Lexer::reset(
        std::istream &input,
        int           line,
        int           column
) -> this_t &
{
        onReset(input, line, column);
        input_.rdbuf(input.rdbuf());
        line_ = line;
        column_ = column;
        offset_ = 0;
        hist_pos_ = hist_end_ = -1;
        hist_begin_ = 0;
        return *this;
}

//--------------------------------------

WRPARSE_API char32_t
Lexer::peek()
{
        auto hist = historyNext();

        if (hist.bytes > 0) {
                return hist.c;
        } else {
                return doRead().c;
        }
}

//--------------------------------------

WRPARSE_API char32_t
Lexer::read()
{
        auto hist = historyNext();

        if (hist.bytes > 0) {
                hist_pos_ = historyIncrement(hist_pos_);
        } else {
                hist = doRead();
                hist_pos_ = hist_end_;
        }

        offset_ += hist.bytes;
        column_ += hist.columns;
        line_ += hist.lines;
        return hist.c;
}

//--------------------------------------

WRPARSE_API char32_t
Lexer::lastRead() const
{
        if (hist_pos_ < 0) {
                return eof;
        } else {
                return history_[hist_pos_].c;
        }
}

//--------------------------------------

WRPARSE_API void
Lexer::backtrack(
        size_t n_code_points
)
{
        while (n_code_points--) {
                if (hist_pos_ == hist_begin_) {
                        throw std::runtime_error(
                                "Lexer::backtrack(): history exhausted");
                }

                if (hist_pos_ == 0) {
                        hist_pos_ = HISTORY_SIZE - 1;
                } else {
                        --hist_pos_;
                }

                offset_ -= history_[hist_pos_].bytes;
                column_ -= history_[hist_pos_].columns;
                line_ -= history_[hist_pos_].lines;
        }
}

//--------------------------------------

WRPARSE_API void
Lexer::replace(
        size_t   n_code_points,
        char32_t with_c
)
{
        if (!n_code_points) {
                return;
        }

        short   src = hist_pos_;
        int16_t columns = 0;
        uint8_t bytes = 0, lines = 0;

        if (hist_pos_ < 0) {
                throw std::runtime_error("Lexer::replace(): history exhausted");
        }

        while (true) {
                bytes += history_[hist_pos_].bytes;
                columns += history_[hist_pos_].columns;
                lines += history_[hist_pos_].lines;

                if (--n_code_points == 0) {
                        break;
                } else if (hist_pos_ == hist_begin_) {
                        throw std::runtime_error(
                                "Lexer::replace(): history exhausted");
                } else if (hist_pos_ == 0) {
                        hist_pos_ = HISTORY_SIZE - 1;
                } else {
                        --hist_pos_;
                }
        }

        if (with_c == eof) {  // no replacement, just erase
                if (hist_pos_ == hist_begin_) {
                        hist_pos_ = -1;
                } else {
                        if (hist_pos_ == 0) {
                                hist_pos_ = HISTORY_SIZE - 1;
                        } else {
                                --hist_pos_;
                        }
                        history_[hist_pos_].bytes += bytes;
                        history_[hist_pos_].lines += lines;
                        history_[hist_pos_].columns += columns;
                }
        } else {
                history_[hist_pos_] = { with_c, bytes, lines, columns };
        }

        if (src == hist_end_) {
                hist_end_ = hist_pos_;
        } else if (historyIncrement(hist_pos_) != src) {
                short dst = hist_pos_;

                // move old history contents down
                do {
                        src = historyIncrement(src);
                        dst = historyIncrement(dst);
                        history_[dst] = history_[src];
                } while (src != hist_end_);

                hist_end_ = dst;
        }
}

//--------------------------------------

WRPARSE_API Lexer &
Lexer::operator=(
        Lexer &&other
)
{
        if (this != &other) {
                input_.rdbuf(other.input_.rdbuf());
                other.input_.rdbuf(nullptr);
                line_ = other.line_;
                other.line_ = 1;
                column_ = other.column_;
                other.column_ = 0;
                offset_ = other.offset_;
                other.offset_ = 0;
                hist_begin_ = other.hist_begin_;
                other.hist_begin_ = 0;
                hist_pos_ = other.hist_pos_;
                hist_end_ = other.hist_end_;
                other.hist_pos_ = other.hist_end_ = -1;
                storage_ = std::move(other.storage_);
                first_free_buf_ = other.first_free_buf_;
                other.first_free_buf_ = other.storage_.end();
        }
        return *this;
}

//--------------------------------------

WRPARSE_API char *
Lexer::allocate(
        size_t size
)
{
        // want to keep this implementation detail out of the header file
        enum { STORAGE_BUF_SIZE = 16384 - sizeof(size_t) };

        struct StorageBuf
        {
                size_t room;
                char   data[STORAGE_BUF_SIZE];
        };

        StorageBufs::value_type own_buf, *buf = nullptr;
        StorageBufsIter         ibuf = storage_.end();

        if (size >= STORAGE_BUF_SIZE) {
                own_buf.reset(new char[sizeof(size_t) + size]);
                buf = &own_buf;
                reinterpret_cast<StorageBuf *>(own_buf.get())->room = size;
        } else if (first_free_buf_ != storage_.end()) {
                auto &b = *reinterpret_cast<StorageBuf *>
                                                (storage_.back().get());
                if (b.room >= size) {
                        buf = &storage_.back();
                        ibuf = std::prev(storage_.end());
                }
        }

        if (!buf) {
                storage_.emplace_back(new char[sizeof(StorageBuf)]);
                reinterpret_cast<StorageBuf *>(storage_.back().get())
                                                      ->room = STORAGE_BUF_SIZE;
                buf = &storage_.back();
                ibuf = std::prev(storage_.end());
                if (first_free_buf_ == storage_.end()) {
                        first_free_buf_ = ibuf;
                }
        }

        auto &sp_buf = *reinterpret_cast<StorageBuf *>(buf->get());
        assert(size <= sp_buf.room);
        char *stored = &sp_buf.data[STORAGE_BUF_SIZE] - sp_buf.room;
        sp_buf.room -= size;

        if (!own_buf) {
                assert(ibuf != storage_.end());

                if (!sp_buf.room) {  // is now full
                        if (ibuf == first_free_buf_) {
                                first_free_buf_ = storage_.end();
                        } else {
                                storage_.splice(
                                        first_free_buf_, storage_, ibuf);
                        }
                } else {
                        auto ibuf2 = ibuf;
                        while ((ibuf2 != storage_.begin())
                                                && (ibuf2 != first_free_buf_)) {
                                auto &sp_buf2 = *reinterpret_cast<StorageBuf *>
                                        ((--ibuf2)->get());
                                if (sp_buf2.room <= sp_buf.room) {
                                        ++ibuf2;
                                        break;
                                }
                        }
                        if (ibuf2 != ibuf) {
                                storage_.splice(ibuf2, storage_, ibuf);
                                if (ibuf2 == first_free_buf_) {
                                        --first_free_buf_;
                                }
                        }
                }
        }

        return stored;
}

//--------------------------------------

WRPARSE_API char *
Lexer::store(
        const void *data,
        size_t      size
)
{
        char *stored = allocate(size);
        memcpy(stored, data, size);
        return stored;
}

//--------------------------------------

WRPARSE_API Lexer &
Lexer::clearStorage()
{
        storage_.clear();
        first_free_buf_ = storage_.end();
        return *this;
}

//--------------------------------------

Lexer::History
Lexer::doRead()
{
        char32_t result = input_.get();
        uint8_t  bytes, lines;
        int16_t  columns = 1;

        switch ((result >> 4) & 0xf) {
        default:  // 0 - 11
                switch (result) {
                default:
                        lines = 0;
                        break;
                case U'\n': case U'\v': case U'\f':
                        lines = 1;
                        columns = -column_;
                        break;
                }
                return historyAppend({ result, 1, lines, columns });
        case 12: case 13:       // 110xxxxx 10xxxxxx
                bytes = 2;
                result &= 0x1f;
                break;
        case 14:                // 1110xxxx 10xxxxxx 10xxxxxx
                bytes = 3;
                result &= 0xf;
                break;
        case 15:                // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
                bytes = 4;
                result &= 7;
                break;
        }

        for (unsigned i = 1; i < bytes; ++i) {
                auto c = input_.get();
                if ((c & 0xc0) == 0x80) {
                        result = (result << 6) | (c & 0x3f);
                } else if (c == std::istream::traits_type::eof()) {
                        bytes = i;
                        columns = 0;
                        result = eof;
                        break;
                } else {
                        bytes = i;
                        result = INVALID_CHAR;
                        break;
                }
        }

        switch (result) {
        default:
                lines = 0;
                break;
        case U'\n': case U'\v': case U'\f': case 0x85: case 0x2028: case 0x2029:
                lines = 1;
                columns = -column_;
                break;
        }

        return historyAppend({ result, bytes, lines, columns });
}

//--------------------------------------

short
Lexer::historyIncrement(
        short pos
)
{
        if (pos < 0) {
                return hist_begin_;
        } else {
                if (++pos < HISTORY_SIZE) {
                        return pos;
                } else {
                        return 0;
                }
        }
}

//--------------------------------------

Lexer::History
Lexer::historyNext()
{
        if (hist_pos_ != hist_end_) {
                return history_[historyIncrement(hist_pos_)];
        } else {
                return { eof, 0, 0 };
        }
}

//--------------------------------------

Lexer::History
Lexer::historyAppend(
        History h
)
{
        if (hist_end_ < 0) {
                hist_end_ = hist_begin_;
        } else {
                hist_end_ = historyIncrement(hist_end_);
                if (hist_end_ == hist_begin_) {
                        assert(hist_pos_ != hist_begin_);
                        hist_begin_ = historyIncrement(hist_begin_);
                }
        }

        history_[hist_end_] = h;
        return h;
}


} // namespace parse
} // namespace wr
