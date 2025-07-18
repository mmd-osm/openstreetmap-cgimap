/**
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of openstreetmap-cgimap (https://github.com/zerebubuth/openstreetmap-cgimap/).
 *
 * Copyright (C) 2009-2025 by the openstreetmap-cgimap developer community.
 * For a full list of authors see the git log.
 */

#ifndef TEXT_FORMATTER_HPP
#define TEXT_FORMATTER_HPP

#include "cgimap/output_formatter.hpp"
#include "cgimap/text_writer.hpp"

#include <memory>

/**
 * Outputs an text formatted document
 */
class text_formatter : public output_formatter {
private:
  std::unique_ptr<text_writer> writer;

  void write_tags(const tags_t &tags);
  void write_common(const element_info &elem);

public:
  explicit text_formatter(std::unique_ptr<text_writer> w);
  ~text_formatter() override = default;

  mime::type mime_type() const override;

  void start_document(const std::string &generator, const std::string &root_name) override;
  void end_document() override;
  void write_bounds(const bbox &bounds) override;

  void start_element() override;
  void end_element() override;
  void start_changeset(bool) override;
  void end_changeset(bool) override;

  void start_action(action_type type) override;
  void end_action(action_type type) override;
  void error(const std::exception &e) override;

  void write_node(const element_info &elem, double lon, double lat,
                  const tags_t &tags) override;
  void write_way(const element_info &elem, const nodes_t &nodes,
                 const tags_t &tags) override;
  void write_relation(const element_info &elem, const members_t &members,
                      const tags_t &tags) override;

  void write_changeset(const changeset_info &elem,
                       const tags_t &tags,
                       bool include_comments,
                       const comments_t &comments,
                       const std::chrono::system_clock::time_point &now) override;

  void write_diffresult_create_modify(const element_type elem,
                                      const osm_nwr_signed_id_t old_id,
                                      const osm_nwr_id_t new_id,
                                      const osm_version_t new_version) override;

  void write_diffresult_delete(const element_type elem,
                               const osm_nwr_signed_id_t old_id) override;

  void flush() override;
  void error(const std::string &) override;
};

#endif /* TEXT_FORMATTER_HPP */
