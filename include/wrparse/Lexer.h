/**
 * \file wrparse/Lexer.h
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


/**
 * \brief Base class for all lexers used with wrParse
 * \headerfile Lexer.h <wrparse/Lexer.h>
 *
 * All language-specific lexers must inherit from `wr::parse::Lexer` and
 * implement the `lex()` virtual method, which is responsible for the
 * entire lexing process, *i.e.* read the input stream and set the output
 * `Token`'s attributes as appropriate according to the input data. Lexing
 * may be implemented "by hand" or call upon externally-generated lexer
 * code such as that produced by [flex](https://github.com/westes/flex) or
 * [re2c](http://re2c.org). Whichever method of implementation is used,
 * from the wrParse library's point of view `Lexer::lex()` is a 'black box'
 * responsible for yielding the next input token on each call. `lex()` must
 * emit a token of type \c TOK_EOF upon reaching the end of the input
 * stream.
 *
 * ### Integrating Externally-Generated Lexers
 *      A generated lexer can be integrated by placing the necessary
 *      initialisation and finalisation code for it in the derived class'
 *      constructor and destructor respectively, then implementing `lex()`
 *      as follows:
 *
 *      1. For defensiveness it is recommended to reset all the output
 *         `Token` object's attributes to a known initial state by invoking
 *         `Token::reset()`
 *      2. Set the output `Token` object's offset to the lexer's current
 *         value as returned by `Lexer::offset()`
 *      3. Set any token flags as appropriate (`TF_SPACE_BEFORE`,
 *         `TF_STARTS_LINE` or any language-specific flags)
 *      4. Invoke the generated lexer code to read the input and obtain the
 *         spelling and type of the output `Token`; in case of a
 *         recoverable error set the token type to `TOK_NULL`
 *      5. If the token has a variable spelling for its type, store a copy
 *         of it (along with any other data if required) using
 *         `Lexer::store()` then invoke `Token::setSpelling()` with the
 *         copied string. If the token has a fixed spelling for its type,
 *         a constant string literal can be used instead.
 *         \note The token's spelling must be encoded as ASCII or UTF-8.
 *      6. Update the `Lexer` object's current line, column and input
 *         offset from the corresponding state of the generated lexer
 *         (`Lexer` tracks input offset because `std::istream::tellg()`
 *         may not work for all input stream types). A group of methods is
 *         provided for maintaining this state in step with a generated
 *         lexer:
 *         * `setLine()`, `setColumn()` and `setOffset()` update the line,
 *           column or input offset respectively with the absolute value
 *           passed
 *         * `bumpLine()`, `bumpColumn()` and `bumpOffset()` adjust the
 *           line, column or input offset respectively by the delta value
 *           passed
 *      7. Set any token flags that could not be set in step 3 above
 *
 * ### Implementing Handwritten Lexers
 *
 * ### Token Data Storage
 *      For token types with variable spellings or any other variable data
 *      specific to an individual `Token` object there are two methods that
 *      can be used to store a block of data associated with the token:
 *
 *      * `void *store(const void *data, size_t size)`: allocates a block
 *        of memory of the given `size` bytes, copying the memory bounded
 *        by <i>(`data`, `data` + `size`)</i> into the returned block
 *      * `wr::u8string_view store(wr::u8string_view s)`: copies the
 *        spelling `s` into a newly-allocated block of size `s.bytes()`,
 *        returning that block
 *
 *      \note If any auxiliary data is required for the `Token` object in
 *              addition to the spelling then one memory block should be
 *              allocated for both using `store(const void *, size_t)`
 *              with the auxiliary data located adjacent to the spelling
 *              string data. The data pointer can then be derived from the
 *              address of the string as returned by `Token::spelling()`
 *
 * \see class `Token`
 */
class WRPARSE_API Lexer
{
public:
        using this_t = Lexer;

        /// shorthand for character constant representing end-of-file
        static constexpr char32_t eof = std::char_traits<char32_t>::eof();

