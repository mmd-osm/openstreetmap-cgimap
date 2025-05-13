/*******************************************************************************

Copyright (c) 2016-2017 Denis Tikhomirov <dvtikhomirov@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*******************************************************************************/

#pragma once

#include <type_traits>

#include "token_parser.h"

namespace SJParser {

template <typename T>
concept HasValueType = requires { typename std::decay_t<T>::ValueType; };

template <typename ParserT>
constexpr bool IsStorageParser = HasValueType<ParserT>;

template <typename NameT>
concept ValidNameType = std::is_same_v<NameT, int64_t> ||
            std::is_same_v<NameT, bool> ||
            std::is_same_v<NameT, double> ||
            std::is_same_v<NameT, std::string_view>;

template <typename ParserT>
concept ValidParserType = std::is_base_of_v<TokenParser, std::decay_t<ParserT>>;

struct EnableCallback : std::true_type {};
struct DisableCallback : std::false_type {};

}  // namespace SJParser
