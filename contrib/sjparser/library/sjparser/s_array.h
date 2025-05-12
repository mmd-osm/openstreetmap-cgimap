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
#include <variant>
#include <vector>

#include "array.h"

namespace SJParser {

struct EnableCallback : std::true_type {};
struct DisableCallback : std::false_type {};

/** @brief %Array parser, that stores the result in an std::vector of
 * @ref SArray_T "ParserT" values.
 *
 * @tparam ParserT Underlying parser type.
 * @anchor SArray_T
 */

template <typename ParserT, typename EnableCallbackTag = std::true_type> class SArray : public Array<ParserT> {
 public:

  static constexpr bool EnableCallback = EnableCallbackTag::value;

  /** Underlying parser type */
  using ParserType = std::decay_t<ParserT>;

  /** Stored value type */
  using ValueType = std::vector<typename ParserType::ValueType>;

  /** Finish callback type */
  using Callback = std::conditional_t<EnableCallback, std::function<bool(const ValueType &)>, std::nullptr_t>;


  explicit SArray(ParserT &&parser, DisableCallback)
      requires (std::is_base_of_v<TokenParser, ParserType>)
      : Array<ParserT>(std::forward<ParserT>(parser)) {}


  /** @brief Constructor.
   *
   * @param [in] parser %Parser for array elements, can be an lvalue reference
   * or an rvalue. It must be one of the parsers that store values (Value,
   * SArray, SAutoObject, SCustomObject, SMap).
   *
   * @param [in] on_finish (optional) Callback, that will be called after the
   * array is parsed.
   * The callback will be called with a reference to the parser as an argument.
   * If the callback returns false, parsing will be stopped with an error.
   */
  template <typename CallbackT = std::nullptr_t>
  explicit SArray(ParserT &&parser, CallbackT on_finish = nullptr)
    requires(std::is_constructible_v<Callback, CallbackT> &&
             std::is_base_of_v<TokenParser, ParserType>);

  /** Move constructor. */
  SArray(SArray &&other) noexcept;

  /** Move assignment operator */
  SArray<ParserT, EnableCallbackTag> &operator=(SArray &&other) noexcept;

  /** @cond INTERNAL Boilerplate. */
  ~SArray() override = default;
  SArray(const SArray &) = delete;
  SArray &operator=(const SArray &) = delete;
  /** @endcond */

  /** @brief Finish callback setter.
   *
   * @param [in] on_finish Callback, that will be called after the
   * array is parsed.
   *
   * The callback will be called with a reference to the parser as an argument.
   *
   * If the callback returns false, parsing will be stopped with an error.
   */
  void setFinishCallback(Callback on_finish) requires EnableCallback;

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
   * @throw std::runtime_error Thrown if the value is unset (no value was
   * parsed or #pop was called).
   */
  ValueType &&pop();

 private:
  void childParsed() override;
  void finish() override;
  void reset() override;

  ValueType _values;
  std::conditional_t<EnableCallback, Callback, std::monostate> _on_finish{};
};

template <typename ParserT>
SArray(SArray<ParserT> &&) -> SArray<SArray<ParserT>>;

template <typename ParserT>
SArray(SArray<ParserT> &) -> SArray<SArray<ParserT> &>;

template <typename ParserT> SArray(ParserT &&) -> SArray<ParserT>;

template <typename ParserT>
SArray(ParserT&&, DisableCallback) -> SArray<ParserT, DisableCallback>;

/****************************** Implementations *******************************/

template <typename ParserT, typename EnableCallbackTag>
template <typename CallbackT>
SArray<ParserT, EnableCallbackTag>::SArray(ParserT &&parser, CallbackT on_finish)
    requires(std::is_constructible_v<Callback, CallbackT> &&
             std::is_base_of_v<TokenParser, ParserType>)
    : Array<ParserT>{std::forward<ParserT>(parser)} {
   if constexpr (EnableCallbackTag::value) {
      _on_finish = std::move(on_finish);
   }
}

template <typename ParserT, typename EnableCallbackTag>
SArray<ParserT, EnableCallbackTag>::SArray(SArray &&other) noexcept
    : Array<ParserT>{std::move(other)},
      _values{std::move(other._values)} {
   if constexpr (EnableCallbackTag::value) {
      _on_finish = std::move(other._on_finish);
   }
}

template <typename ParserT, typename EnableCallbackTag>
SArray<ParserT, EnableCallbackTag> &SArray<ParserT, EnableCallbackTag>::operator=(SArray &&other) noexcept {
  Array<ParserT>::operator=(std::move(other));
  _values = std::move(other._values);
   if constexpr (EnableCallbackTag::value) {
    _on_finish = std::move(other._on_finish);
   }

  return *this;
}

template <typename ParserT, typename EnableCallbackTag>
void SArray<ParserT, EnableCallbackTag>::setFinishCallback(Callback on_finish) requires EnableCallback {
  _on_finish = on_finish;
}

template <typename ParserT, typename EnableCallbackTag>
const typename SArray<ParserT, EnableCallbackTag>::ValueType &SArray<ParserT, EnableCallbackTag>::get() const {
  TokenParser::checkSet();
  return _values;
}

template <typename ParserT, typename EnableCallbackTag>
typename SArray<ParserT, EnableCallbackTag>::ValueType &&SArray<ParserT, EnableCallbackTag>::pop() {
  TokenParser::checkSet();
  TokenParser::unset();
  return std::move(_values);
}

template <typename ParserT, typename EnableCallbackTag>
void SArray<ParserT, EnableCallbackTag>::childParsed() {
  _values.push_back(Array<ParserT>::parser().pop());
}

template <typename ParserT, typename EnableCallbackTag>
void SArray<ParserT, EnableCallbackTag>::finish() {
  if constexpr (EnableCallbackTag::value) {
    if (_on_finish && !_on_finish(_values)) {
      throw std::runtime_error("Callback returned false");
    }
  }
}

template <typename ParserT, typename EnableCallbackTag>
void SArray<ParserT, EnableCallbackTag>::reset() {
  ArrayParser::reset();
  _values = {};
}

}  // namespace SJParser