        /**
         * \brief Initialise a `Lexer` object
         *
         * \param [in] input   the source input stream
         * \param [in] line    starting line number
         * \param [in] column  starting column number
         *
         * \note `Lexer` creates its own input stream object which shares
         *      `input`'s stream buffer object. As a result, the lexer may
         *      outlive `input` but the stream buffer as returned by
         *      `input.rdbuf()` must outlive both `input` and the lexer.
         */
        Lexer(std::istream &input, int line = 1, int column = 0);

        /**
         * \brief Initialise a `Lexer` object
         *
         * The input source is set to `wr::uin`, a UTF-8 encoded version
         * of `std::cin`.
         *
         * \param [in] line    starting line number
         * \param [in] column  starting column number
         */
        Lexer(int line = 1, int column = 0);

        Lexer(const this_t &) = delete;
                ///< \details Copying of `Lexer` objects is prohibited.

        /**
         * \brief Transfer `other`'s complete state to `*this`
         * \param [in,out] other  object to be transferred
         */
        Lexer(this_t &&other);

        /// \brief Finalise a `Lexer` object
        virtual ~Lexer();

        /**
         * \brief Read next token from input
         *
         * This method must be implemented by an language-specific derived
         * class.
         *
         * \param [out] out_token
         *      the output `Token` object; initial contents are undefined
         *
         * \return reference to `out_token`
         */
        virtual Token &lex(Token &out_token) = 0;

        /**
         * \brief Return language-specific name corresponding to `kind`
         *
         * This method is intended to aid debugging and/or diagnostic
         * output. Language-specific derived classes should override this
         * function to return names for token types specific to the target
         * language.
         *
         * \param [in] kind  the requested token type
         *
         * \return `"NULL"` and `"EOF"` respectively for the generic token
         *      types `TOK_NULL` and `TOK_EOF`
         * \return `"?"` for all other values
         */
        virtual const char *tokenKindName(TokenKind kind) const;

        int line() const              { return line_; }
                        ///< \brief Obtain the current line number
        int column() const            { return column_; }
                        ///< \brief Obtain the current column number
        std::streamoff offset() const { return offset_; }
                        /**< \brief Obtain number of bytes read from
                                the beginning of the input */

        /**
         * \brief Reset lexer to new input stream, offset zero and clear
         *      history buffer
         *
         * The most-derived protected `onReset()` method is invoked to
         * give subclasses the opportunity to perform any necessary
         * changes to their state.
         *
         * Memory previously allocated by `store()` is not affected.
         *
         * \param [in] input   new input stream
         * \param [in] line    starting line number
         * \param [in] column  starting column number
         *
         * \return reference to `*this` object
         *
         * \note `Lexer` creates its own input stream object which shares
         *      `input`'s stream buffer object. As a result, the lexer may
         *      outlive `input` but the stream buffer as returned by
         *      `input.rdbuf()` must outlive both `input` and the lexer.
         */
        this_t &reset(std::istream &input, int line = 1, int column = 0);

        /**
         * \brief Reset lexer to offset zero and clear history buffer
         *
         * The original input stream is retained.
         *
         * The most-derived protected `onReset()` method is invoked to
         * give subclasses the opportunity to perform any necessary
         * changes to their state.
         *
         * Memory previously allocated by `store()` is not affected.
         *
         * \param [in] line    starting line number
         * \param [in] column  starting column number
         *
         * \return reference to `*this` object
         */
        this_t &reset(int line = 1, int column = 0)
                { return reset(input_, line, column); }

        this_t &operator=(const this_t &) = delete;
                ///< \details Copying of `Lexer` objects is prohibited.

        /**
         * \brief Transfer `other`'s complete state to `*this`
         * \param [in,out] other  object to be transferred
         */
        this_t &operator=(this_t &&other);

