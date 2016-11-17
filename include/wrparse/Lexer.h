/**
 * \file Lexer.h
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
#ifndef WRPARSE_LEXER_H
#define WRPARSE_LEXER_H

#include <list>
#include <memory>
#include <wrutil/string_view.h>
#include <wrutil/u8string_view.h>
#include <wrutil/uiostream.h>
#include <wrparse/Config.h>
#include <wrparse/Token.h>


namespace wr {
namespace parse {


class WRPARSE_API Lexer
{
public:
        using this_t = Lexer;

        static constexpr char32_t eof = std::char_traits<char32_t>::eof();

        Lexer(int line = 1, int column = 0);
        Lexer(std::istream &input, int line = 1, int column = 0);
        Lexer(const this_t &) = delete;
        Lexer(this_t &&other);

        virtual ~Lexer();

        virtual Token &lex(Token &out_token) = 0;
        virtual const char *tokenKindName(TokenKind kind) const = 0;

        int line() const              { return line_; }
        int column() const            { return column_; }
        std::streamoff offset() const { return offset_; }

        this_t &reset(int line = 1, int column = 0)
                { return reset(input_, line, column); }

        this_t &reset(std::istream &input, int line = 1, int column = 0);

        this_t &operator=(const this_t &) = delete;
        this_t &operator=(this_t &&other);

        virtual this_t &clearStorage();

protected:
        std::istream &input() { return input_; }

        virtual void onReset(std::istream &input, int line, int column)
                { (void) input; (void) line; (void) column; }

        char32_t peek();
        char32_t read();
        char32_t lastRead() const;
        void backtrack(size_t n_code_points = 1);
        void replace(size_t n_code_points, char32_t with_c);
        void erase(size_t n_code_points) { replace(n_code_points, eof); }

        void *store(const void *data, size_t size);

        u8string_view store(u8string_view s)
                { return { static_cast<const char *>
                           (store(s.data(), s.bytes())), s.bytes() }; }

private:
        enum : short {
                HISTORY_SIZE = 16
        };

        struct History
        {
                char32_t c;    ///< the character read
                uint8_t  bytes,  ///< no. of bytes taken by this character
                         lines;  ///< no. of lines to next character
                int16_t  columns;   ///< no. of columns taken by this character
        };

        History doRead();
        short historyIncrement(short pos);
        History historyNext();
        History historyAppend(History h);

        using StorageBufs = std::list<std::unique_ptr<char[]>>;
        using StorageBufsIter = StorageBufs::iterator;

        std::istream    input_;
        int             line_, column_;
        std::streamoff  offset_;
        History         history_[HISTORY_SIZE];  ///< backtracking ring buffer
        short           hist_begin_, hist_pos_, hist_end_;
        StorageBufs     storage_;
        StorageBufsIter first_free_buf_;
};


} // namespace parse
} // namespace wr


#endif // !WRPARSE_LEXER_H
