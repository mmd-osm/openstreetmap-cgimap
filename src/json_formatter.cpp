/**
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of openstreetmap-cgimap (https://github.com/zerebubuth/openstreetmap-cgimap/).
 *
 * Copyright (C) 2009-2023 by the CGImap developer community.
 * For a full list of authors see the git log.
 */

#include "cgimap/json_formatter.hpp"

#include <chrono>

using namespace std::literals;

json_formatter::json_formatter(std::unique_ptr<json_writer> w) : writer(std::move(w)) {}

json_formatter::~json_formatter() = default;

mime::type json_formatter::mime_type() const { return mime::type::application_json; }

void json_formatter::write_tags(const tags_t &tags) {

  if (tags.empty())
    return;

  writer->object_key("tags");
  writer->start_object();
  for (const auto& [key, value] : tags) {
    writer->property(key, value);
  }
  writer->end_object();
}


void json_formatter::start_document(
  const std::string &generator, const std::string &root_name) {
  writer->start_object();

  writer->property("version"sv,     output_formatter::API_VERSION);
  writer->property("generator"sv,   generator);
  writer->property("copyright"sv,   output_formatter::COPYRIGHT);
  writer->property("attribution"sv, output_formatter::ATTRIBUTION);
  writer->property("license"sv,     output_formatter::LICENSE);
}

void json_formatter::write_bounds(const bbox &bounds) {
  writer->object_key("bounds"sv);
  writer->start_object();
  writer->property("minlat"sv, bounds.minlat);
  writer->property("minlon"sv, bounds.minlon);
  writer->property("maxlat"sv, bounds.maxlat);
  writer->property("maxlon"sv, bounds.maxlon);
  writer->end_object();
}

void json_formatter::end_document() {

  writer->end_array();            // end of elements array
  is_in_elements_array = false;
  writer->end_object();
}

void json_formatter::start_element_type(element_type type) {

  if (is_in_elements_array)
    return;

  writer->object_key("elements"sv);
  writer->start_array();
  is_in_elements_array = true;
}

void json_formatter::end_element_type(element_type type) {}

void json_formatter::start_action(action_type type) {
}

void json_formatter::end_action(action_type type) {
}

void json_formatter::error(const std::exception &e) {
  writer->start_object();
  writer->property("error", e.what());
  writer->end_object();
}

void json_formatter::write_id(const element_info &elem) {
  writer->property("id"sv, elem.id);
}

void json_formatter::write_common(const element_info &elem) {
  writer->property("timestamp"sv, elem.timestamp);
  writer->property("version"sv,   elem.version);
  writer->property("changeset"sv, elem.changeset);
  if (elem.display_name && elem.uid) {
    writer->property("user"sv, *elem.display_name);
    writer->property("uid"sv,  *elem.uid);
  }
  // At this itme, only the map call is really supported for JSON output,
  // where all elements are expected to be visible.
  if (!elem.visible) {
    writer->property("visible"sv, elem.visible);
  }
}

void json_formatter::write_node(const element_info &elem, double lon,
                                double lat, const tags_t &tags) {
  writer->start_object();

  writer->property("type"sv, "node"sv);

  write_id(elem);
  if (elem.visible) {
    writer->property("lat"sv, lat);
    writer->property("lon"sv, lon);
  }
  write_common(elem);
  write_tags(tags);

  writer->end_object();
}

void json_formatter::write_way(const element_info &elem, const nodes_t &nodes,
                               const tags_t &tags) {
  writer->start_object();

  writer->property("type"sv, "way"sv);

  write_id(elem);
  write_common(elem);

  if (!nodes.empty()) {
      writer->object_key("nodes"sv);
      writer->start_array();
      for (const auto &node : nodes) {
        writer->entry(node);
      }
      writer->end_array();
  }

  write_tags(tags);

  writer->end_object();
}

void json_formatter::write_relation(const element_info &elem,
                                    const members_t &members,
                                    const tags_t &tags) {
  writer->start_object();

  writer->property("type"sv, "relation"sv);

  write_id(elem);
  write_common(elem);

  if (!members.empty()) {
      writer->object_key("members"sv);
      writer->start_array();
      for (const auto & member : members) {
	  writer->start_object();
	  writer->property("type"sv, element_type_name(member.type));
	  writer->property("ref"sv,  member.ref);
	  writer->property("role"sv, member.role);
	  writer->end_object();
      }
      writer->end_array();
  }

  write_tags(tags);

  writer->end_object();
}

void json_formatter::write_changeset(const changeset_info &elem,
                                     const tags_t &tags,
                                     bool include_comments,
                                     const comments_t &comments,
                                     const std::chrono::system_clock::time_point &now) {

  writer->start_object();
  writer->property("type"sv, "changeset"sv);
  writer->property("id"sv, elem.id);
  writer->property("created_at"sv, elem.created_at);

  const bool is_open = elem.is_open_at(now);
  if (!is_open) {
      writer->property("closed_at"sv, elem.closed_at);
  }

  writer->property("open"sv, is_open);

  if (elem.display_name && bool(elem.uid)) {
    writer->property("user"sv, *elem.display_name);
    writer->property("uid"sv, *elem.uid);
  }

  if (elem.bounding_box) {
      writer->property("minlat"sv, elem.bounding_box->minlat);
      writer->property("minlon"sv, elem.bounding_box->minlon);
      writer->property("maxlat"sv, elem.bounding_box->maxlat);
      writer->property("maxlon"sv, elem.bounding_box->maxlon);
  }

  writer->property("comments_count"sv, elem.comments_count);
  writer->property("changes_count"sv, elem.num_changes);

  write_tags(tags);

  if (include_comments && !comments.empty()) {
      writer->object_key("discussion"sv);
      writer->start_array();
      for (const auto & comment : comments) {
	  writer->start_object();
	  writer->property("id"sv,   comment.id);
	  writer->property("date"sv, comment.created_at);
	  writer->property("uid"sv,  comment.author_id);
	  writer->property("user"sv, comment.author_display_name);
	  writer->property("text"sv, comment.body);
	  writer->end_object();
      }
      writer->end_array();
  }

  writer->end_object();
}

void json_formatter::write_diffresult_create_modify(const element_type elem,
                                            const osm_nwr_signed_id_t old_id,
                                            const osm_nwr_id_t new_id,
                                            const osm_version_t new_version)
{

//  writer->start_object();
//  writer->object_key("type");
//  writer->entry_string(element_type_name(elem));
//  writer->object_key("old_id");
//  writer->entry_int(old_id);
//  writer->object_key("new_id");
//  writer->entry_int(new_id);
//  writer->object_key("new_version");
//  writer->entry_int(new_version);
//  writer->end_object();
}


void json_formatter::write_diffresult_delete(const element_type elem,
                                            const osm_nwr_signed_id_t old_id)
{
//  writer->start_object();
//  writer->object_key("type");
//  writer->entry_string(element_type_name(elem));
//  writer->object_key("old_id");
//  writer->entry_int(old_id);
//  writer->end_object();
}

void json_formatter::flush() { writer->flush(); }

void json_formatter::error(const std::string &s) { writer->error(s); }
