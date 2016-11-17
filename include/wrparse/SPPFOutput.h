/**
 * \file SPPFOutput.h
 *
 * \brief C++ iostream template function for printing SPPF forests
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
#ifndef WRPARSE_SPPF_OUTPUT_H
#define WRPARSE_SPPF_OUTPUT_H

#include <iomanip>
#include <iostream>
#include <string>

#include <wrparse/SPPF.h>


namespace wr {
namespace parse {


template <typename CharT, typename Traits> std::basic_ostream<CharT, Traits> &
operator<<(
        std::basic_ostream<CharT, Traits> &dst,
        const SPPFNode                    &src
)
{
        static thread_local int indent = 0;

        dst << std::setw(indent) << "";

        if (src.isNonTerminal()) {
                dst << src.nonTerminal()->name();
                if (src.rule()) {
                        dst << '.' << src.rule()->index();
                }
        }

        auto walker = nonTerminals(src);

        for (; walker && walker->is(src); walker.reset(walker.node())) {
                dst << "->" << walker->nonTerminal()->name();
                if (walker->rule()) {
                        dst << '.' << walker->rule()->index();
                }
        }

        {
                std::string content = src.content(25);
                size_t      pos     = 0;

                do {
                        pos = content.find('\n', pos);
                        if (pos == std::string::npos) {
                                break;
                        } else {
                                content[pos] = ' ';
                        }
                } while (true);

                if (content.empty()) {
                        dst.put('\n');
                } else {
                        dst << " (" << content << ")\n";
                }
        }

        indent += 4;
        for (const SPPFNode &child: walker) {
                dst << child;
        }
        indent -= 4;
        return dst;
}


} // namespace parse
} // namespace wr


#endif // !WRPARSE_AST_OUTPUT_H
