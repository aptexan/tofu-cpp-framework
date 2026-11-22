#include "tfframework/schema.h"

namespace tfframework::schema {

tftypes::TypePtr Schema::ValueType() const {
  std::map<std::string, tftypes::TypePtr> types;
  for (const auto& [name, attribute] : attributes) types[name] = attribute.type;
  return tftypes::Type::Object(std::move(types));
}

diag::Diagnostics Schema::ValidateImplementation() const {
  diag::Diagnostics diagnostics;
  for (const auto& [name, a] : attributes) {
    auto error = [&](const std::string& what) {
      diagnostics.AddError("Invalid Schema", "Attribute \"" + name + "\" " + what);
    };
    if (!a.type) error("has no type.");
    if (!a.required && !a.optional && !a.computed) error("must be required, optional or computed.");
    if (a.required && a.optional) error("cannot be both required and optional.");
    if (a.required && a.computed) error("cannot be both required and computed.");
    if (a.default_value && !a.computed) error("has a default, so it must be computed.");
    if (a.default_value && a.required) error("is required, so it cannot have a default.");
  }
  return diagnostics;
}

}  // namespace tfframework::schema
