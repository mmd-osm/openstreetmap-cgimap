#include "cgimap/text_responder.hpp"
#include "cgimap/config.hpp"

using std::list;
using std::shared_ptr;

text_responder::text_responder(mime::type mt)
    : output_text{}, responder(mt) {}

text_responder::~text_responder() = default;

list<mime::type> text_responder::types_available() const {
  list<mime::type> types;
  types.push_back(mime::text_plain);
  return types;
}

void text_responder::add_response_header(const std::string &line) {
  extra_headers << line << "\r\n";
}

std::string text_responder::extra_response_headers() const {
  return extra_headers.str();
}

void text_responder::write(std::shared_ptr<output_formatter> formatter,
                     const std::string &generator,
                     const std::chrono::system_clock::time_point &now)
{
  output_formatter &fmt = *formatter;
  fmt.error(output_text);
}

