// Tests of the attribute value types: null, unknown and known stay apart, and they convert to tftypes::Value and back.

#include "check.h"
#include "tfframework/types.h"

using namespace tfframework::types;

namespace {

template <class T>
std::string RoundTrip(const T& in) {
  tftypes::Value value = Codec<T>::ToValue(in);
  T out;
  absl::Status status = Codec<T>::FromValue(value, &out);
  if (!status.ok()) return "ERROR: " + std::string(status.message());
  return Codec<T>::ToValue(out).DebugString();
}

std::string ReadInt(const tftypes::Value& value) {
  Int64 out;
  absl::Status status = Codec<Int64>::FromValue(value, &out);
  return status.ok() ? "ok" : "ERROR: " + std::string(status.message());
}

}  // namespace

void RunTypesTests() {
  Section("types: null, unknown and a value are three different things");
  String unset;
  Expect("a default-constructed value is null", unset.IsNull(), true);
  Expect("... and not unknown", unset.IsUnknown(), false);
  Expect("... and not set", unset.IsSet(), false);
  Expect("unknown is unknown", StringUnknown().IsUnknown(), true);
  Expect("unknown is not null", StringUnknown().IsNull(), false);
  Expect("an empty string is a value, not null", StringValue("").IsSet() && !StringValue("").IsNull(), true);
  Expect("a known value reads back", StringValue("x").ValueString(), "x");
  Expect("false is a value, not null", BoolValue(false).IsSet() && !BoolValue(false).ValueBool(), true);
  Expect("zero is a value, not null", Int64Value(0).IsSet(), true);
  Expect("null equals null", StringNull() == String(), true);
  Expect("null is not unknown", StringNull() == StringUnknown(), false);
  Expect("equal values are equal", StringValue("a") == StringValue("a"), true);
  Expect("different values differ", StringValue("a") == StringValue("b"), false);

  Section("types: conversion to the low-level value and back");
  Expect("string", RoundTrip(StringValue("hi")), tftypes::Value::String("hi").DebugString());
  Expect("string null", RoundTrip(StringNull()), tftypes::Value::Null().DebugString());
  Expect("string unknown", RoundTrip(StringUnknown()), tftypes::Value::Unknown().DebugString());
  Expect("bool", RoundTrip(BoolValue(true)), tftypes::Value::Bool(true).DebugString());
  Expect("int64", RoundTrip(Int64Value(42)), tftypes::Value::Number(int64_t{42}).DebugString());
  Expect("float64", RoundTrip(Float64Value(0.5)), tftypes::Value::Number(0.5).DebugString());
  Expect("a list of strings", RoundTrip(List<String>::Known({StringValue("a"), StringUnknown()})),
         tftypes::Value::List({tftypes::Value::String("a"), tftypes::Value::Unknown()}).DebugString());
  Expect("a set of numbers", RoundTrip(Set<Int64>::Known({Int64Value(2), Int64Value(1)})),
         tftypes::Value::Set({tftypes::Value::Number(int64_t{1}), tftypes::Value::Number(int64_t{2})}).DebugString());
  Expect("a map of bools", RoundTrip(Map<Bool>::Known({{"k", BoolValue(true)}})),
         tftypes::Value::Map({{"k", tftypes::Value::Bool(true)}}).DebugString());
  Expect("a null map", RoundTrip(Map<Bool>::Null()), tftypes::Value::Null().DebugString());
  Expect("a list's type", Codec<List<String>>::Type()->ToJson(), "[\"list\",\"string\"]");
  Expect("a map's type", Codec<Map<Int64>>::Type()->ToJson(), "[\"map\",\"number\"]");
  Expect("an int64 whole number reads as int64", ReadInt(tftypes::Value::Number(5.0)), "ok");
  SectionNegative("types: a value of the wrong kind is refused");
  ExpectNegative("a fraction is not an int64", ReadInt(tftypes::Value::Number(0.5)).substr(0, 5), "ERROR");
  ExpectNegative("a string is not an int64", ReadInt(tftypes::Value::String("x")).substr(0, 5), "ERROR");
  String s;
  ExpectNegative("a number is not a string", Codec<String>::FromValue(tftypes::Value::Number(int64_t{1}), &s).ok() ? "accepted" : "refused", "refused");
  List<String> l;
  ExpectNegative("a string is not a list", Codec<List<String>>::FromValue(tftypes::Value::String("x"), &l).ok() ? "accepted" : "refused", "refused");
  List<Int64> numbers;
  ExpectNegative("a bad element is reported",
                 Codec<List<Int64>>::FromValue(tftypes::Value::List({tftypes::Value::String("x")}), &numbers).ok() ? "accepted" : "refused",
                 "refused");
}
