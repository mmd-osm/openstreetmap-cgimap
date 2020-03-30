#include <pqxx/pqxx>
#include <iostream>
#include <sstream>

#include <boost/lambda/lambda.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <sys/wait.h>

#include "cgimap/bbox.hpp"
#include "cgimap/http.hpp"
#include "cgimap/logger.hpp"
#include "cgimap/options.hpp"
#include "cgimap/output_formatter.hpp"
#include "cgimap/output_writer.hpp"
#include "cgimap/handler.hpp"
#include "cgimap/routes.hpp"
#include "cgimap/request_helpers.hpp"
#include "cgimap/rate_limiter.hpp"
#include "cgimap/choose_formatter.hpp"
#include "cgimap/backend.hpp"
#include "cgimap/fcgi_request.hpp"
#include "cgimap/process_request.hpp"
#include "cgimap/config.hpp"

#ifdef ENABLE_APIDB
#include "cgimap/backend/apidb/apidb.hpp"
#endif

#include "cgimap/backend/staticxml/staticxml.hpp"

using std::runtime_error;
using std::vector;
using std::string;
using std::map;
using std::ostringstream;
using std::shared_ptr;
using boost::format;

namespace al = boost::algorithm;
namespace po = boost::program_options;

/**
 * global flags set by signal handlers.
 */
static bool terminate_requested = false;
static bool reload_requested = false;

/**
 * make a string to be used as the generator header
 * attribute of output files. includes some instance
 * identifying information.
 */
static string get_generator_string() {
  char hostname[HOST_NAME_MAX];
  if (gethostname(hostname, sizeof hostname) != 0) {
    throw std::runtime_error("gethostname returned error.");
  }

  return (boost::format(PACKAGE_STRING " (%1% %2%)") % getpid() % hostname)
      .str();
}

/**
 * convert an environment variable name to an option name
 */
static string environment_option_name(string name){
  string option;

  if (name.substr(0, 7) == "CGIMAP_") {
    std::transform(name.begin() + 7, name.end(),
                   std::back_inserter(option),
                   [](unsigned char c) {
                     return c == '_' ? '-' : std::tolower(c);
                   });
  }

  return option;
}

/**
 * Parse the options file for configuration options
 */
static void load_configuration(po::variables_map &options) {
  Options &config_options = Options::get_instance();
  if (options.count("configfile") != 0)
    config_options.parse_file(options["configfile"].as<string>());
  else
    config_options.parse_file();
}

/**
 * parse the comment line and environment for options.
 */
static void get_options(int argc, char **argv, po::variables_map &options) {
  po::options_description desc(PACKAGE_STRING ": Allowed options");

  // clang-format off
  desc.add_options()
    ("help", "display this help and exit")
    ("settings", "display configuration settings and exit")
    ("daemon", "run as a daemon")
    ("instances", po::value<int>()->default_value(Options::MAX_INSTANCES), "number of daemon instances to run")
    ("pidfile", po::value<string>(), "file to write pid to")
    ("logfile", po::value<string>(), "file to write log messages to")
    ("configfile", po::value<string>()->default_value(Options::CONFIG_FILE_PATH), "configuration file")
    ("memcache", po::value<string>(), "memcache server specification")
    ("ratelimit", po::value<int>(), "average number of bytes/s to allow each client")
    ("maxdebt", po::value<int>(), "maximum debt (in Mb) to allow each client before rate limiting")
    ("port", po::value<int>(), "FCGI port number (e.g. 8000) to listen on. This option is for backwards compatibility, please use --socket for new configurations.")
    ("socket", po::value<string>(), "FCGI port number (e.g. :8000) or UNIX socket to listen on")
    ;
  // clang-format on

  // add the backend options to the options description
  setup_backend_options(argc, argv, desc);

  po::store(po::parse_command_line(argc, argv, desc), options);
  po::store(po::parse_environment(desc, environment_option_name), options);
  po::notify(options);

  if (options.count("help")) {
    std::cout << desc << std::endl;
    output_backend_options(std::cout);
    exit(1);
  }

  // load configuration
  load_configuration(options);
  // override the configuration with commandline arguments
  Options &config_options = Options::get_instance();
  config_options.override_options(options);

  if (options.count("settings")) {
    std::cout << Options::get_instance().get_configuration_options() << std::endl;
    exit(1);
  }

  // for ability to accept both the old --port option in addition to socket if not available.
  if (config_options.get_run_as_daemon() && config_options.get_socket_path().empty()) {
    throw runtime_error("an FCGI port number or UNIX socket is required in daemon mode");
  }
}

/**
 * loop processing fasctgi requests until are asked to stop by
 * somebody sending us a TERM signal.
 */
