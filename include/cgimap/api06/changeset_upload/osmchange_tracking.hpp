#ifndef OSMCHANGE_TRACKING_HPP
#define OSMCHANGE_TRACKING_HPP


#include <string>
#include <vector>

#include "types.hpp"


class OSMChange_Tracking {

public:

	struct object_id_mapping_t {
		osm_nwr_signed_id_t old_id;
		osm_nwr_id_t new_id;
		osm_version_t new_version;
	};

	OSMChange_Tracking();

	// created objects are kept separately for id replacement purposes
	std::vector<object_id_mapping_t> created_node_ids;
	std::vector<object_id_mapping_t> created_way_ids;
	std::vector<object_id_mapping_t> created_relation_ids;

	std::vector<object_id_mapping_t> modified_node_ids;
	std::vector<object_id_mapping_t> modified_way_ids;
	std::vector<object_id_mapping_t> modified_relation_ids;

	std::vector<osm_nwr_id_t> deleted_node_ids;
	std::vector<osm_nwr_id_t> deleted_way_ids;
	std::vector<osm_nwr_id_t> deleted_relation_ids;

	// in case the caller has provided an "if-unused" flag and requests
	// deletion for objects which are either (a) already deleted or
	// (b) still in used by another object, we have to return
	// old_id, new_id and version instead of raising an error message
	std::vector<object_id_mapping_t> skip_deleted_node_ids;
	std::vector<object_id_mapping_t> skip_deleted_way_ids;
	std::vector<object_id_mapping_t> skip_deleted_relation_ids;

};


#endif
