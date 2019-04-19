

#include "cgimap/backend/apidb/changeset_upload/changeset_updater.hpp"
#include "cgimap/backend/apidb/pqxx_string_traits.hpp"
#include "cgimap/http.hpp"
#include "cgimap/logger.hpp"

#include <boost/format.hpp>
#include <pqxx/pqxx>
#include <stdexcept>

using boost::format;

ApiDB_Changeset_Updater::ApiDB_Changeset_Updater(Transaction_Manager &_m,
                                                 osm_changeset_id_t _changeset,
                                                 osm_user_id_t _uid)
    : m(_m), cs_num_changes(0), changeset(_changeset), uid(_uid) {}

ApiDB_Changeset_Updater::~ApiDB_Changeset_Updater() = default;

void ApiDB_Changeset_Updater::lock_current_changeset() {

  {
    m.prepare("changeset_exists",
	      R"( SELECT id,
			 user_id
		  FROM changesets
		  WHERE id = $1)");

    pqxx::result r = m.exec_prepared("changeset_exists", changeset);

    if (r.affected_rows() != 1)
      throw http::not_found("");

    if (r[0]["user_id"].as<osm_user_id_t>() != uid)
      throw http::conflict("The user doesn't own that changeset");
  }

  // Only lock changeset if it belongs to user_id = uid
  m.prepare("changeset_current_lock",
            R"( SELECT id, 
                       user_id,
                       created_at,
                       min_lat,
                       max_lat,
                       min_lon,
                       max_lon,
                       num_changes, 
                       to_char(closed_at,'YYYY-MM-DD HH24:MI:SS "UTC"') as closed_at, 
                       ((now() at time zone 'utc') > closed_at) as is_closed,
                       to_char((now() at time zone 'utc'),'YYYY-MM-DD HH24:MI:SS "UTC"') as current_time
                FROM changesets WHERE id = $1 AND user_id = $2 
                FOR UPDATE 
             )");

  pqxx::result r = m.exec_prepared("changeset_current_lock", changeset, uid);

  if (r.affected_rows() != 1)
    throw http::conflict("The user doesn't own that changeset");

  if (r[0]["is_closed"].as<bool>())
    throw http::conflict((boost::format("The changeset %1% was closed at %2%") %
                          changeset % r[0]["closed_at"].as<std::string>())
                             .str());

  // Some clients try to send further changes, although the changeset already
  // holds the maximum number of elements. As this is futile, we raise an error
  // as early as possible.
  if (r[0]["num_changes"].as<int>() >= CHANGESET_MAX_ELEMENTS)
    throw http::conflict((boost::format("The changeset %1% was closed at %2%") %
                          changeset % r[0]["current_time"].as<std::string>())
                             .str());

  cs_num_changes = r[0]["num_changes"].as<int>();

  if (!(r.empty() || r[0]["min_lat"].is_null())) {
    cs_bbox.minlat = r[0]["min_lat"].as<int64_t>();
    cs_bbox.minlon = r[0]["min_lon"].as<int64_t>();
    cs_bbox.maxlat = r[0]["max_lat"].as<int64_t>();
    cs_bbox.maxlon = r[0]["max_lon"].as<int64_t>();
  }
}

