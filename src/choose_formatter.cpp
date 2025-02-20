/**
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of openstreetmap-cgimap (https://github.com/zerebubuth/openstreetmap-cgimap/).
 *
 * Copyright (C) 2009-2024 by the CGImap developer community.
 * For a full list of authors see the git log.
 */

#include "cgimap/choose_formatter.hpp"
#include "cgimap/output_writer.hpp"
#include "cgimap/mime_types.hpp"
#include "cgimap/http.hpp"
#include "cgimap/request_helpers.hpp"
#include "cgimap/xml_writer.hpp"
#include "cgimap/xml_formatter.hpp"
#if HAVE_YAJL
#include "cgimap/json_writer.hpp"
#include "cgimap/json_formatter.hpp"
#endif
#include "cgimap/text_writer.hpp"
#include "cgimap/text_formatter.hpp"
#include "cgimap/logger.hpp"
#include "cgimap/util.hpp"

#include <stdexcept>
#include <list>
#include <map>
#include <limits>
#include <optional>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

#include <fmt/core.h>

// https://cpp.godbolt.org/z/7cYWof1cq

AcceptHeader::AcceptHeader(const std::string &header) {

  acceptedTypes = parse(header);

  std::sort(acceptedTypes.begin(), acceptedTypes.end(),
            [](const AcceptElement &a, const AcceptElement &b) {
              return std::tie(a.q, a.type, a.subtype) >
                     std::tie(b.q, b.type, b.subtype);
            });

  for (const auto &acceptedType : acceptedTypes)
    mapping[acceptedType.mimeType] = acceptedType.q;

  if (mapping.empty()) {
    throw http::bad_request("Accept header could not be parsed.");
  }
}

[[nodiscard]] bool AcceptHeader::is_acceptable(mime::type mt) const {
  return mapping.find(mt) != mapping.end() ||
         mapping.find(mime::type::any_type) != mapping.end();
}

[[nodiscard]] mime::type AcceptHeader::most_acceptable_of(
    const std::vector<mime::type> &available) const {
  mime::type best = mime::type::unspecified_type;
  double score = -1;

  // first, try for an exact match
  for (const auto &type : available) {
    auto itr = mapping.find(type);
    if ((itr != mapping.end()) && (itr->second > score)) {
      best = type;
      score = itr->second;
    }
  }

  // if no exact match found, check for wildcard match
  if (best == mime::type::unspecified_type && !available.empty()) {
    auto itr = mapping.find(mime::type::any_type);
    if (itr != mapping.end()) {
      best = available.front();
      score = itr->second;
    }
  }

  return best;
}

// Parse the accept header and return a vector containing all the
// information about the Accepted types
std::vector<AcceptHeader::AcceptElement> AcceptHeader::parse(const std::string &data) {

  std::vector<AcceptHeader::AcceptElement> acceptElements;

  // Split by comma to get individual media types
  auto items = split_trim(data, ',');

  for (const auto &item : items) {
    // Split each item by semicolon to separate media type from
    // parameters
    auto elems = split_trim(item, ';');

    if (elems.empty()) {
      continue;
    }

    // Treat Accept: * as Accept: */*
    if (elems[0] == "*")
      elems[0] = "*/*";

    // Split the media type into type and subtype
    auto mime_parts = split_trim(elems[0], '/');
    if (mime_parts.size() != 2 || mime_parts[1].empty()) {
      continue;
    }

    // figure out the mime::type from the string
    mime::type mime_type = mime::parse_from(elems[0]);
    if (mime_type == mime::type::unspecified_type) {
      continue;
    }

    AcceptElement acceptElement;
    acceptElement.raw = elems[0];
    acceptElement.type = mime_parts[0];
    acceptElement.subtype = mime_parts[1];
    acceptElement.mimeType = mime_type;

    // Parse parameters
    for (size_t i = 1; i < elems.size(); ++i) {
      auto param_parts = split_trim(elems[i], '=');
      if (param_parts.size() != 2)
        continue;

      if (param_parts[0] == "q") {
        try {
          acceptElement.q = std::stod(param_parts[1]);
        } catch (const std::exception &) {
          acceptElement.q = 0.0;
        }
      } else {
        acceptElement.params[param_parts[0]] = param_parts[1];
      }
    }

    acceptElements.push_back(acceptElement);
  }

  return acceptElements;
}

