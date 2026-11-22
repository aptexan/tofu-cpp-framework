#pragma once

// tfsdk: the configuration, plan and state a provider reads and writes (the counterpart of the framework's tfsdk
// package). Each holds a value and the schema it follows. Read with GetAttribute (one attribute) or Get (all of them
// into a model struct); write with SetAttribute or Set.
//
// A model struct names its fields once, with member pointers (C++ has no struct tags to read):
//
//     struct ThingModel {
//       types::String id, name;
//       static auto Fields() {
//         return tfsdk::Fields(tfsdk::Attr("id", &ThingModel::id), tfsdk::Attr("name", &ThingModel::name));
//       }
//     };
//     ThingModel model; resp.diagnostics.Append(req.plan.Get(&model));
//
// The model must have exactly the attributes of the schema.

#include <tuple>

#include "tfframework/diag.h"
#include "tfframework/path.h"
#include "tfframework/schema.h"
#include "tfframework/types.h"

namespace tfframework::tfsdk {

template <class M, class T>
struct ModelField {
  const char* name;
  T M::*member;
};
template <class M, class T>
ModelField<M, T> Attr(const char* name, T M::*member) { return {name, member}; }
template <class... Fs>
auto Fields(Fs... fields) { return std::make_tuple(fields...); }

class Data {
 public:
  Data(const schema::Schema* schema, tftypes::Value raw) : schema_(schema), raw_(std::move(raw)) {}

  const tftypes::Value& Raw() const { return raw_; }
  const schema::Schema& GetSchema() const { return *schema_; }

  // Reads the attribute at `at` as T (types::String, types::List<types::Int64>, ...).
  template <class T>
  diag::Diagnostics GetAttribute(const path::Path& at, T* target) const {
    diag::Diagnostics diagnostics;
    absl::StatusOr<tftypes::Value> value = tftypes::ValueAtPath(raw_, at.raw());
    if (!value.ok()) {
      diagnostics.AddAttributeError(at, "Value Read Error", std::string(value.status().message()));
      return diagnostics;
    }
    absl::Status status = types::Codec<T>::FromValue(*value, target);
    if (!status.ok()) {
      diagnostics.AddAttributeError(at, "Value Conversion Error", std::string(status.message()));
    }
    return diagnostics;
  }

  // Reads every attribute into the fields of `model`.
  template <class M>
  diag::Diagnostics Get(M* model) const {
    diag::Diagnostics diagnostics;
    if (raw_.kind() != tftypes::Value::Kind::kObject) {
      diagnostics.AddError("Value Conversion Error", "The value has no attributes to read (it is null or unknown).");
      return diagnostics;
    }
    auto fields = M::Fields();
    if (!CheckFields(fields, &diagnostics)) return diagnostics;
    std::apply([&](auto... field) { (ReadField(field, model, &diagnostics), ...); }, fields);
    return diagnostics;
  }

 protected:
  template <class... Fs>
  bool CheckFields(const std::tuple<Fs...>& fields, diag::Diagnostics* diagnostics) const {
    std::vector<std::string> names;
    std::apply([&](auto... field) { (names.push_back(field.name), ...); }, fields);
    bool ok = true;
    for (const std::string& name : names) {
      if (!schema_->attributes.count(name)) {
        diagnostics->AddError("Model Mismatch", "The model has a field \"" + name + "\" that is not an attribute of the schema.");
        ok = false;
      }
    }
    for (const auto& [name, attribute] : schema_->attributes) {
      bool found = false;
      for (const std::string& n : names) found = found || n == name;
      if (!found) {
        diagnostics->AddError("Model Mismatch", "The schema has an attribute \"" + name + "\" that the model has no field for.");
        ok = false;
      }
    }
    return ok;
  }
  template <class M, class T>
  void ReadField(const ModelField<M, T>& field, M* model, diag::Diagnostics* diagnostics) const {
    diagnostics->Append(GetAttribute(path::Root(field.name), &(model->*(field.member))));
  }

  const schema::Schema* schema_;
  tftypes::Value raw_;
};

// The configuration: read only.
class Config : public Data {
 public:
  using Data::Data;
};

// A value the provider may change: the plan and the state.
class MutableData : public Data {
 public:
  using Data::Data;

  // Replaces the attribute at `at`. If the whole value is null (a resource being created) it first becomes an object
  // with every attribute null.
  template <class T>
  diag::Diagnostics SetAttribute(const path::Path& at, const T& value) {
    diag::Diagnostics diagnostics;
    if (raw_.kind() != tftypes::Value::Kind::kObject) raw_ = NullObject();
    absl::StatusOr<tftypes::Value> updated = tftypes::ReplaceAtPath(raw_, at.raw(), types::Codec<T>::ToValue(value));
    if (!updated.ok()) {
      diagnostics.AddAttributeError(at, "Value Write Error", std::string(updated.status().message()));
      return diagnostics;
    }
    raw_ = *std::move(updated);
    return diagnostics;
  }

  // Replaces the whole value from the fields of `model`.
  template <class M>
  diag::Diagnostics Set(const M& model) {
    diag::Diagnostics diagnostics;
    auto fields = M::Fields();
    if (!CheckFields(fields, &diagnostics)) return diagnostics;
    std::map<std::string, tftypes::Value> attributes;
    std::apply([&](auto... field) { (WriteField(field, model, &attributes), ...); }, fields);
    raw_ = tftypes::Value::Object(std::move(attributes));
    return diagnostics;
  }

  // Replaces the raw value (the framework does this; a provider rarely needs to).
  void SetRaw(tftypes::Value raw) { raw_ = std::move(raw); }

 private:
  tftypes::Value NullObject() const {
    std::map<std::string, tftypes::Value> attributes;
    for (const auto& [name, attribute] : schema_->attributes) attributes[name] = tftypes::Value::Null();
    return tftypes::Value::Object(std::move(attributes));
  }
  template <class M, class T>
  void WriteField(const ModelField<M, T>& field, const M& model, std::map<std::string, tftypes::Value>* out) const {
    (*out)[field.name] = types::Codec<T>::ToValue(model.*(field.member));
  }
};

class Plan : public MutableData {
 public:
  using MutableData::MutableData;
};

class State : public MutableData {
 public:
  using MutableData::MutableData;
  // The resource no longer exists: the state becomes null.
  void RemoveResource() { SetRaw(tftypes::Value::Null()); }
};

}  // namespace tfframework::tfsdk
