/**
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of openstreetmap-cgimap (https://github.com/zerebubuth/openstreetmap-cgimap/).
 *
 * Copyright (C) 2009-2024 by the CGImap developer community.
 * For a full list of authors see the git log.
 */

#include "cgimap/bbox.hpp"
#include "cgimap/util.hpp"
#include <cmath>
#include <vector>

bbox::bbox(double minlat_, double minlon_, double maxlat_, double maxlon_)
    : minlat(minlat_), minlon(minlon_), maxlat(maxlat_), maxlon(maxlon_) {}

bool bbox::operator==(const bbox &other) const {
  return ((minlat == other.minlat) &&
          (minlon == other.minlon) &&
          (maxlat == other.maxlat) &&
          (maxlon == other.maxlon));
}

bool bbox::parse(const std::string &s) {
  const auto strs = split(s, ',');

  if (strs.size() != 4)
    return false;

  try {
    bbox b(std::stod(std::string{strs[1]}), std::stod(std::string{strs[0]}),
           std::stod(std::string{strs[3]}), std::stod(std::string{strs[2]}));

    // update the current object.
    *this = b;

  } catch(const std::invalid_argument&) {
    return false;
  } catch(const std::out_of_range &) {
    return false;
  }

  return true;
}

void bbox::clip_to_world() {
  minlon = std::max(minlon, -180.0);
  minlat = std::max(minlat, -90.0);
  maxlon = std::min(maxlon, 180.0);
  maxlat = std::min(maxlat, 90.0);
}

bool bbox::valid() const {
  bool is_valid = true;
  is_valid &= (minlon >= -180.0) && (minlon <= 180.0);
  is_valid &= (maxlon >= -180.0) && (maxlon <= 180.0);
  is_valid &= (minlat >= -90.0) && (minlat <= 90.0);
  is_valid &= (maxlat >= -90.0) && (maxlat <= 90.0);
  is_valid &= minlon <= maxlon;
  is_valid &= minlat <= maxlat;
  return is_valid;
}

double bbox::area() const { return (maxlon - minlon) * (maxlat - minlat); }