static void process_requests(int socket, const po::variables_map &options) {
  // generator string - identifies the cgimap instance.
  string generator = get_generator_string();

  const Options &config_options = Options::get_instance();
  // open any log file
  if (!config_options.get_log_file_path().empty()) {
    logger::initialise(config_options.get_log_file_path());
  }

  // create the rate limiter
  memcached_rate_limiter limiter(options);

  // create the routes map (from URIs to handlers)
  routes route;

  // create the request object (persists over several calls)
  fcgi_request req(socket, std::chrono::system_clock::time_point());

  // create a factory for data selections - the mechanism for actually
  // getting at data.
  std::shared_ptr<data_selection::factory> factory = create_backend();

  std::shared_ptr<data_update::factory> update_factory = create_update_backend();

  std::shared_ptr<oauth::store> oauth_store = create_oauth_store();

  logger::message("Initialised");

  // enter the main loop
  while (!terminate_requested) {
    // process any reload request
    if (reload_requested) {
      if (!config_options.get_log_file_path().empty()) {
        logger::initialise(config_options.get_log_file_path());
      }

      reload_requested = false;
    }

    // get the next request
    if (req.accept_r() >= 0) {
      std::chrono::system_clock::time_point now(std::chrono::system_clock::now());
      req.set_current_time(now);
      process_request(req, limiter, generator, route, factory, update_factory, oauth_store);
    }
  }

  // finish up - dispose of the resources
  req.dispose();
}

/**
 * SIGTERM handler.
 */
static void terminate(int) {
  // termination has been requested
  terminate_requested = true;
}

/**
 * SIGHUP handler.
 */
static void reload(int) {
  // reload has been requested
  reload_requested = true;
}

/**
 * make the process into a daemon by detaching from the console.
 */
static void daemonise() {
  pid_t pid;
  struct sigaction sa;

  // fork to make sure we aren't a session leader
  if ((pid = fork()) < 0) {
    throw runtime_error("fork failed.");
  } else if (pid > 0) {
    exit(0);
  }

  // start a new session
  if (setsid() < 0) {
    throw runtime_error("setsid failed");
  }

  // install a SIGTERM handler
  sa.sa_handler = terminate;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGTERM, &sa, NULL) < 0) {
    throw runtime_error("sigaction failed");
  }

  // install a SIGHUP handler
  sa.sa_handler = reload;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGHUP, &sa, NULL) < 0) {
    throw runtime_error("sigaction failed");
  }

  // close standard descriptors
  close(0);
  close(1);
  close(2);
}

void setup_backends() {
#if ENABLE_APIDB
  register_backend(make_apidb_backend());
#endif
  register_backend(make_staticxml_backend());
}

int main(int argc, char **argv) {
  try {
    po::variables_map options;
    int socket;

    // set up all the backends
    setup_backends();

    // get options
    get_options(argc, argv, options);

    const Options &config_options = Options::get_instance();

    // get the socket to use
    if (!config_options.get_socket_path().empty()) {
      if ((socket = fcgi_request::open_socket(config_options.get_socket_path(), 5)) < 0) {
        throw runtime_error("Couldn't open FCGX socket.");
      }
    } else {
      socket = 0;
    }

    // are we supposed to run as a daemon?
    if (config_options.get_run_as_daemon()) {
      size_t instances = config_options.get_max_instances();

      bool children_terminated = false;
      std::set<pid_t> children;

      // make ourselves into a daemon
      daemonise();

      // record our pid if requested
      if (!config_options.get_pid_file_path().empty()) {
        std::ofstream pidfile(config_options.get_pid_file_path());
        pidfile << getpid() << std::endl;
      }

      // loop until we have been asked to stop and have no more children
      while (!terminate_requested || children.size() > 0) {
        pid_t pid;

        // start more children if we don't have enough
        while (!terminate_requested && (children.size() < instances)) {
          if ((pid = fork()) < 0) {
            throw runtime_error("fork failed.");
          } else if (pid == 0) {
            process_requests(socket, options);
            exit(0);
          }

          children.insert(pid);
        }

        // wait for a child to exit
        if ((pid = wait(NULL)) >= 0) {
          children.erase(pid);
        } else if (errno != EINTR) {
          throw runtime_error("wait failed.");
        }

        // pass on any termination request to our children
        if (terminate_requested && !children_terminated) {
          for (auto pid : children) { kill(pid, SIGTERM); }

          children_terminated = true;
        }

        // pass on any reload request to our children
        if (reload_requested) {
          for (auto pid : children) { kill(pid, SIGHUP); }

          reload_requested = false;
        }
      }

      // remove any pid file
      if (!config_options.get_pid_file_path().empty()) {
        remove(config_options.get_pid_file_path().c_str());
      }
    } else {
      // record our pid if requested
      if (!config_options.get_pid_file_path().empty()) {
        std::ofstream pidfile(config_options.get_pid_file_path());
        pidfile << getpid() << std::endl;
      }

      // do work here
      process_requests(socket, options);

      // remove any pid file
      if (!config_options.get_pid_file_path().empty()) {
        remove(config_options.get_pid_file_path().c_str());
      }
    }
  } catch (const pqxx::sql_error &er) {
    // Catch-all for query related postgres exceptions
    std::cerr << "Error: " << er.what() << std::endl
              << "Caused by: " << er.query() << std::endl;
    return 1;

  } catch (const pqxx::pqxx_exception &e) {
    // Catch-all for any other postgres exceptions
    std::cerr << "Error: " << e.base().what() << std::endl;
    return 1;

  } catch (const std::exception &e) {
    logger::message(e.what());
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
