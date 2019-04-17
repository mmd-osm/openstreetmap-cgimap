#include "cgimap/config.hpp"
#include "cgimap/http.hpp"
#include "cgimap/logger.hpp"
#include "cgimap/request_helpers.hpp"

#include "cgimap/api06/changeset_upload/osmchange_handler.hpp"
#include "cgimap/api06/changeset_upload/osmchange_input_format.hpp"
#include "cgimap/api06/changeset_upload/osmchange_tracking.hpp"
#include "cgimap/api06/changeset_upload_handler.hpp"
#include "cgimap/backend/apidb/changeset_upload/changeset_updater.hpp"
#include "cgimap/backend/apidb/changeset_upload/node_updater.hpp"
#include "cgimap/backend/apidb/changeset_upload/relation_updater.hpp"
#include "cgimap/backend/apidb/changeset_upload/way_updater.hpp"
#include "cgimap/backend/apidb/transaction_manager.hpp"
#include "cgimap/infix_ostream_iterator.hpp"

#include "cgimap/types.hpp"
#include "cgimap/util.hpp"

#include <sstream>

#include <boost/format.hpp>
#include <crypto++/base64.h>
#include <crypto++/config.h>
#include <crypto++/filters.h>
#include <crypto++/sha3.h>
#include <crypto++/sha.h>

using boost::format;

namespace api06 {


std::string sha256_hash(const std::string& s) {
  using namespace CryptoPP;

  SHA256 hash;
  std::string digest;
  StringSource ss(s, true, new HashFilter(hash, new Base64Encoder(new StringSink(digest), false)));
  return digest;
}


changeset_upload_responder::changeset_upload_responder(
    mime::type mt, data_update_ptr & upd, osm_changeset_id_t id_,
    const std::string &payload,
    const std::string &idempotency_key,
    boost::optional<osm_user_id_t> user_id)
    : osm_diffresult_responder(mt), upd(upd) {

  osm_changeset_id_t changeset = id_;
  osm_user_id_t uid = *user_id;

  change_tracking = std::make_shared<OSMChange_Tracking>();

  auto changeset_updater = upd->get_changeset_updater(changeset, uid);
  auto node_updater = upd->get_node_updater(change_tracking);
  auto way_updater = upd->get_way_updater(change_tracking);
  auto relation_updater = upd->get_relation_updater(change_tracking);

  changeset_updater->lock_current_changeset();

  if (!idempotency_key.empty()) {

      // read db entry for changeset and idempotency_key
      std::string cached_payload{};
      std::string cached_hash_value{};

      bool cached_entry_exists = changeset_updater->load_from_cache_by_idempotency_key(idempotency_key, cached_payload, cached_hash_value);

      if (cached_entry_exists) {
	// calculate hash for payload
	std::string hash = sha256_hash(payload);

	// compare hash with db hash
	if (hash != cached_hash_value)
	  throw http::bad_request("Idempotency-Key was used before with a different payload");

	logger::message(format("Using cached diff result for given Idempotency-Key %1%") % idempotency_key);

	change_tracking->deserialize(cached_payload);

	// abort transaction to release lock on changeset
	upd->rollback();
	return;
      }
  }

  OSMChange_Handler handler(std::move(node_updater), std::move(way_updater),
                            std::move(relation_updater), changeset);

  OSMChangeXMLParser parser(&handler);

  parser.process_message(payload);

  change_tracking->populate_orig_sequence_mapping();

  if (!idempotency_key.empty()) {
      // calc hash for payload
      std::string hash_value = sha256_hash(payload);
      std::string payload = change_tracking->serialize();
      changeset_updater->save_to_cache_by_idempotency_key(idempotency_key, payload, hash_value);
  }

  changeset_updater->update_changeset(handler.get_num_changes(),
                                      handler.get_bbox());

  upd->commit();
}

changeset_upload_responder::~changeset_upload_responder() = default;

changeset_upload_handler::changeset_upload_handler(request &,
                                                   osm_changeset_id_t id_)
    : payload_enabled_handler(mime::unspecified_type,
                              http::method::POST | http::method::OPTIONS),
      id(id_) {}

changeset_upload_handler::~changeset_upload_handler() = default;

std::string changeset_upload_handler::log_name() const {
  return ((boost::format("changeset/upload %1%") % id).str());
}

responder_ptr_t
changeset_upload_handler::responder(data_selection_ptr &) const {
  throw http::server_error(
      "changeset_upload_handler: data_selection unsupported");
}

responder_ptr_t changeset_upload_handler::responder(
    data_update_ptr & upd,
    const std::string &payload,
    const std::string& idempotency_key,
    boost::optional<osm_user_id_t> user_id) const {
  return responder_ptr_t(
      new changeset_upload_responder(mime_type, upd, id, payload, idempotency_key, user_id));
}

} // namespace api06
