/**
 * \file wrparse/Token.h
 *
 * \brief Token handling infrastructure
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
#ifndef WRPARSE_TOKEN_H
#define WRPARSE_TOKEN_H

#include <wrutil/u8string_view.h>
#include <wrparse/Config.h>


namespace wr {
namespace parse {


typedef uint16_t TokenKind;   /**< A token's meaning in the target language */
typedef uint16_t TokenFlags;  /**< Bit flags set on individual tokens to mark
                                   special aspects (e.g. beginning of line) */

class Parser;


/**
 * \brief Basic token types
 *
 * Language implementations are expected to define their own `TOK_*`
 * enumeration constants of value `TOK_USER_MIN` and upwards (values below
 * this constant are reserved for the wrParse library). These constants are
 * used by the lexer and parser to represent terminals. Only generic token
 * types are defined here, which should be handled by all languages.
 *
 * \note Language-specific token enumeration types \e must specify
 *      `TokenKind` as their base type otherwise compilation will fail when
 *      using the constants in grammar rule definitions.
 */
enum : TokenKind
{
        TOK_NULL = 0,          /**< used to express 'no token' or 'any token'
                                    depending on context; otherwise not
                                    considered a valid type */
        TOK_EOF,               ///< represents end of input file
        TOK_USER_MIN = 0x0400  ///< first language-specific ID
};

//--------------------------------------
/**
 * \brief Basic token bit flags
 *
 * `TokenFlags` is a 16-bit field, the top eight bits of which may be used
 * by language-specific Lexer and Parser implementations for custom token
 * flags.
 */
enum : TokenFlags
{
        TF_SPACE_BEFORE = 1U,       /**< token is immediately preceded by
                                         whitespace */
        TF_STARTS_LINE  = 1U << 1,  ///< token is the first on a line
        TF_USER_MIN     = 1U << 8   ///< first language-specific bit
};

//--------------------------------------
/**
 * \brief Token object data type
 * \headerfile Token.h <wrparse/Token.h>
 *
 * Token objects embody indivisible pieces of text (key words, symbols, names
 * etc.) as defined by a target language. They are emitted by the language's
 * `Lexer` which derives them from the raw input text stream, to be fed into
 * the language's `Parser` which interprets the emitted sequence of tokens
 * according to its grammar rules. Each `Token` object carries its type
 * (*i.e.* its 'meaning' in the language), its byte offset within the input
 * text, its length in bytes, its spelling (an ASCII or UTF-8 encoded string)
 * plus bit flags conveying other important information (see the `TokenFlags`
 * type for more details).
 *
 * In order to conserve memory the spelling is not stored with each token
 * since many token types (*e.g.* keywords and symbols) in typical languages
 * share fixed spellings; other variable-spelling tokens (*e.g.* names and
 * numbers) will have their spellings stored by the `Lexer` upon reading
 * them (`Lexer::store()` and `Lexer::clearStorage()` are used to store and
 * free any token-specific data stored separately).
 *
 * Tokens are chained together as a linked list by the `Parser` in the order
 * they were read. It is possible for a language's `Parser` implementation,
 * or parse actions registered with nonterminals, to modify the input token
 * stream but this must be done with great care and should be avoided if
 * possible. The `next()` functions are used to query or change the links
 * between tokens.
 */
class WRPARSE_API Token
{
public:
        using Offset = uint32_t;  ///< Token byte offset

        /**
         * \brief Default constructor
         *
         * Sets all attributes to known values (type \c TOK_NULL, zero
         * offset/length, all flags clear, empty spelling, \c nullptr link to
         * next token)
         */
        Token() : next_(nullptr) { reset(); }

        /**
         * \brief Reset default attributes
         *
         * Resets attributes to their defaults as set by the default
         * constructor, except the link to the next token is left unchanged.
         *
         * \return reference to `*this` object
         */
        Token &reset();

        /**
         * \brief Set token type
         *
         * \note Does not alter the token's spelling even when the type
         *      directly infers it; language-specific \c Lexer objects should
         *      also set spelling using \c setSpelling().
         *
         * \param [in] kind  the target type
         * \return reference to `*this` object
         */
        Token &setKind(TokenKind kind) { kind_ = kind; return *this; }

        /**
         * \brief Set token offset
         * \param [in] offset  the target offset
         * \return reference to `*this` object
         */
        Token &setOffset(Offset offset) { offset_ = offset; return *this; }

