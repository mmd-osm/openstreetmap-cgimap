/**
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of openstreetmap-cgimap (https://github.com/zerebubuth/openstreetmap-cgimap/).
 *
 * Copyright (C) 2009-2023 by the CGImap developer community.
 * For a full list of authors see the git log.
 */

#ifndef OSMCHANGE_JSON_INPUT_FORMAT_HPP
#define OSMCHANGE_JSON_INPUT_FORMAT_HPP

#include "cgimap/api06/changeset_upload/node.hpp"
#include "cgimap/api06/changeset_upload/osmobject.hpp"
#include "cgimap/api06/changeset_upload/parser_callback.hpp"
#include "cgimap/api06/changeset_upload/relation.hpp"
#include "cgimap/api06/changeset_upload/way.hpp"
#include "cgimap/types.hpp"

#include "sjparser/sjparser.h"

#include <fmt/core.h>

#include <cassert>
#include <string>
#include <type_traits>



namespace api06 {

using SJParser::Array;
using SJParser::Member;
using SJParser::Object;
using SJParser::Parser;
using SJParser::Presence;
using SJParser::SArray;
using SJParser::SAutoObject;
using SJParser::SMap;
using SJParser::Value;
using SJParser::OptionalValue;
using SJParser::Reaction;
using SJParser::ObjectOptions;
using SJParser::Presence::Optional;
using SJParser::Ignore;


// FixedValue allows to set a fixed value for a member
// and ignore the value in the payload
template <typename T>
class FixedValue : public Ignore {
public:
  using ValueType = std::conditional_t<std::is_enum_v<T>, std::underlying_type_t<T>, T>;

  FixedValue() = default;

  explicit FixedValue(T value) : _value{ static_cast<ValueType>(value) } {
    setNotEmpty();
  }

  [[nodiscard]] const ValueType &get() const {
    return _value;
  }

  [[nodiscard]] ValueType &&pop() {
    unset();
    return std::move(_value);
  }

  private:
    ValueType _value{};
};

class OSMChangeJSONParserFormat {

  [[nodiscard]] static bool validateType(const std::string &type) {
    if (type != "node" && type != "way" && type != "relation") {
      throw payload_error{fmt::format("Unknown element {}, expecting node, way or relation", type)};
    }
    return true;
  }

  [[nodiscard]] static bool validateAction(const std::string &action) {
    if (action != "create" && action != "modify" && action != "delete") {
      throw payload_error{fmt::format("Unknown action {}, expecting create, modify or delete", action)};
    }
    return true;
  }

  [[nodiscard]] static bool check_version_callback(const std::string& version) {

    if (version != "0.6") {
      throw payload_error{fmt::format(R"(Unsupported version "{}", expecting "0.6")", version)};
    }
    return true;
  }

  static auto getMemberParser() {

    return SAutoObject{std::tuple{Member{"type", Value<std::string>{validateType}},
                                  Member{"ref", Value<int64_t>{}},
                                  Member{"role", Value<std::string>{}, Optional, ""}}
                                  };
  }

  static auto getElementsParser() {
    return SAutoObject{
          std::tuple{
            Member{"type", Value<std::string>{validateType}},
            Member{"id", Value<int64_t>{}},
            Member{"lat", OptionalValue<double>{}, Optional, std::optional<double>{}},
            Member{"lon", OptionalValue<double>{}, Optional, std::optional<double>{}},
            Member{"version", OptionalValue<int64_t>{}, Optional, std::optional<int64_t>{}},
            Member{"changeset", Value<int64_t>{}},
            Member{"tags", SMap{Value<std::string>{}}, Optional, std::map<std::string, std::string>{}},
            Member{"nodes", SArray{Value<int64_t>{}}, Optional, std::vector<int64_t>{}},
            Member{"members", SArray{getMemberParser()}, Optional, std::vector<std::tuple<std::string, int64_t, std::string>>{}},
          }
      };
  }

