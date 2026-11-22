#pragma once

// types: attribute values that keep null, unknown and zero apart (the counterpart of the framework's types package).
// A types::String is not a std::string: it can also be null (not set) or unknown (not known until apply). A provider
// asks first, then reads:
//
//     types::String name; plan.GetAttribute(path::Root("name"), &name);
//     if (!name.IsNull() && !name.IsUnknown()) Use(name.ValueString());
//
// A default-constructed value is null. Codec<T> turns these into the low-level tftypes::Value and back.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "tftypes/tftypes.h"

namespace tfframework::types {

enum class ValueState { kNull, kUnknown, kKnown };

// --- scalars ----------------------------------------------------------------------------------------------------------

template <class Derived, class T>
class Scalar {
 public:
  static Derived Null() { return Derived(); }
  static Derived Unknown() { Derived d; d.state_ = ValueState::kUnknown; return d; }
  static Derived Known(T value) { Derived d; d.state_ = ValueState::kKnown; d.value_ = std::move(value); return d; }

  bool IsNull() const { return state_ == ValueState::kNull; }
  bool IsUnknown() const { return state_ == ValueState::kUnknown; }
  // Known and not null: there is a value to read.
  bool IsSet() const { return state_ == ValueState::kKnown; }
  bool operator==(const Scalar& other) const {
    return state_ == other.state_ && (state_ != ValueState::kKnown || value_ == other.value_);
  }

 protected:
  ValueState state_ = ValueState::kNull;
  T value_{};
};

class String : public Scalar<String, std::string> {
 public:
  const std::string& ValueString() const { return value_; }
};
class Bool : public Scalar<Bool, bool> {
 public:
  bool ValueBool() const { return value_; }
};
class Int64 : public Scalar<Int64, int64_t> {
 public:
  int64_t ValueInt64() const { return value_; }
};
class Float64 : public Scalar<Float64, double> {
 public:
  double ValueFloat64() const { return value_; }
};

inline String StringValue(std::string v) { return String::Known(std::move(v)); }
inline String StringNull() { return String::Null(); }
inline String StringUnknown() { return String::Unknown(); }
inline Bool BoolValue(bool v) { return Bool::Known(v); }
inline Bool BoolNull() { return Bool::Null(); }
inline Bool BoolUnknown() { return Bool::Unknown(); }
inline Int64 Int64Value(int64_t v) { return Int64::Known(v); }
inline Int64 Int64Null() { return Int64::Null(); }
inline Int64 Int64Unknown() { return Int64::Unknown(); }
inline Float64 Float64Value(double v) { return Float64::Known(v); }
inline Float64 Float64Null() { return Float64::Null(); }
inline Float64 Float64Unknown() { return Float64::Unknown(); }

// --- collections ------------------------------------------------------------------------------------------------------

template <class Derived, class Rep>
class Aggregate {
 public:
  static Derived Null() { return Derived(); }
  static Derived Unknown() { Derived d; d.state_ = ValueState::kUnknown; return d; }
  static Derived Known(Rep elements) { Derived d; d.state_ = ValueState::kKnown; d.elements_ = std::move(elements); return d; }

  bool IsNull() const { return state_ == ValueState::kNull; }
  bool IsUnknown() const { return state_ == ValueState::kUnknown; }
  bool IsSet() const { return state_ == ValueState::kKnown; }
  const Rep& Elements() const { return elements_; }
  bool operator==(const Aggregate& other) const {
    return state_ == other.state_ && (state_ != ValueState::kKnown || elements_ == other.elements_);
  }

