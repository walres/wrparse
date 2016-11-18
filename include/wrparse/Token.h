/**
 * \file Token.h
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


typedef uint16_t TokenKind;   ///< A token's type (its 'meaning')
typedef uint16_t TokenFlags;  /**< Bit flags set on individual tokens to mark
                                   special aspects (e.g. beginning of line) */

class Parser;


/**
 * \brief Basic token types
 *
 * Language implementations are expected to define their own TOK_* enumeration
 * constants above TOK_USER_MIN (values below this are reserved for the
 * wrparse library). These constants are used by the lexer and parser to
 * represent terminals. Only generic token types are defined here, which should
 * be handled by all languages.
 *
 * \note Language-specific token enumeration types \e must specify \c TokenKind
 *      as their base type otherwise compilation will fail when using the
 *      constants in grammar rule definitions.
 */
enum : TokenKind
{
        TOK_NULL = 0,          /**< expresses 'empty token' or 'any token'
                                    depending on context; otherwise not
                                    considered a valid type */
        TOK_EOF,               ///< represents end of input file
        TOK_USER_MIN = 0x0400  ///< first language-specific ID
};

//--------------------------------------
/**
 * \brief Basic token bit flags
 *
 * TokenFlags is a 16-bit field, the top eight bits of which may be used by
 * language-specific Lexer and Parser implementations for custom token flags.
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
 * \brief Token data type
 *
 * Token objects embody indivisible pieces of text (keywords, symbols, names
 * etc.) as read by a target language. They are emitted by the language's
 * \c Lexer which derives them from the raw input text stream, to be fed into
 * the language's \c Parser which interprets the emitted sequence of tokens
 * according to its grammar rules. Each Token object carries its type (i.e. its
 * 'meaning' in the language), its byte offset within the input text, its
 * length in bytes, its spelling (a UTF-8 encoded string) plus bit flags
 * conveying other important information (see the TokenFlags enumeration type
 * for more details).
 *
 * In order to conserve memory the spelling is not stored as part of a token,
 * since many token types (e.g. keywords and symbols) in typical languages
 * share fixed spellings; other multi-spelling tokens (e.g. names and numbers)
 * will have their spellings stored by the lexer upon reading them
 * (Lexer::store() and Lexer::clearStorage() can be used to store and free any
 * token-specific data not stored as part of the token).
 *
 * Tokens are chained together as a linked list by the \c Parser in the order
 * they were read. It is possible for a language's \c Parser implementation,
 * or registered pre-/post-parse actions to modify the original token stream
 * but this must be done with great care and should be avoided if possible.
 * The \c next() functions are used to query or change the links between
 * tokens although applications are not expected to need to do the latter in
 * most cases.
 */
class WRPARSE_API Token
{
public:
        using Offset = uint32_t;  ///< token byte offset

        /**
         * \brief Default constructor
         *
         * Sets all attributes to known values (type TOK_NULL, zero
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
         * \return reference to \c *this
         */
        Token &reset();

        /**
         * \brief Set token type
         *
         * \note Does not alter the token's spelling even when the type
         *      directly infers it; language-specific \c Lexer objects should
         *      also set spelling using \c setSpelling().
         *
         * \param [in] kind
         *      the target type
         * \return reference to \c *this
         */
        Token &setKind(TokenKind kind) { kind_ = kind; return *this; }

        /**
         * \brief Set token offset
         * \param [in] offset
         *      the target offset
         * \return reference to \c *this
         */
        Token &setOffset(Offset offset) { offset_ = offset; return *this; }

        /**
         * \brief Reset all token flags exactly as given
         * \param [in] flags
         *      flags to be set and cleared
         * \return reference to \c *this
         */
        Token &setFlags(TokenFlags flags) { flags_ = flags; return *this; }

        /**
         * \brief Add specified token flags to those already set
         * \param [in] flags
         *      flags to be bitwise-OR'ed with existing flags
         * \return reference to \c *this
         */
        Token &addFlags(TokenFlags flags) { flags_ |= flags; return *this; }

        /**
         * \brief Clear specified token flags
         * \param [in] flags
         *      any bit positions set here cause the same bits to be
         *      cleared for the token's flags
         * \return reference to \c *this
         */
        Token &clearFlags(TokenFlags flags)
                { flags_ &= (~flags); return *this; }

        /**
         * \brief Move token offset
         * \param [in] delta
         *      value to be added to existing offset
         * \note For use by language-specific \c Parser implementations
         *      to manipulate the token stream. Use with caution!
         * \return reference to \c *this
         */
        Token &adjustOffset(int32_t delta)
                { offset_ += static_cast<Offset>(delta); return *this; }

        /**
         * \brief Set token's spelling
         * \param [in] spelling
         *      the new spelling, encoded as ASCII or UTF-8; must outlive
         *      the Token object as the memory for the spelling is neither
         *      copied nor managed by it
         * \return reference to \c *this
         */
        Token &setSpelling(const u8string_view &spelling);

        /**
         * \brief Retrieve token type
         * \return the token's designated type
         */
        TokenKind kind() const { return kind_; }

        /**
         * \brief Check if type matches that specified
         * \param [in] kind
         *      token type to check for
         * \return \c true if \c kind matches, \c false otherwise
         */
        bool is(TokenKind kind) const { return kind_ == kind; }

        /**
         * \brief Retrieve offset within raw input text
         * \return number of bytes from start of original input text
         */
        Offset offset() const { return offset_; }

        /**
         * \brief Retrieve number of bytes occupied by the token
         * \return number of bytes occupied within raw input text
         */
        size_t bytes() const { return bytes_; }

        /// \brief Retrieve flags
        TokenFlags flags() const { return flags_; }

        /**
         * \brief Get next token of input sequence
         * \note This function does not read further tokens from the input;
         *      use Parser::nextToken() to achieve this.
         * \return pointer to next token or \c nullptr if no more tokens
         * \see Parser::nextToken()
         */
        const Token *next() const { return next_; }
        Token *next()             { return next_; }

        /**
         * \brief Set next token of input sequence
         * \param [in] new_next
         *      pointer to next token object
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
         * \brief Compare two \c Token objects
         * \param [in] r
         *      token on right-hand side of comparison operator
         */
        bool operator==(const Token &r) const;
        bool operator!=(const Token &r) const;
        ///@}

        ///@{
        /**
         * \brief Compare stored type with a given type
         * \param [in] kind
         *      type to be compared with token's type
         */
        bool operator==(TokenKind kind) const { return kind == kind_; }
        bool operator!=(TokenKind kind) const { return kind != kind_; }
        ///@}

        ///@{
        /**
         * \brief Compare referenced spelling with a given spelling
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
         * \return \c true if the type is any other than \c TOK_NULL,
         *      \c false otherwise
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