void ApiDB_Changeset_Updater::update_changeset(const uint32_t num_new_changes,
                                               const bbox_t bbox) {

  // Don't raise an exception when reaching exactly CHANGESET_MAX_ELEMENTS!
  if (cs_num_changes + num_new_changes > CHANGESET_MAX_ELEMENTS) {

    auto r = m.exec(
        R"(SELECT to_char((now() at time zone 'utc'),'YYYY-MM-DD HH24:MI:SS "UTC"') as current_time)");

    throw http::conflict((boost::format("The changeset %1% was closed at %2%") %
                          changeset % r[0]["current_time"].as<std::string>())
                             .str());
  }

  cs_num_changes += num_new_changes;

  bbox_t undefined_bbox; // bounding box with default value outside valid
                         // lat/lon range

  // Update current changeset bounding box with new bounds
  cs_bbox.expand(bbox);

  bool valid_bbox = !(cs_bbox == undefined_bbox);

  /*
   update for closed_at according to logic in update_closed_at in
   changeset.rb:
   set the auto-close time to be one hour in the future unless
   that would make it more than 24h long, in which case clip to
   24h, as this has been decided is a reasonable time limit.

   */

  m.prepare("changeset_update",
            R"( 
       UPDATE changesets 
       SET num_changes = ($1 :: integer),
           min_lat = $2,
           min_lon = $3,
           max_lat = $4,
           max_lon = $5,
           closed_at = 
             CASE
                WHEN (closed_at - created_at) > 
                     (($6 ::interval) - ($7 ::interval)) THEN
                  created_at + ($7 ::interval)
                ELSE 
                  now() at time zone 'utc' + ($7 ::interval)
             END
       WHERE id = $8

       )");

  if (valid_bbox) {
    pqxx::result r =
        m.exec_prepared("changeset_update", cs_num_changes, cs_bbox.minlat,
             cs_bbox.minlon, cs_bbox.maxlat, cs_bbox.maxlon, MAX_TIME_OPEN,
             IDLE_TIMEOUT, changeset);

    if (r.affected_rows() != 1)
      throw http::server_error("Cannot update changeset");
  } else {
    pqxx::result r = m.prepared("changeset_update")(cs_num_changes)()()()()(              // TODO
                          MAX_TIME_OPEN)(IDLE_TIMEOUT)(changeset)
                         .exec();

    if (r.affected_rows() != 1)
      throw http::server_error("Cannot update changeset");
  }
}

/*


CREATE TABLE changeset_idempotency_cache
(
  id bigint NOT NULL,
  idempotency_key character varying,
  hash_value character varying,
  "timestamp" timestamp without time zone,
  payload jsonb,
  CONSTRAINT changeset_idempotency_cache_pk PRIMARY KEY (id),
  CONSTRAINT changeset_idempotency_cache_fk FOREIGN KEY (id)
      REFERENCES public.changesets (id) MATCH SIMPLE
      ON UPDATE NO ACTION ON DELETE NO ACTION
)

 */


bool ApiDB_Changeset_Updater::load_from_cache_by_idempotency_key(const std::string & idempotency_key,
								 std::string & cached_payload,
								 std::string & hash_value)
{
    cached_payload = "";
    hash_value = "";

    m.prepare("load_from_cache_by_idempotency_key",
    R"(
	  select q.hash_value, string_agg(q.row, E'\n') as payload  FROM (
	  
	  select concat_ws(' ', op, type, skipped, old_id, new_id, new_version) as row,
		 hash_value
	       from changeset_idempotency_cache 
	       join lateral jsonb_to_recordset(payload) as 
		    r("op" integer, 
		      "type" integer, 
		      "skipped" integer,
		      "old_id" bigint,
		      "new_id" bigint,
		      "new_version" bigint)
	       on true
	  where id = $1 AND idempotency_key = $2) q
	  GROUP BY q.hash_value;
    
    )");

    pqxx::result r = m.exec_prepared("load_from_cache_by_idempotency_key", changeset, idempotency_key);

    if (r.affected_rows() != 1)
      return false;  // cached entry was not found

    cached_payload = r[0]["payload"].as<std::string>();
    hash_value     = r[0]["hash_value"].as<std::string>();

    return true;
}

void ApiDB_Changeset_Updater::save_to_cache_by_idempotency_key(const std::string idempotency_key,
							       const std::string cached_string,
							       const std::string hash_value)
{

  {
    m.prepare("clear_cache_by_idempotency_key",
		R"( DELETE
		    FROM changeset_idempotency_cache
		    WHERE id = $1  )");

    pqxx::result r = m.exec_prepared("clear_cache_by_idempotency_key", changeset);
  }

  {
    m.prepare("save_to_cache_by_idempotency_key",
		R"( INSERT INTO changeset_idempotency_cache(id, idempotency_key, hash_value, timestamp, payload)
		    VALUES ($1, $2, $3, now() at time zone 'utc', $4) )");

    pqxx::result r = m.exec_prepared("save_to_cache_by_idempotency_key", changeset, idempotency_key, hash_value, cached_string);

    if (r.affected_rows() != 1)
      throw http::server_error("Could not store changeset for Idempotency-Key");
  }

}