 protected:
  ValueState state_ = ValueState::kNull;
  Rep elements_{};
};

template <class E> class List : public Aggregate<List<E>, std::vector<E>> {};
template <class E> class Set : public Aggregate<Set<E>, std::vector<E>> {};
template <class E> class Map : public Aggregate<Map<E>, std::map<std::string, E>> {};

// --- types, as a schema names them ------------------------------------------------------------------------------------

inline tftypes::TypePtr StringType() { return tftypes::Type::String(); }
inline tftypes::TypePtr BoolType() { return tftypes::Type::Bool(); }
inline tftypes::TypePtr Int64Type() { return tftypes::Type::Number(); }
inline tftypes::TypePtr Float64Type() { return tftypes::Type::Number(); }
inline tftypes::TypePtr ListType(tftypes::TypePtr element) { return tftypes::Type::List(std::move(element)); }
inline tftypes::TypePtr SetType(tftypes::TypePtr element) { return tftypes::Type::Set(std::move(element)); }
inline tftypes::TypePtr MapType(tftypes::TypePtr element) { return tftypes::Type::Map(std::move(element)); }

// --- Codec: types::X <-> tftypes::Value -------------------------------------------------------------------------------

template <class T>
struct Codec;

namespace internal {
inline absl::Status Mismatch(const char* wanted, const tftypes::Value& got) {
  return absl::InvalidArgumentError(std::string("expected ") + wanted + ", got " + got.DebugString());
}
template <class T, class Make, class Read, tftypes::Value::Kind kKind>
struct ScalarCodec {
  static tftypes::Value ToValue(const T& v) {
    if (v.IsNull()) return tftypes::Value::Null();
    if (v.IsUnknown()) return tftypes::Value::Unknown();
    return Make()(v);
  }
  static absl::Status FromValue(const tftypes::Value& value, T* out) {
    if (value.IsNull()) { *out = T::Null(); return absl::OkStatus(); }
    if (value.IsUnknown()) { *out = T::Unknown(); return absl::OkStatus(); }
    return Read()(value, out);
  }
};
struct MakeString { tftypes::Value operator()(const String& v) const { return tftypes::Value::String(v.ValueString()); } };
struct ReadString {
  absl::Status operator()(const tftypes::Value& v, String* out) const {
    if (v.kind() != tftypes::Value::Kind::kString) return Mismatch("a string", v);
    *out = StringValue(v.AsString()); return absl::OkStatus();
  }
};
struct MakeBool { tftypes::Value operator()(const Bool& v) const { return tftypes::Value::Bool(v.ValueBool()); } };
struct ReadBool {
  absl::Status operator()(const tftypes::Value& v, Bool* out) const {
    if (v.kind() != tftypes::Value::Kind::kBool) return Mismatch("a bool", v);
    *out = BoolValue(v.AsBool()); return absl::OkStatus();
  }
};
struct MakeInt64 { tftypes::Value operator()(const Int64& v) const { return tftypes::Value::Number(v.ValueInt64()); } };
struct ReadInt64 {
  absl::Status operator()(const tftypes::Value& v, Int64* out) const {
    if (v.kind() != tftypes::Value::Kind::kNumber) return Mismatch("a number", v);
    if (!v.IsInteger()) return Mismatch("a whole number that fits an int64", v);
    *out = Int64Value(v.AsInt64()); return absl::OkStatus();
  }
};
struct MakeFloat64 { tftypes::Value operator()(const Float64& v) const { return tftypes::Value::Number(v.ValueFloat64()); } };
struct ReadFloat64 {
  absl::Status operator()(const tftypes::Value& v, Float64* out) const {
    if (v.kind() != tftypes::Value::Kind::kNumber) return Mismatch("a number", v);
    *out = Float64Value(v.AsDouble()); return absl::OkStatus();
  }
};
}  // namespace internal

template <> struct Codec<String> : internal::ScalarCodec<String, internal::MakeString, internal::ReadString, tftypes::Value::Kind::kString> {
  static tftypes::TypePtr Type() { return StringType(); }
};
template <> struct Codec<Bool> : internal::ScalarCodec<Bool, internal::MakeBool, internal::ReadBool, tftypes::Value::Kind::kBool> {
  static tftypes::TypePtr Type() { return BoolType(); }
};
template <> struct Codec<Int64> : internal::ScalarCodec<Int64, internal::MakeInt64, internal::ReadInt64, tftypes::Value::Kind::kNumber> {
  static tftypes::TypePtr Type() { return Int64Type(); }
};
template <> struct Codec<Float64> : internal::ScalarCodec<Float64, internal::MakeFloat64, internal::ReadFloat64, tftypes::Value::Kind::kNumber> {
  static tftypes::TypePtr Type() { return Float64Type(); }
};

namespace internal {
// List and Set share everything but the kinds.
template <class C, class E, bool kIsSet>
struct SequenceCodec {
  static tftypes::TypePtr Type() { return kIsSet ? SetType(Codec<E>::Type()) : ListType(Codec<E>::Type()); }
  static tftypes::Value ToValue(const C& v) {
    if (v.IsNull()) return tftypes::Value::Null();
    if (v.IsUnknown()) return tftypes::Value::Unknown();
    std::vector<tftypes::Value> elements;
    for (const E& e : v.Elements()) elements.push_back(Codec<E>::ToValue(e));
    return kIsSet ? tftypes::Value::Set(std::move(elements)) : tftypes::Value::List(std::move(elements));
  }
  static absl::Status FromValue(const tftypes::Value& value, C* out) {
    if (value.IsNull()) { *out = C::Null(); return absl::OkStatus(); }
    if (value.IsUnknown()) { *out = C::Unknown(); return absl::OkStatus(); }
    if (value.kind() != (kIsSet ? tftypes::Value::Kind::kSet : tftypes::Value::Kind::kList)) {
      return Mismatch(kIsSet ? "a set" : "a list", value);
    }
    std::vector<E> elements;
    for (const tftypes::Value& item : value.Elements()) {
      E e;
      absl::Status status = Codec<E>::FromValue(item, &e);
      if (!status.ok()) return status;
      elements.push_back(std::move(e));
    }
    *out = C::Known(std::move(elements));
    return absl::OkStatus();
  }
};
}  // namespace internal

template <class E> struct Codec<List<E>> : internal::SequenceCodec<List<E>, E, false> {};
template <class E> struct Codec<Set<E>> : internal::SequenceCodec<Set<E>, E, true> {};
template <class E> struct Codec<Map<E>> {
  static tftypes::TypePtr Type() { return MapType(Codec<E>::Type()); }
  static tftypes::Value ToValue(const Map<E>& v) {
    if (v.IsNull()) return tftypes::Value::Null();
    if (v.IsUnknown()) return tftypes::Value::Unknown();
    std::map<std::string, tftypes::Value> entries;
    for (const auto& [key, e] : v.Elements()) entries[key] = Codec<E>::ToValue(e);
    return tftypes::Value::Map(std::move(entries));
  }
  static absl::Status FromValue(const tftypes::Value& value, Map<E>* out) {
    if (value.IsNull()) { *out = Map<E>::Null(); return absl::OkStatus(); }
    if (value.IsUnknown()) { *out = Map<E>::Unknown(); return absl::OkStatus(); }
    if (value.kind() != tftypes::Value::Kind::kMap) return internal::Mismatch("a map", value);
    std::map<std::string, E> entries;
    for (const auto& [key, item] : value.Entries()) {
      E e;
      absl::Status status = Codec<E>::FromValue(item, &e);
      if (!status.ok()) return status;
      entries[key] = std::move(e);
    }
    *out = Map<E>::Known(std::move(entries));
    return absl::OkStatus();
  }
};

}  // namespace tfframework::types
