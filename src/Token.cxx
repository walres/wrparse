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


WRPARSE_API
Token::Token()
{
        reset();
}

//--------------------------------------

WRPARSE_API Token &
Token::reset()
{
        next_.tag(1);  // but leave next_ pointer alone
        spelling_.addr_ = "";
        bytes_ = 0;
        offset_ = 0;
        kind_ = TOK_NULL;
        flags_ = 0;
        line_ = 0;
        column_ = 0;
        return *this;
}

//--------------------------------------

WRPARSE_API Token &
Token::setSpelling(
        const u8string_view &spelling
)
{
        if (spelling.has_max_size(1)) {
                if (spelling.empty()) {
                        next_.tag(1);
                        spelling_.addr_ = "";
                } else {
                        next_.tag(0);
                        memcpy(spelling_.buf_,
                               spelling.data(), spelling.bytes());
                }
        } else if (spelling.bytes()
                        > std::numeric_limits<decltype(bytes_)>::max()) {
                throw std::invalid_argument(printStr(
                        "token spelling \"%s\" is too long",
                        utf8_narrow_cvt().from_utf8(spelling)));
        } else {
                next_.tag(1);
                spelling_.addr_ = spelling.char_data();
        }

        bytes_ = static_cast<decltype(bytes_)>(spelling.bytes());
#ifndef NDEBUG
        auto verify = this->spelling();
        if (next_.tag() && !spelling.empty() && (verify.data() != spelling.data())) {
                throw std::logic_error("spelling address corrupted");
        }
#endif
        return *this;
}

//--------------------------------------

WRPARSE_API u8string_view
Token::spelling() const
{
        if (next_.tag() == 0) {
                return u8string_view(spelling_.buf_, bytes_);
        } else {
                return u8string_view(spelling_.addr_, bytes_);
        }
}

//--------------------------------------

WRPARSE_API bool
Token::operator==(
        const Token &r
) const
{
        return (kind_ == r.kind_) && (spelling() == r.spelling());
}

//--------------------------------------

WRPARSE_API bool
Token::operator!=(
        const Token &r
) const
{
        return (kind_ != r.kind_) || (spelling() != r.spelling());
}


} // namespace parse

//--------------------------------------

namespace fmt {


WRPARSE_API void
fmt::TypeHandler<wr::parse::Token>::set(
        Arg                    &arg,
        const wr::parse::Token &val
)
{
        arg.type = Arg::STR_T;
        arg.s = { val.spelling().char_data(), val.spelling().bytes() };
}


} // namespace fmt
} // namespace wr
