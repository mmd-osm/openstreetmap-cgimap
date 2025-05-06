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

  [[nodiscard]] static bool check_version_callback(const std::string& version) {

    if (version != "0.6") {
      throw payload_error{fmt::format(R"(Unsupported version "{}", expecting "0.6")", version)};
    }
    return true;
  }

  static auto getMemberParser() {

    return SAutoObject{std::tuple{Member{"type", Value<std::string>{validateType}},
                                  Member{"ref", Value<int64_t>{}},
                                  Member{"role", Value<std::string>{}, Optional, ""}},
                                  ObjectOptions{Reaction::Ignore}
                                  };
  }

  template <typename ElementParserCallback = std::nullptr_t>
  static auto getElementsParser(operation op, ElementParserCallback element_parser_callback = nullptr) {
    return Object{
          std::tuple{
            Member{"type", Value<std::string>{validateType}},
            Member{"id", Value<int64_t>{}},
            Member{"lat", OptionalValue<double>{}, Optional},
            Member{"lon", Value<double>{}, Optional},
            Member{"version", Value<int64_t>{}, Optional},
            Member{"changeset", Value<int64_t>{}},
            Member{"tags", SMap{Value<std::string>{}}, Optional},
            Member{"nodes", SArray{Value<int64_t>{}}, Optional},
            Member{"members", SArray{getMemberParser()}, Optional},
             // internal property to set the action (create/modify/delete), payload value is ignored
            Member{"_action_", FixedValue{op}, Optional},
            Member{"if-unused", Value<bool>{}, Optional, false}
          },
        ObjectOptions{Reaction::Ignore},
        element_parser_callback};
  }

  template <typename ElementParserCallback = std::nullptr_t>
  static auto getOsmChangeParser(ElementParserCallback element_parser_callback = nullptr) {
    using enum operation;
    return Object{
          std::tuple{
            Member{"create", Array{getElementsParser(op_create, element_parser_callback)}, Optional},
            Member{"modify", Array{getElementsParser(op_modify, element_parser_callback)}, Optional},
            Member{"delete", Array{getElementsParser(op_delete, element_parser_callback)}, Optional}
          },
        ObjectOptions{Reaction::Ignore}
        };
  }

  template <typename ElementParserCallback = std::nullptr_t>
  static auto getMainParser(ElementParserCallback element_parser_callback = nullptr) {
    return Parser{
       Object{
        std::tuple{
          Member{"version", Value<std::string>{check_version_callback}},
          Member{"generator", Value<std::string>{}, Optional},
          Member{"osmChange", getOsmChangeParser(element_parser_callback)}
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

  using ElementsParser = decltype(api06::OSMChangeJSONParserFormat::getElementsParser(operation::op_undefined));
  using MainParser = decltype(api06::OSMChangeJSONParserFormat::getMainParser());

  MainParser _parser{api06::OSMChangeJSONParserFormat::getMainParser(
                     std::bind_front(&api06::OSMChangeJSONParser::process_element, this))};

  // OSM element callback
  bool process_element(ElementsParser &parser) {

    element_count++;

    // process action
    process_action(parser);

    // process if-unused flag for delete action
    process_if_unused(parser);

    // process type (node, way, relation)
    process_type(parser);

    return true;
  }

  void process_action(ElementsParser &parser) {

    auto op = parser.get<9>();
    if (op < 1|| op > 3) {
      throw payload_error{fmt::format("Unknown action value", op)};
    }
    m_operation = static_cast<operation>(op);
  }

  void process_if_unused(ElementsParser &parser) {

    if (m_operation == operation::op_delete) {
      m_if_unused = false;
      if (parser.parser<10>().isSet()) {
        m_if_unused = parser.get<10>();
      }
    }
    else {
      m_if_unused = false;
      if (parser.parser<10>().isSet()) {
        throw payload_error{fmt::format("if-unused attribute is only allowed for delete action")};
      }
    }
  }

  void process_type(ElementsParser &parser) {

    const std::string& type = parser.get<0>();

    if (type == "node") {
      process_node(parser);
    } else if (type == "way") {
      process_way(parser);
    } else if (type == "relation") {
      process_relation(parser);
    } else {
      throw payload_error{fmt::format("Unknown element {}, expecting node, way or relation", type)};
    }
  }

  void process_node(ElementsParser& parser) {

    if (parser.parser<7>().isSet()) {
      throw payload_error{fmt::format("Element {}/{:d} has way nodes, but it is not a way", parser.get<0>(), parser.get<1>())};
    }

    if (parser.parser<8>().isSet()) {
      throw payload_error{fmt::format("Element {}/{:d} has relation members, but it is not a relation", parser.get<0>(), parser.get<1>())};
    }

    Node node;
    init_object(node, parser);

    // read optional value
    if (parser.get<2>().has_value()) {
      node.set_lat(parser.get<2>().value());
    }

    if (parser.parser<3>().isSet()) {
      node.set_lon(parser.get<3>());
    }

    process_tags(node, parser);

    if (!node.is_valid(m_operation)) {
      throw payload_error{fmt::format("{} does not include all mandatory fields", node.to_string())};
    }

    m_callback.process_node(node, m_operation, m_if_unused);
  }

  void process_way(ElementsParser& parser) {

    if (parser.parser<2>().isSet()) {
      throw payload_error{fmt::format("Element {}/{:d} has lat, but it is not a node", parser.get<0>(), parser.get<1>())};
    }

    if (parser.parser<3>().isSet()) {
      throw payload_error{fmt::format("Element {}/{:d} has lon, but it is not a node", parser.get<0>(), parser.get<1>())};
    }

    if (parser.parser<8>().isSet()) {
      throw payload_error{fmt::format("Element {}/{:d} has relation members, but it is not a relation", parser.get<0>(), parser.get<1>())};
    }

    Way way;
    init_object(way, parser);

    // adding way nodes
    if (parser.parser<7>().isSet()) {
      for (const auto& way_node_id : parser.get<7>()) {
          way.add_way_node(way_node_id);
      }
    }

    process_tags(way, parser);

    if (!way.is_valid(m_operation)) {
      throw payload_error{fmt::format("{} does not include all mandatory fields", way.to_string())};
    }

    m_callback.process_way(way, m_operation, m_if_unused);
  }

  void process_relation(ElementsParser& parser) {

    if (parser.parser<2>().isSet()) {
      throw payload_error{fmt::format("Element {}/{:d} has lat, but it is not a node", parser.get<0>(), parser.get<1>())};
    }

    if (parser.parser<3>().isSet()) {
      throw payload_error{fmt::format("Element {}/{:d} has lon, but it is not a node", parser.get<0>(), parser.get<1>())};
    }

    if (parser.parser<7>().isSet()) {
      throw payload_error{fmt::format("Element {}/{:d} has way nodes, but it is not a way", parser.get<0>(), parser.get<1>())};
    }

    Relation relation;
    init_object(relation, parser);

    process_relation_members(relation, parser);

    process_tags(relation, parser);

    if (!relation.is_valid(m_operation)) {
      throw payload_error{fmt::format("{} does not include all mandatory fields", relation.to_string())};
    }

    m_callback.process_relation(relation, m_operation, m_if_unused);
  }

  void process_relation_members(Relation &relation, ElementsParser& parser) const {

    // relation member attribute is mandatory for create and modify operations.
    // Its value can be an empty array, though. Delete operations don't require
    // this attribute.
    if (m_operation == operation::op_delete)
      return;

    if (!parser.parser<8>().isSet()) {
      throw payload_error{fmt::format("Element {}/{:d} has no relation member attribute", parser.get<0>(), parser.get<1>())};
    }

    for (const auto& [type, ref, role] : parser.get<8>()) {
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

  void process_tags(OSMObject &o, ElementsParser& parser) const {

    if (parser.parser<6>().isSet()) {
      for (const auto &[key, value] : parser.get<6>()) {
         o.add_tag(key, value);
      }
    }
  }

  void init_object(OSMObject &object, ElementsParser& parser) const {

    // id
    object.set_id(parser.get<1>());

    // version
    if (parser.parser<4>().isSet()) {
      object.set_version(parser.get<4>());
    }

    // changeset
    object.set_changeset(parser.get<5>());

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
