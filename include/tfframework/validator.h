#pragma once

// validator: checks on one attribute's configuration value (the counterpart of the framework's validator package and
// the *validator helper packages). A validator is a function. The framework calls every validator of an attribute when
// Core validates a configuration; the value may be null (not set) or unknown (not known until apply), and the stock
// validators ignore both. A validator reports by adding to response.diagnostics.
//
//     .validators = {validator::StringLengthBetween(1, 64),
//                    validator::ForKnown<types::String>([](const types::String& v, const validator::Request& req,
//                                                          validator::Response& resp) { ... })}

#include <functional>
#include <regex>
#include <string>

#include "tfframework/diag.h"
#include "tfframework/path.h"
#include "tfframework/types.h"

namespace tfframework::validator {

struct Request {
  path::Path path;                  // the attribute
  tftypes::Value config_value;      // its value in the configuration
  tftypes::Value config;            // the whole block's configuration (an object)
};
struct Response {
  diag::Diagnostics diagnostics;
};
using Validator = std::function<void(const Request&, Response&)>;

// A validator written for a typed value, called only when the value is known and not null. If the value is not of type T
// an error diagnostic is added (that is a bug in the schema, not in the configuration).
template <class T>
Validator ForKnown(std::function<void(const T&, const Request&, Response&)> check) {
  return [check = std::move(check)](const Request& request, Response& response) {
    T value;
    absl::Status status = types::Codec<T>::FromValue(request.config_value, &value);
    if (!status.ok()) {
      response.diagnostics.AddAttributeError(request.path, "Validator Type Error",
                                             "The validator does not fit the attribute's type: " + std::string(status.message()));
      return;
    }
    if (value.IsNull() || value.IsUnknown()) return;
    check(value, request, response);
  };
}

// Stock validators. Each ignores null and unknown values.
Validator StringLengthBetween(int min, int max);                          // length in bytes, both ends included
Validator StringMatches(std::regex pattern, std::string description);     // the whole string must match
Validator Int64Between(int64_t min, int64_t max);
Validator Float64Between(double min, double max);

}  // namespace tfframework::validator