        /**
         * \brief Reset all token flags exactly as given
         * \param [in] flags  exact settings for all flags
         * \return reference to `*this` object
         */
        Token &setFlags(TokenFlags flags) { flags_ = flags; return *this; }

        /**
         * \brief Add specified token flags to those already set
         * \param [in] flags  value to be bitwise-OR'ed with existing flags
         * \return reference to `*this` object
         */
        Token &addFlags(TokenFlags flags) { flags_ |= flags; return *this; }

        /**
         * \brief Clear specified token flags
         * \param [in] flags
         *      bits to be cleared in the token should be set here
         * \return reference to `*this` object
         */
        Token &clearFlags(TokenFlags flags)
                { flags_ &= (~flags); return *this; }

        /**
         * \brief Adjust token offset by `delta`
         * \param [in] delta  value to be added to existing offset
         * \note For use by language-specific \c Parser implementations
         *      to manipulate the token stream. Use with caution!
         * \return reference to `*this` object
         */
        Token &adjustOffset(int32_t delta)
                { offset_ += static_cast<Offset>(delta); return *this; }

        /**
         * \brief Set token's spelling
         * \param [in] spelling
         *      the new spelling, encoded as ASCII or UTF-8; its memory
         *      space must outlive the Token object as the caller retains
         *      ownership of it
         * \return reference to `*this` object
         */
        Token &setSpelling(const u8string_view &spelling);

        /**
         * \brief Retrieve token type
         * \return the token's designated type
         */
        TokenKind kind() const { return kind_; }

        /**
         * \brief Check whether the token's type matches the value specified
         * \param [in] kind
         *      token type to check for
         * \return `true` if `kind` matches, `false` otherwise
         */
        bool is(TokenKind kind) const { return kind_ == kind; }

        /// \brief Retrieve offset in bytes from start of raw input text
        Offset offset() const { return offset_; }

        /// \brief Retrieve number of bytes occupied by the token
        size_t bytes() const { return bytes_; }

        /// \brief Retrieve flags
        TokenFlags flags() const { return flags_; }

        /**
         * \brief Get next token of input sequence
         * \note This function does not read further tokens from the input;
         *      use `Parser::nextToken()` to achieve this.
         * \return pointer to next token or `nullptr` if no further tokens
         *      read from the input source
         * \see `Parser::nextToken()`
         */
        const Token *next() const { return next_; }
        Token *next()             { return next_; }

        /**
         * \brief Set next token of input sequence
         * \param [in] new_next  pointer to next token object
         * \note This function is not expected to be used by typical
         *      applications.
         */
        void next(Token *new_next) { next_ = new_next; }

        /**
         * \brief Retrieve token's spelling
         * \return UTF-8 encoded spelling
         */
        u8string_view spelling() const
                { return u8string_view(spelling_, bytes_); }

        ///@{
        /**
         * \brief Compare two `Token` objects
         * \param [in] r  token on right-hand side of comparison operator
         */
        bool operator==(const Token &r) const;
        bool operator!=(const Token &r) const;
        ///@}

        ///@{
        /**
         * \brief Check whether the token's type matches the value specified
         * \param [in] kind  type to be compared with token's type
         */
        bool operator==(TokenKind kind) const { return kind == kind_; }
        bool operator!=(TokenKind kind) const { return kind != kind_; }
        ///@}

        ///@{
        /**
         * \brief Compare token's spelling with the string given
         * \param [in] spelling
         *      string to be compared with token's spelling
         */
        bool operator==(const u8string_view &spelling) const
                { return spelling == this->spelling(); }

        bool operator!=(const u8string_view &spelling) const
                { return spelling != this->spelling(); }
        ///@}

        /**
         * \brief Determine if token is of a valid type
         * \return `false` if the type is `TOK_NULL`, `true` otherwise
         */
        explicit operator bool() const { return kind_ != TOK_NULL; }

private:
        Token      *next_;
        const char *spelling_;
        uint16_t    bytes_;
        TokenFlags  flags_;
        Offset      offset_;
        TokenKind   kind_;
};

static_assert(alignof(Token) >= 4, "Token requires alignment of 4 or more");


} // namespace parse
} // namespace wr


#endif // !WRPARSE_TOKEN_H
