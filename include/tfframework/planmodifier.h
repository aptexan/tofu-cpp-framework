#pragma once

// planmodifier: changes to an attribute's planned value (the counterpart of the framework's planmodifier package and
// the *planmodifier helper packages). A plan modifier is a function. The framework calls the modifiers of every
// attribute, in order, after it has applied defaults and marked the computed attributes that may change as unknown.
//
//   RequiresReplace()       a change of this attribute destroys and re-creates the resource
//   UseStateForUnknown()    a computed attribute that is not set keeps its prior value instead of becoming unknown

#include <functional>
#include <string>

#include "tfframework/diag.h"
#include "tfframework/path.h"
#include "tftypes/tftypes.h"

namespace tfframework::planmodifier {

struct Request {
  path::Path path;              // the attribute
  tftypes::Value config_value;  // in the configuration
  tftypes::Value plan_value;    // in the plan so far
  tftypes::Value state_value;   // in the prior state; null when the resource is new
  tftypes::Value config;        // whole objects: configuration, plan so far, prior state (null for a new resource)
  tftypes::Value plan;
  tftypes::Value state;
};
struct Response {
  tftypes::Value plan_value;        // starts as the request's plan_value; set it to change the plan
  bool requires_replace = false;
  diag::Diagnostics diagnostics;
};
using PlanModifier = std::function<void(const Request&, Response&)>;

PlanModifier RequiresReplace();
PlanModifier UseStateForUnknown();

}  // namespace tfframework::planmodifier