  template <typename ActionElementsParserCallback = std::nullptr_t>
  static auto getActionElementsParser(ActionElementsParserCallback action_elements_parser_callback = nullptr) {
    using enum operation;
    return Object{
          std::tuple{
            Member{"action", Value<std::string>{validateAction}},
            Member{"elements", SArray{getElementsParser()}},
            Member{"if-unused", Value<bool>{}, Optional},
          },
        ObjectOptions{Reaction::Ignore},
        action_elements_parser_callback
        };
  }

  template <typename ActionElementsParserCallback = std::nullptr_t>
  static auto getMainParser(ActionElementsParserCallback action_elements_parser_callback = nullptr) {
    return Parser{
       Object{
        std::tuple{
          Member{"version", Value<std::string>{check_version_callback}},
          Member{"generator", Value<std::string>{}, Optional},
          Member{"osmChange", Array{getActionElementsParser(action_elements_parser_callback)}}
        },
        ObjectOptions{Reaction::Ignore}
      }};
  }

  friend class OSMChangeJSONParser;
};

class OSMChangeJSONParser {

public:
  explicit OSMChangeJSONParser(Parser_Callback& callback)
      : m_callback(callback) { }

  OSMChangeJSONParser(const OSMChangeJSONParser &) = delete;
  OSMChangeJSONParser &operator=(const OSMChangeJSONParser &) = delete;

  OSMChangeJSONParser(OSMChangeJSONParser &&) = delete;
  OSMChangeJSONParser &operator=(OSMChangeJSONParser &&) = delete;

  void process_message(const std::string &data) {

    try {
      m_callback.start_document();
      _parser.parse(data);
      _parser.finish();

      if (_parser.parser().isEmpty()) {
        throw payload_error("Empty JSON payload");
      }

      if (element_count == 0) {
        throw payload_error("osmChange array is empty");
      }

      m_callback.end_document();
    } catch (const std::exception& e) {
      throw http::bad_request(e.what());    // rethrow JSON parser error as HTTP 400 Bad request
    }
  }

private:

  using ActionElementsParser = decltype(api06::OSMChangeJSONParserFormat::getActionElementsParser());
  using MainParser = decltype(api06::OSMChangeJSONParserFormat::getMainParser());

  MainParser _parser{api06::OSMChangeJSONParserFormat::getMainParser(
                     std::bind_front(&api06::OSMChangeJSONParser::process_action_elements, this))};

  // OSM element callback
  bool process_action_elements(ActionElementsParser &parser) {

    // process action
    process_action(parser);

    // process if-unused flag for delete action
    process_if_unused(parser);

    // process type (node, way, relation)
    process_elements(parser);

    return true;
  }

  void process_action(ActionElementsParser &parser) {

    using enum operation;
    auto & action = parser.get<0>();
    if (action == "create") {
      m_operation = op_create;
    } else if (action == "modify") {
      m_operation = op_modify;
    } else if (action == "delete") {
      m_operation = op_delete;
    }
  }

  void process_if_unused(ActionElementsParser &parser) {
    m_if_unused = false;

    if (m_operation == operation::op_delete) {
      if (parser.parser<2>().isSet()) {
        m_if_unused = parser.get<2>();
      }
    }
    else {
      if (parser.parser<2>().isSet()) {
        throw payload_error{fmt::format("if-unused attribute is only allowed for delete action")};
      }
    }
  }

  void process_elements(ActionElementsParser &parser) {

    for (const auto& element : parser.get<1>()) {
      element_count++;
      process_type(element);
    }
  }

  void process_type(auto& element) {

    auto& type = std::get<0>(element);

    if (type == "node") {
      process_node(element);
    } else if (type == "way") {
      process_way(element);
    } else if (type == "relation") {
      process_relation(element);
    }
  }

  void process_node(auto& element) {

    if (!std::get<7>(element).empty()) {
      throw payload_error{fmt::format("Element {}/{:d} has way nodes, but it is not a way", std::get<0>(element), std::get<1>(element))};
    }

    if (!std::get<8>(element).empty()) {
      throw payload_error{fmt::format("Element {}/{:d} has relation members, but it is not a relation", std::get<0>(element), std::get<1>(element))};
    }

    Node node;
    init_object(node, element);

    // read optional value
    if (std::get<2>(element).has_value()) {
      node.set_lat(std::get<2>(element).value());
    }

    if (std::get<3>(element).has_value()) {
      node.set_lon(std::get<3>(element).value());
    }

    process_tags(node, element);

    if (!node.is_valid(m_operation)) {
      throw payload_error{fmt::format("{} does not include all mandatory fields", node.to_string())};
    }

    m_callback.process_node(node, m_operation, m_if_unused);
  }

