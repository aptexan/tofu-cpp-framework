#pragma once

// schema: what a provider or a resource looks like to Core (the counterpart of the framework's schema package): its
// attributes, their types, and for each whether it is required, optional, computed or sensitive, plus its default,
// validators and plan modifiers.
//
//     schema::Schema{.description = "...", .version = 1, .attributes = {
//         {"id",   {.type = types::StringType(), .computed = true, .plan_modifiers = {planmodifier::UseStateForUnknown()}}},
//         {"name", {.type = types::StringType(), .required = true}},
//     }};
//
// Fields of Attribute must be written in the order they are declared (a C++ rule for designated initializers).

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "tfframework/defaults.h"
#include "tfframework/diag.h"
#include "tfframework/planmodifier.h"
#include "tfframework/validator.h"
#include "tftypes/tftypes.h"

namespace tfframework::schema {

struct Attribute {
  tftypes::TypePtr type;
  std::string description;
  bool required = false;   // the configuration must set it
  bool optional = false;   // the configuration may set it
  bool computed = false;   // the provider may set it; with optional: the provider supplies a value if the config has none
  bool sensitive = false;  // Core hides the value in its output
  std::optional<tftypes::Value> default_value;  // needs computed
  std::vector<validator::Validator> validators;
  std::vector<planmodifier::PlanModifier> plan_modifiers;
};

struct Schema {
  std::string description;
  int64_t version = 0;  // bumped when the stored form of a resource changes; see resource::ResourceWithUpgradeState
  std::map<std::string, Attribute> attributes;

  // The object type every value of this schema (config, plan, state) is encoded with.
  tftypes::TypePtr ValueType() const;
  // Mistakes in the schema itself (an attribute that is neither required, optional nor computed, a default on an
  // attribute that is not computed, ...), found when Core asks for the schema.
  diag::Diagnostics ValidateImplementation() const;
};

}  // namespace tfframework::schema
