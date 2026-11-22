#pragma once

// path: where in a configuration, plan or state an attribute is (the counterpart of the framework's path package).
//   path::Root("tags").AtMapKey("env")     path::Root("items").AtListIndex(0)

#include <string>

#include "tftypes/tftypes.h"

namespace tfframework::path {

class Path {
 public:
  Path() = default;
  Path AtName(std::string name) const { return Path(raw_.WithAttributeName(std::move(name))); }
  Path AtMapKey(std::string key) const { return Path(raw_.WithElementKeyString(std::move(key))); }
  Path AtListIndex(int64_t index) const { return Path(raw_.WithElementKeyInt(index)); }
  const tftypes::AttributePath& raw() const { return raw_; }
  std::string ToString() const { return raw_.ToString(); }
  bool operator==(const Path& other) const { return raw_ == other.raw_; }

 private:
  explicit Path(tftypes::AttributePath raw) : raw_(std::move(raw)) {}
  tftypes::AttributePath raw_;
};

inline Path Root(std::string name) { return Path().AtName(std::move(name)); }

}  // namespace tfframework::path