void test() {

//    std::string header = "text/*;q=0.3, text/html;q=0.7, text/html;level=1, text/html;level=2;q=0.4, */*;q=0.5";
//    AcceptHeader(header).display();
//    AcceptHeader("audio/*; q=0.2, audio/basic").display();
//    AcceptHeader("text/*, text/html, text/html;level=1, */*").display();
//    AcceptHeader("text, text/html").display();
//    AcceptHeader("text/plain; q=0.5, text/html, text/x-dvi; q=0.8, text/x-c").display();
//    AcceptHeader("application/json, text/javascript, */*; q=0.01");    // jquery example
}



namespace {

/**
 * figures out the preferred mime type(s) from the Accept headers, mapped to
 * their relative acceptability.
 */
AcceptHeader header_mime_type(const request &req) {
  // need to look at HTTP_ACCEPT request environment
  std::string accept_header = fcgi_get_env(req, "HTTP_ACCEPT", "*/*");
  return AcceptHeader(accept_header);
}

std::string mime_types_to_string(const std::vector<mime::type> &mime_types) {
  if (mime_types.empty()) {
    return "";
  }

  std::string result = mime::to_string(mime_types[0]);
  for (size_t i = 1; i < mime_types.size(); ++i) {
    result += ", " + mime::to_string(mime_types[i]);
  }
  return result;
}

}  // anonymous namespace

mime::type choose_best_mime_type(const request &req, const responder& hptr) {
  // figure out what, if any, the Accept-able resource mime types are
  auto types = header_mime_type(req);
  const std::vector<mime::type> types_available = hptr.types_available();

  mime::type best_type = hptr.resource_type();
  // check if the handler is capable of supporting an acceptable set of mime types
  if (best_type != mime::type::unspecified_type) {
    // check that this doesn't conflict with anything in the Accept header
    if (!hptr.is_available(best_type)) {
      throw http::not_acceptable(fmt::format("Acceptable formats for {} are: {}",
                               get_request_path(req),
                               mime_types_to_string(types_available)));
    }
    if (!types.is_acceptable(best_type)) {
      throw http::not_acceptable(fmt::format("Acceptable formats for {} are: {}",
                               get_request_path(req),
                               mime_types_to_string({best_type})));
    }
  } else {
    best_type = types.most_acceptable_of(types_available);
    // if none were acceptable then...
    if (best_type == mime::type::unspecified_type) {
      throw http::not_acceptable(fmt::format("Acceptable formats for {} are: {}",
                               get_request_path(req),
                               mime_types_to_string(types_available)));
    }
    if (best_type == mime::type::any_type && !types_available.empty()) {
      // choose the first of the available types if nothing is preferred
      best_type = types_available.front();
    }
    // otherwise we've chosen the most acceptable and available type
  }

  return best_type;
}

std::unique_ptr<output_formatter> create_formatter(mime::type best_type, output_buffer& out) {

  switch (best_type) {
    case mime::type::application_xml:
      return std::make_unique<xml_formatter>(std::make_unique<xml_writer>(out, true));

#if HAVE_YAJL
    case mime::type::application_json:
      return std::make_unique<json_formatter>(std::make_unique<json_writer>(out, false));
#endif
    case mime::type::text_plain:
      return std::make_unique<text_formatter>(std::make_unique<text_writer>(out, true));

    default:
      throw std::runtime_error(fmt::format("Could not create formatter for MIME type `{}'.", mime::to_string(best_type)));
  }
}