  void process_way(auto& element) {

    if (std::get<2>(element).has_value()) {
      throw payload_error{fmt::format("Element {}/{:d} has lat, but it is not a node", std::get<0>(element), std::get<1>(element))};
    }

    if (std::get<3>(element).has_value()) {
      throw payload_error{fmt::format("Element {}/{:d} has lon, but it is not a node", std::get<0>(element), std::get<1>(element))};
    }

    if (!std::get<8>(element).empty()) {
      throw payload_error{fmt::format("Element {}/{:d} has relation members, but it is not a relation", std::get<0>(element), std::get<1>(element))};
    }

    Way way;
    init_object(way, element);

    // adding way nodes
    for (const auto& way_node_id : std::get<7>(element)) {
        way.add_way_node(way_node_id);
    }

    process_tags(way, element);

    if (!way.is_valid(m_operation)) {
      throw payload_error{fmt::format("{} does not include all mandatory fields", way.to_string())};
    }

    m_callback.process_way(way, m_operation, m_if_unused);
  }

  void process_relation(auto& element) {

    if (std::get<2>(element).has_value()) {
      throw payload_error{fmt::format("Element {}/{:d} has lat, but it is not a node", std::get<0>(element), std::get<1>(element))};
    }

    if (std::get<3>(element).has_value()) {
      throw payload_error{fmt::format("Element {}/{:d} has lon, but it is not a node", std::get<0>(element), std::get<1>(element))};
    }

    if (!std::get<7>(element).empty()) {
      throw payload_error{fmt::format("Element {}/{:d} has way nodes, but it is not a way", std::get<0>(element), std::get<1>(element))};
    }

    Relation relation;
    init_object(relation, element);

    process_relation_members(relation, element);

    process_tags(relation, element);

    if (!relation.is_valid(m_operation)) {
      throw payload_error{fmt::format("{} does not include all mandatory fields", relation.to_string())};
    }

    m_callback.process_relation(relation, m_operation, m_if_unused);
  }

  void process_relation_members(Relation &relation, auto& element) const {

    // relation member attribute is mandatory for create and modify operations.
    // Its value can be an empty array, though. Delete operations don't require
    // this attribute.
    if (m_operation == operation::op_delete)
      return;

    for (const auto& [type, ref, role] : std::get<8>(element)) {
      RelationMember member;
      member.set_type(type);
      member.set_ref(ref);
      member.set_role(role);

      if (!member.is_valid()) {
        throw payload_error{fmt::format("Missing mandatory field on relation member in {}", relation.to_string()) };
      }
      relation.add_member(member);
    }
  }

  void process_tags(OSMObject &o, auto& element) const {

    for (const auto &[key, value] : std::get<6>(element)) {
        o.add_tag(key, value);
    }
  }

  void init_object(OSMObject &object, auto& element) const {

    // id
    object.set_id(std::get<1>(element));

    // version
    if (std::get<4>(element).has_value()) {
      object.set_version(std::get<4>(element).value());
    }

    // changeset
    object.set_changeset(std::get<5>(element));

    if (m_operation == operation::op_create) {
      // we always override version number for create operations (they are not
      // mandatory)
      object.set_version(0u);
    } else if (m_operation == operation::op_delete ||
               m_operation == operation::op_modify) {
      // objects for other operations must have a positive version number
      if (!object.has_version()) {
        throw payload_error{fmt::format("Version is required when updating {}", object.to_string()) };
      }
      if (object.version() < 1) {
        throw payload_error{ fmt::format("Invalid version number {} in {}", object.version(), object.to_string()) };
      }
    }
  }

  operation m_operation = operation::op_undefined;
  Parser_Callback& m_callback;
  bool m_if_unused = false;
  int element_count = 0;
};

} // namespace api06

#endif // OSMCHANGE_JSON_INPUT_FORMAT_HPP
