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

#include <functional>
#include <optional>

#include "internals/token_parser.h"

namespace SJParser {

/** @brief Plain value parser.
 *
 * @tparam ValueT JSON value type, can be std::string, int64_t, bool or double
 */

template <typename ValueT> class OptionalNValue : public TokenParser {
 public:
  /** Underlying type, that can be obtained from this parser with #get or #pop.
   */
  using ValueType = std::optional<ValueT>;

  /** @brief Constructor.
   */
  explicit OptionalNValue();

  /** Move constructor. */
  OptionalNValue(OptionalNValue &&other) noexcept;

  /** Move assignment operator */
  OptionalNValue<ValueT> &operator=(OptionalNValue &&other) noexcept;

  /** @cond INTERNAL Boilerplate. */
  ~OptionalNValue() override = default;
  OptionalNValue(const OptionalNValue &) = delete;
  OptionalNValue &operator=(const OptionalNValue &) = delete;
  /** @endcond */

  /** @brief Parsed value getter.
   *
   * @return Const reference to a parsed value.
   *
   * @throw std::runtime_error Thrown if the value is unset (no value was
   * parsed or #pop was called).
   */
  [[nodiscard]] const ValueType &get() const;

  /** @brief Get the parsed value and unset the parser.
   *
   * Moves the parsed value out of the parser.
   *
   * @return Rvalue reference to the parsed value.
   *
   * @throw std::runtime_error thrown if the value is unset (no value was
   * parsed or #pop was called).
   */
  ValueType &&pop();

 private:
  void on(TokenType<ValueT> value) override;
  void on(TokenSecondaryType<ValueT> value) override;
  void finish() override;

  ValueType _value;
};

extern template class OptionalNValue<int64_t>;
extern template class OptionalNValue<bool>;
extern template class OptionalNValue<double>;
extern template class OptionalNValue<std::string>;

}  // namespace SJParser
