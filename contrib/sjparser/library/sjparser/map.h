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
#include <variant>

#include "internals/dispatcher.h"
#include "internals/token_parser.h"
#include "internals/traits.h"

namespace SJParser {

/** @brief %Map parser.
 *
 * Parses an object where each member value has type @ref Map_T "ParserT",
 * and member name represents map key.
 *
 * @tparam ParserT Element's value parser type.
 * @anchor Map_T
 */

template <typename ParserT, typename EnableCallbackTag = std::true_type> class Map : public TokenParser {
 public:
  static constexpr bool EnableCallback = EnableCallbackTag::value;

  /** Element's value parser type. */
  using ParserType = std::decay_t<ParserT>;

  /** Element callback type. */
  using ElementCallback =
      std::conditional_t<EnableCallback, std::function<bool(const std::string &, ParserType &)>, std::nullptr_t>;

  /** Finish callback type. */
  using Callback = std::conditional_t<EnableCallback, std::function<bool(Map<ParserT> &)>, std::nullptr_t>;

  explicit Map(ParserT &&parser, DisableCallback)
      requires (std::is_base_of_v<TokenParser, ParserType>)
      : _parser{std::forward<ParserT>(parser)} {}

  /** @brief Constructor.
   *
   * @param [in] parser %Parser for map elements values, can be an lvalue
   * reference or an rvalue.
   *
   * @param [in] on_finish (optional) Callback, that will be called after the
   * map is parsed.
   * The callback will be called with a reference to the parser as an argument.
   * If the callback returns false, parsing will be stopped with an error.
   */
  template <typename CallbackT = std::nullptr_t>
  explicit Map(ParserT &&parser, CallbackT on_finish = nullptr)
    requires std::is_constructible_v<Callback, CallbackT>;

  /** @brief Constructor.
   *
   * @param [in] parser %Parser for map elements values, can be an lvalue
   * reference or an rvalue.
   *
   * @param [in] on_element Callback, that will be called after an element of
   * the map is parsed.
   * The callback will be called with a map key and a reference to the parser as
   * arguments.
   * If the callback returns false, parsing will be stopped with an error.
   *
   * @param [in] on_finish (optional) Callback, that will be called after the
   * array is parsed.
   * The callback will be called with a reference to the parser as an argument.
   * If the callback returns false, parsing will be stopped with an error.
   */
  template <typename ElementCallbackT, typename CallbackT = std::nullptr_t>
  Map(ParserT &&parser, ElementCallbackT on_element,
      CallbackT on_finish = nullptr)
      requires (std::is_constructible_v<Callback, CallbackT> &&
                std::is_constructible_v<ElementCallback, ElementCallbackT> &&
                std::is_base_of_v<TokenParser, ParserType>);

  /** Move constructor. */
  Map(Map &&other) noexcept;

  /** Move assignment operator */
  Map<ParserT, EnableCallbackTag> &operator=(Map &&other) noexcept;

  /** @cond INTERNAL Boilerplate. */
  ~Map() override = default;
  Map(const Map &) = delete;
  Map &operator=(const Map &) = delete;
  /** @endcond */

  /** @brief Element callback setter.
   *
   * @param [in] on_element Callback, that will be called after an element of
   * the map is parsed.
   * The callback will be called with a map key and a reference to the parser as
   * arguments.
   * If the callback returns false, parsing will be stopped with an error.
   * */
  void setElementCallback(ElementCallback on_element) requires EnableCallback;

  /** @brief Finish callback setter.
   *
   * @param [in] on_finish Callback, that will be called after the
   * map is parsed.
   * The callback will be called with a reference to the parser as an argument.
   * If the callback returns false, parsing will be stopped with an error.
   */
  void setFinishCallback(Callback on_finish) requires EnableCallback;

  /** @brief Elements value parser getter.
   *
   * @return Reference to the elements parser.
   */
  [[nodiscard]] ParserType &parser();

  /** @cond INTERNAL Internal */
  void setDispatcher(Dispatcher *dispatcher) noexcept override;
  /** @endcond */

 protected:
  std::string &currentKey() noexcept;

 private:
  void on(MapStartT /*unused*/) override;
  void on(MapKeyT key) override;
  void on(MapEndT /*unused*/) override;

  void childParsed() override;
  void finish() override;

  ParserT _parser;
  std::string _current_key;
  std::conditional_t<EnableCallback, ElementCallback, std::monostate> _on_element{};
  std::conditional_t<EnableCallback, Callback, std::monostate> _on_finish{};
};

template <typename ParserT> Map(Map<ParserT> &&) -> Map<Map<ParserT>>;

template <typename ParserT> Map(Map<ParserT> &) -> Map<Map<ParserT> &>;

template <typename ParserT> Map(ParserT &&) -> Map<ParserT>;

template <typename ParserT> Map(ParserT &&, DisableCallback) -> Map<ParserT, DisableCallback>;

/****************************** Implementations *******************************/

template <typename ParserT, typename EnableCallbackTag>
template <typename CallbackT>
Map<ParserT, EnableCallbackTag>::Map(ParserT &&parser, CallbackT on_finish)
  requires std::is_constructible_v<Callback, CallbackT>
    : Map{std::forward<ParserT>(parser), nullptr, std::move(on_finish)} {}

template <typename ParserT, typename EnableCallbackTag>
template <typename ElementCallbackT, typename CallbackT>
Map<ParserT, EnableCallbackTag>::Map(ParserT &&parser, ElementCallbackT on_element,
                  CallbackT on_finish)
    requires (std::is_constructible_v<Callback, CallbackT> &&
              std::is_constructible_v<ElementCallback, ElementCallbackT> &&
              std::is_base_of_v<TokenParser, ParserType>)
    : _parser{std::forward<ParserT>(parser)} {
  if constexpr (EnableCallback) {
    _on_element = std::move(on_element);
    _on_finish = std::move(on_finish);
  }
}

template <typename ParserT, typename EnableCallbackTag>
Map<ParserT, EnableCallbackTag>::Map(Map &&other) noexcept
    : TokenParser{std::move(other)},
      _parser{std::forward<ParserT>(other._parser)} {

  if constexpr (EnableCallback) {
    _on_element = std::move(other._on_element);
    _on_finish = std::move(other._on_finish);
  }
}

template <typename ParserT, typename EnableCallbackTag>
Map<ParserT, EnableCallbackTag> &Map<ParserT, EnableCallbackTag>::operator=(Map &&other) noexcept {
  TokenParser::operator=(std::move(other));
  _parser = std::forward<ParserT>(other._parser);
  if constexpr (EnableCallback) {
    _on_element = std::move(other._on_element);
    _on_finish = std::move(other._on_finish);
  }

  return *this;
}

template <typename ParserT, typename EnableCallbackTag>
void Map<ParserT, EnableCallbackTag>::setElementCallback(ElementCallback on_element) requires EnableCallback {
  _on_element = on_element;
}

template <typename ParserT, typename EnableCallbackTag>
void Map<ParserT, EnableCallbackTag>::setFinishCallback(Callback on_finish) requires EnableCallback {
  _on_finish = on_finish;
}

template <typename ParserT, typename EnableCallbackTag>
typename Map<ParserT, EnableCallbackTag>::ParserType &Map<ParserT, EnableCallbackTag>::parser() {
  return _parser;
}

template <typename ParserT, typename EnableCallbackTag>
void Map<ParserT, EnableCallbackTag>::setDispatcher(Dispatcher *dispatcher) noexcept {
  TokenParser::setDispatcher(dispatcher);
  _parser.setDispatcher(dispatcher);
}

template <typename ParserT, typename EnableCallbackTag>
std::string &Map<ParserT, EnableCallbackTag>::currentKey() noexcept {
  return _current_key;
}

template <typename ParserT, typename EnableCallbackTag>
void Map<ParserT, EnableCallbackTag>::on(MapStartT /*unused*/) {
  reset();
}

template <typename ParserT, typename EnableCallbackTag>
void Map<ParserT, EnableCallbackTag>::on(MapKeyT key) {
  TokenParser::setNotEmpty();
  dispatcher()->pushParser(&_parser);
  _current_key = key.key;
}

template <typename ParserT, typename EnableCallbackTag>
void Map<ParserT, EnableCallbackTag>::on(MapEndT /*unused*/) {
  endParsing();
}

template <typename ParserT, typename EnableCallbackTag>
void Map<ParserT, EnableCallbackTag>::childParsed() {
  if constexpr (EnableCallback) {
    if (_on_element && !_on_element(_current_key, _parser)) {
      throw std::runtime_error("Element callback returned false");
    }
  }
}

template <typename ParserT, typename EnableCallbackTag>
void Map<ParserT, EnableCallbackTag>::finish() {
  if constexpr (EnableCallback) {
    if (_on_finish && !_on_finish(*this)) {
      throw std::runtime_error("Callback returned false");
    }
  }
}

}  // namespace SJParser