        /**
         * \brief Free all memory allocated for stored token data
         * \return reference to `*this` object
         * \warning After calling this method any attempts to reference
         *      certain data (spellings in particular) from tokens returned
         *      by a parser using this lexer will result in undefined
         *      behaviour (probably a segfault); ensure all tokens involved
         *      are finished with before invoking this method.
         */
        virtual this_t &clearStorage();

protected:
        /**
         * \brief Obtain input data source
         * \return
         *      reference to the input stream object. This object belongs
         *      to `Lexer` and is not a copy of the original stream passed
         *      to the constructor or to `reset()`. It shares the stream
         *      buffer from the original stream.
         */
        std::istream &input() { return input_; }

        /**
         * \brief Method invoked upon construction or a call to `reset()`
         *
         * Subclasses may override this method to make any necessary
         * changes to their state when the input stream is changed, for
         * example.
         *
         * This method is invoked before any internal state is changed, so
         * all other functions will return their original values.
         *
         * \warning Invoking `reset()` from this method will result in
         *      infinite recursion.
         *
         * \param [in] input
         *      the input stream passed by the caller of `reset()` or one
         *      of the constructors (except the move constructor)
         * \param [in] line
         *      the new line number (old line number is still available
         *      via `line()`)
         * \param [in] column
         *      the new column number (old column number is still available
         *      via `column()`)
         */
        virtual void onReset(std::istream &input, int line, int column)
                { (void) input; (void) line; (void) column; }

        /**
         * \name Functions for integrating generated lexers
         */
        ///@{
        void setLine(int line)                { line_ = line; }
                ///< \brief Set current line number to `line`
        void bumpLine(int delta)              { line_ += delta; }
                ///< \brief Adjust current line number by `delta`
        void setColumn(int column)            { column_ = column; }
                ///< \brief Set current column number to `column`
        void bumpColumn(int delta)            { column_ += delta; }
                ///< \brief Adjust current column number by `delta`
        void setOffset(std::streamoff offset) { offset_ = offset; }
                ///< \brief Set current input offset to `offset`
        void bumpOffset(std::streamoff delta) { offset_ += delta; }
                ///< \brief Adjust current input offset by `delta`
        ///@}

        /**
         * \name Functions for handwritten lexers
         */
        ///@{
        char32_t peek();  ///< \brief Look ahead at next input character
        char32_t read();  ///< \brief Consume and return next input character
        char32_t lastRead() const;
                ///< \brief Obtain character returned by last call to `read()`
        void backtrack(size_t n_code_points = 1);
                ///< \brief Move backwards through input history buffer
        void replace(size_t n_code_points, char32_t c);
                ///< \brief Replace characters in history buffer with `c`
        void erase(size_t n_code_points) { replace(n_code_points, eof); }
                ///< \brief Remove characters in history buffer
        ///@}

        /**
         * \name Functions for token data storage
         */
        ///@{
        /**
         * \brief Allocate zeroed memory block of `size` bytes
         *
         * The memory is freed by either a future call to `clearStorage()`
         * or upon expiry of the `Lexer` object, whichever happens first.
         *
         * \param [in] size  size of memory block in bytes
         *
         * \return address of the allocated memory block
         * \return `nullptr` if `size` is zero
         */
        char *allocate(size_t size);

        /**
         * \brief Allocate and return copy of memory block bounded by
         *      <i>(`data`, `size`)</i>
         *
         * The memory is freed by either a future call to `clearStorage()`
         * or upon expiry of the `Lexer` object, whichever happens first.
         *
         * \param [in] data  address of memory block to be copied
         * \param [in] size  size of memory block in bytes
         *
         * \return address of the copied memory block
         * \return `nullptr` if `size` is zero
         */
        char *store(const void *data, size_t size);

        /**
         * \brief Allocate and return copy of spelling `s`
         *
         * The memory is freed either by a future call to `clearStorage()`
         * or upon expiry of the `Lexer` object, whichever happens first.
         *
         * \param [in] s  spelling to be copied
         * \return copy of `s` stored by the lexer or `s` if `s` is empty
         */
        u8string_view store(u8string_view s);
        ///@}

private:
        Lexer(nullptr_t);  /* dummy delegate constructor to ensure vtable
                              is set inside bodies of other constructors */

        enum : short
        {
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
