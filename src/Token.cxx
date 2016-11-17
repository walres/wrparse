/**
 * \file Token.cxx
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
#include <string.h>
#include <limits>
#include <stdexcept>
#include <string>

#include <wrutil/codecvt.h>
#include <wrutil/Format.h>
#include <wrparse/Parser.h>
#include <wrparse/Token.h>


namespace wr {
namespace parse {


WRPARSE_API Token &
Token::reset()
{
        spelling_ = "";
        bytes_ = 0;
        offset_ = 0;
        kind_ = TOK_NULL;
        flags_ = 0;
        // leave next_ alone
        return *this;
}

//--------------------------------------

WRPARSE_API Token &
Token::setSpelling(
        const u8string_view &spelling
)
{
        if (spelling.bytes() > std::numeric_limits<decltype(bytes_)>::max()) {
                throw std::invalid_argument(wr::printStr(
                        "Token::setSpelling(): spelling \"%s\" is too long",
                        wr::utf8_narrow_cvt().from_utf8(spelling)));
        }
        spelling_ = spelling.char_data();
        bytes_ = static_cast<decltype(bytes_)>(spelling.bytes());
        return *this;
}

//--------------------------------------

WRPARSE_API bool
Token::operator==(
        const Token &r
) const
{
        return (kind_ == r.kind_) && (bytes_ == r.bytes_)
                                  && !strncmp(spelling_, r.spelling_, bytes_);
}

//--------------------------------------

WRPARSE_API bool
Token::operator!=(
        const Token &r
) const
{
        return (kind_ != r.kind_) || (bytes_ != r.bytes_)
                                  || strncmp(spelling_, r.spelling_, bytes_);
}


} // namespace parse
} // namespace wr
