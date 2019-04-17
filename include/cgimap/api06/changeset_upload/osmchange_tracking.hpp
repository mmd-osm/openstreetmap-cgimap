#ifndef OSMCHANGE_TRACKING_HPP
#define OSMCHANGE_TRACKING_HPP

#include <map>
#include <set>
#include <string>
#include <sstream>
#include <vector>

#include "cgimap/types.hpp"

namespace api06 {

class OSMChange_Tracking {

public:

  OSMChange_Tracking() = default;

  void populate_orig_sequence_mapping();

  struct object_id_mapping_t {
    osm_nwr_signed_id_t old_id;
    osm_nwr_id_t new_id;
    osm_version_t new_version;
  };

  struct osmchange_t {
    operation op;
    object_type obj_type;
    osm_nwr_signed_id_t orig_id;
    osm_version_t orig_version;
    bool if_unused;
    // the following fields will be populated once the whole message has been processed
    object_id_mapping_t mapping;
    bool deletion_skipped;         // if-unused flag was set, and object could not be deleted
  };


  // serialize osmchange_orig_sequence contents into a string
  std::string serialize() {

    std::string space{" "};
    std::stringstream ss;
    for (const auto & r : osmchange_orig_sequence) {
      ss << static_cast<int>(r.op) << space
             << static_cast<int>(r.obj_type) << space
	     << r.orig_id << space
	     << r.mapping.old_id << space
	     << r.mapping.new_id << space
	     << r.mapping.new_version << space
	     << r.deletion_skipped
	     << "\n";
    }

    return ss.str();
  }

  // deserialize string into osmchange_orig_sequence
  void deserialize(const std::string & payload) {
    osmchange_orig_sequence.clear();

    std::istringstream f(payload);
    std::string line;
    while (std::getline(f, line)) {
	osmchange_t row;
	std::stringstream ss(line);
	int _op;
	int _obj_type;
	ss >> _op
	   >> _obj_type
	   >> row.orig_id
	   >> row.mapping.old_id
	   >> row.mapping.new_id
	   >> row.mapping.new_version
	   >> row.deletion_skipped;
	row.op = static_cast< operation >(_op);
	row.obj_type = static_cast< object_type >(_obj_type);
	osmchange_orig_sequence.push_back(row);
    }

  }

  // created objects are kept separately for id replacement purposes
  std::vector<object_id_mapping_t> created_node_ids;
  std::vector<object_id_mapping_t> created_way_ids;
  std::vector<object_id_mapping_t> created_relation_ids;

  std::vector<object_id_mapping_t> modified_node_ids;
  std::vector<object_id_mapping_t> modified_way_ids;
  std::vector<object_id_mapping_t> modified_relation_ids;

  std::vector<osm_nwr_signed_id_t> deleted_node_ids;
  std::vector<osm_nwr_signed_id_t> deleted_way_ids;
  std::vector<osm_nwr_signed_id_t> deleted_relation_ids;

  // in case the caller has provided an "if-unused" flag and requests
  // deletion for objects which are either (a) already deleted or
  // (b) still in used by another object, we have to return
  // old_id, new_id and version instead of raising an error message
  std::vector<object_id_mapping_t> skip_deleted_node_ids;
  std::vector<object_id_mapping_t> skip_deleted_way_ids;
  std::vector<object_id_mapping_t> skip_deleted_relation_ids;

  // Some clients might expect diffResult to reflect the original
  // object sequence as provided in the osmChange message
  // the following vector keeps a copy of that original sequence
  std::vector<osmchange_t> osmchange_orig_sequence;
};

} // namespace api06

#endif
