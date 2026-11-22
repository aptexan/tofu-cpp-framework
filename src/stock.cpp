// The stock validators and plan modifiers.

#include <cmath>

#include "absl/strings/str_cat.h"
#include "tfframework/planmodifier.h"
#include "tfframework/validator.h"

namespace tfframework {

namespace validator {

Validator StringLengthBetween(int min, int max) {
  return ForKnown<types::String>([=](const types::String& value, const Request& request, Response& response) {
    const size_t length = value.ValueString().size();
    if (length < static_cast<size_t>(min) || length > static_cast<size_t>(max)) {
      response.diagnostics.AddAttributeError(
          request.path, "Invalid Attribute Value Length",
          absl::StrCat("Attribute ", request.path.ToString(), " string length must be between ", min, " and ", max,
                       ", got: ", length, "."));
    }
  });
}

Validator StringMatches(std::regex pattern, std::string description) {
  return ForKnown<types::String>([pattern = std::move(pattern), description = std::move(description)](
                                     const types::String& value, const Request& request, Response& response) {
    if (!std::regex_match(value.ValueString(), pattern)) {
      response.diagnostics.AddAttributeError(request.path, "Invalid Attribute Value Match",
                                             absl::StrCat("Attribute ", request.path.ToString(), " ", description, ", got: \"",
                                                          value.ValueString(), "\"."));
    }
  });
}

Validator Int64Between(int64_t min, int64_t max) {
  return ForKnown<types::Int64>([=](const types::Int64& value, const Request& request, Response& response) {
    if (value.ValueInt64() < min || value.ValueInt64() > max) {
      response.diagnostics.AddAttributeError(
          request.path, "Invalid Attribute Value",
          absl::StrCat("Attribute ", request.path.ToString(), " value must be between ", min, " and ", max,
                       ", got: ", value.ValueInt64(), "."));
    }
  });
}

Validator Float64Between(double min, double max) {
  return ForKnown<types::Float64>([=](const types::Float64& value, const Request& request, Response& response) {
    if (!(value.ValueFloat64() >= min && value.ValueFloat64() <= max)) {
      response.diagnostics.AddAttributeError(
          request.path, "Invalid Attribute Value",
          absl::StrCat("Attribute ", request.path.ToString(), " value must be between ", min, " and ", max,
                       ", got: ", value.ValueFloat64(), "."));
    }
  });
}

}  // namespace validator

namespace planmodifier {

// Go's *planmodifier.RequiresReplace: nothing for a new or destroyed resource, nothing if the value does not change.
PlanModifier RequiresReplace() {
  return [](const Request& request, Response& response) {
    if (request.state.IsNull()) return;  // being created
    if (request.plan.IsNull()) return;   // being destroyed
    if (request.plan_value == request.state_value) return;
    response.requires_replace = true;
  };
}

// Go's *planmodifier.UseStateForUnknown: a computed attribute that the configuration leaves out keeps its prior value.
PlanModifier UseStateForUnknown() {
  return [](const Request& request, Response& response) {
    if (request.state.IsNull()) return;                  // no prior value: being created
    if (!request.plan_value.IsUnknown()) return;         // already has a value
    if (!request.config_value.IsNull()) return;          // the configuration decides (it may itself be unknown)
    response.plan_value = request.state_value;
  };
}

}  // namespace planmodifier

}  // namespace tfframework
