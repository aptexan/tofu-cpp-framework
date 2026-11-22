#pragma once

// defaults: the value an optional + computed attribute gets when the configuration leaves it out (the counterpart of
// the framework's defaults and *default helper packages).
//     .optional = true, .computed = true, .default_value = defaults::Bool(true)

#include <string>

#include "tftypes/tftypes.h"

namespace tfframework::defaults {

inline tftypes::Value String(std::string v) { return tftypes::Value::String(std::move(v)); }
inline tftypes::Value Bool(bool v) { return tftypes::Value::Bool(v); }
inline tftypes::Value Int64(int64_t v) { return tftypes::Value::Number(v); }
inline tftypes::Value Float64(double v) { return tftypes::Value::Number(v); }

}  // namespace tfframework::defaults
