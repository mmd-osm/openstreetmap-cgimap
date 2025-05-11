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

#include "nvalue.h"

namespace SJParser {


template <typename ValueT>
NValue<ValueT>::NValue(NValue &&other) noexcept
    : TokenParser{std::move(other)},
      _value{std::move(other._value)} {}

template <typename ValueT>
NValue<ValueT> &NValue<ValueT>::operator=(NValue &&other) noexcept {
  TokenParser::operator=(std::move(other));
  _value = std::move(other._value);

  return *this;
}

template <typename ValueT> void NValue<ValueT>::on(TokenType<ValueT> value) {
  setNotEmpty();
  _value = value;
  endParsing();
}

template <typename ValueT>
void NValue<ValueT>::on(TokenSecondaryType<ValueT> value) {
  if constexpr (!std::is_same_v<TokenSecondaryType<ValueT>, SJParser::DummyT>) {
    setNotEmpty();
    _value = static_cast<decltype(_value)>(value);
    endParsing();
  }
}

template <typename ValueT> const ValueT &NValue<ValueT>::get() const {
  checkSet();
  return _value;
}

template <typename ValueT> ValueT &&NValue<ValueT>::pop() {
  checkSet();
  unset();
  return std::move(_value);
}

template <typename ValueT> void NValue<ValueT>::finish() {
 // NValue has no callback
}

template class NValue<int64_t>;
template class NValue<bool>;
template class NValue<double>;
template class NValue<std::string>;

}  // namespace SJParser
