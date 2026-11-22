// The framework's behaviour: a tfprotov6::ProviderServer built from a provider::Provider. This is where the protocol's
// rules live, so that a provider does not repeat them. Each RPC below says which rules it applies. The plan algorithm
// follows terraform-plugin-framework's fwserver (PlanResourceChange), and the apply dispatch its Create/Update/Delete.

#include <mutex>

#include "absl/strings/str_cat.h"
#include "tf6server/tf6server.h"
#include "tfframework/providerserver.h"

namespace tfframework {
namespace {

using tftypes::Value;
namespace pb = tfprotov6;

class FrameworkServer final : public pb::ProviderServer {
 public:
  explicit FrameworkServer(std::unique_ptr<provider::Provider> provider) : provider_(std::move(provider)) {}

  // --- the provider ------------------------------------------------------------------------------------------------

  pb::GetProviderSchemaResponse GetProviderSchema(const pb::GetProviderSchemaRequest&) override {
    const Registry& registry = GetRegistry();
    pb::GetProviderSchemaResponse response;
    response.diagnostics = registry.diagnostics.items();
    response.provider = ToProtocol(registry.provider_schema);
    for (const auto& [name, entry] : registry.resources) response.resource_schemas[name] = ToProtocol(entry.schema);
    return response;
  }

  // Runs the attribute validators, then the provider's own ValidateConfig. (Protocol 6 validates only: Core never
  // changes the provider's configuration.)
  pb::ValidateProviderConfigResponse ValidateProviderConfig(const pb::ValidateProviderConfigRequest& request) override {
    const Registry& registry = GetRegistry();
    diag::Diagnostics diagnostics;
    std::optional<Value> config = Decode(request.config, *registry.provider_schema.ValueType(), "the provider configuration", &diagnostics);
    if (config) {
      ValidateAttributes(registry.provider_schema, *config, &diagnostics);
      if (auto* validating = dynamic_cast<provider::ProviderWithValidateConfig*>(provider_.get())) {
        provider::ValidateConfigResponse response;
        validating->ValidateConfig({tfsdk::Config(&registry.provider_schema, *config)}, &response);
        diagnostics.Append(response.diagnostics);
      }
    }
    return {diagnostics.items()};
  }

  pb::ConfigureProviderResponse ConfigureProvider(const pb::ConfigureProviderRequest& request) override {
    const Registry& registry = GetRegistry();
    diag::Diagnostics diagnostics;
    std::optional<Value> config = Decode(request.config, *registry.provider_schema.ValueType(), "the provider configuration", &diagnostics);
    if (!config) return {diagnostics.items()};
    provider::ConfigureResponse response;
    provider_->Configure({tfsdk::Config(&registry.provider_schema, *config)}, &response);
    diagnostics.Append(response.diagnostics);
    if (!response.diagnostics.HasError()) {
      std::lock_guard<std::mutex> lock(data_mutex_);
      provider_data_ = std::move(response.resource_data);
    }
    return {diagnostics.items()};
  }

  pb::StopProviderResponse StopProvider(const pb::StopProviderRequest&) override { return {}; }

  // --- resources: validate and upgrade -----------------------------------------------------------------------------

  pb::ValidateResourceConfigResponse ValidateResourceConfig(const pb::ValidateResourceConfigRequest& request) override {
    diag::Diagnostics diagnostics;
    const ResourceEntry* entry = Lookup(request.type_name, &diagnostics);
    if (!entry) return {diagnostics.items()};
    std::optional<Value> config = Decode(request.config, *entry->schema.ValueType(), "the configuration", &diagnostics);
    if (!config) return {diagnostics.items()};
    ValidateAttributes(entry->schema, *config, &diagnostics);
    std::unique_ptr<resource::Resource> instance = Instantiate(*entry, &diagnostics);
    if (auto* validating = dynamic_cast<resource::ResourceWithValidateConfig*>(instance.get())) {
      resource::ValidateConfigResponse response;
      validating->ValidateConfig({tfsdk::Config(&entry->schema, *config)}, &response);
      diagnostics.Append(response.diagnostics);
    }
    return {diagnostics.items()};
  }

  pb::ValidateDataResourceConfigResponse ValidateDataResourceConfig(const pb::ValidateDataResourceConfigRequest& request) override {
    diag::Diagnostics diagnostics;
    diagnostics.AddError("Unknown data source type", absl::StrCat("This provider has no data source type \"", request.type_name, "\"."));
    return {diagnostics.items()};
  }

  // The stored state has the current version: read it as it is. An older version: the resource's upgraders decide.
  pb::UpgradeResourceStateResponse UpgradeResourceState(const pb::UpgradeResourceStateRequest& request) override {
    diag::Diagnostics diagnostics;
    pb::UpgradeResourceStateResponse response;
    const ResourceEntry* entry = Lookup(request.type_name, &diagnostics);
    if (!entry) { response.diagnostics = diagnostics.items(); return response; }
    const tftypes::TypePtr type = entry->schema.ValueType();
    if (request.raw_state.json.empty()) {
      diagnostics.AddError("Unsupported state format", "The stored state is not JSON (a legacy flatmap state).");
      response.diagnostics = diagnostics.items();
      return response;
    }
    std::optional<Value> upgraded;
    if (request.version == entry->schema.version) {
      absl::StatusOr<Value> state = request.raw_state.UnmarshalJson(*type);
      if (!state.ok()) diagnostics.AddError("Cannot read the stored state", std::string(state.status().message()));
      else upgraded = *std::move(state);
    } else {
      std::unique_ptr<resource::Resource> instance = Instantiate(*entry, &diagnostics);
      std::map<int64_t, resource::StateUpgrader> upgraders;
      if (auto* upgrading = dynamic_cast<resource::ResourceWithUpgradeState*>(instance.get())) upgraders = upgrading->UpgradeState();
      auto found = upgraders.find(request.version);
      if (found == upgraders.end() || !found->second.upgrade) {
        diagnostics.AddError("Unsupported state version",
                             absl::StrCat("The stored state has schema version ", request.version,
                                          "; this provider can only read version ", entry->schema.version, "."));
      } else {
        resource::UpgradeStateRequest call{request.version, request.raw_state, std::nullopt};
        if (found->second.prior_schema) {
          absl::StatusOr<Value> prior = request.raw_state.UnmarshalJson(*found->second.prior_schema->ValueType());
          if (!prior.ok()) diagnostics.AddError("Cannot read the stored state", std::string(prior.status().message()));
          else call.state = tfsdk::State(&*found->second.prior_schema, *std::move(prior));
        }
        if (!diagnostics.HasError()) {
          resource::UpgradeStateResponse result{tfsdk::State(&entry->schema, Value::Null()), {}};
          found->second.upgrade(call, &result);
          diagnostics.Append(result.diagnostics);
          if (!result.diagnostics.HasError()) {
            if (result.state.Raw().IsNull()) {
              diagnostics.AddError("Missing Upgraded Resource State", "The state upgrader did not set the upgraded state.");
            } else {
              upgraded = result.state.Raw();
            }
          }
        }
      }
    }
    if (upgraded) Encode(*upgraded, *type, "the upgraded state", &response.upgraded_state, &diagnostics);
    response.diagnostics = diagnostics.items();
    return response;
  }

  // --- resources: read, plan, apply --------------------------------------------------------------------------------

  pb::ReadResourceResponse ReadResource(const pb::ReadResourceRequest& request) override {
    diag::Diagnostics diagnostics;
    pb::ReadResourceResponse response;
    response.private_state = request.private_state;
    const ResourceEntry* entry = Lookup(request.type_name, &diagnostics);
    if (!entry) { response.diagnostics = diagnostics.items(); return response; }
    const tftypes::TypePtr type = entry->schema.ValueType();
    std::optional<Value> current = Decode(request.current_state, *type, "the current state", &diagnostics);
    if (current) {
      std::unique_ptr<resource::Resource> instance = Instantiate(*entry, &diagnostics);
      resource::ReadResponse result{tfsdk::State(&entry->schema, *current), {}};
      instance->Read({tfsdk::State(&entry->schema, *current)}, &result);
      diagnostics.Append(result.diagnostics);
      Encode(result.state.Raw(), *type, "the new state", &response.new_state, &diagnostics);
    }
    response.diagnostics = diagnostics.items();
    return response;
  }

  // The plan, step by step (the order is the framework's):
  //   0. Core proposes: the configuration, with the prior value kept for computed attributes. A null proposal is a
  //      destroy: nothing to decide.
  //   1. Defaults: an attribute with a default, whose configuration value is null, gets the default.
  //   2. If the plan now differs from the prior state, every computed attribute that the configuration leaves null
  //      (and that has no default) becomes unknown: it may change. (A new resource always differs.)
  //   3. The attribute plan modifiers run, in order (UseStateForUnknown, RequiresReplace, ...).
  //   4. The resource's ModifyPlan runs, if it has one.
  pb::PlanResourceChangeResponse PlanResourceChange(const pb::PlanResourceChangeRequest& request) override {
    diag::Diagnostics diagnostics;
    pb::PlanResourceChangeResponse response;
    response.planned_private = request.prior_private;
    const ResourceEntry* entry = Lookup(request.type_name, &diagnostics);
    if (!entry) { response.diagnostics = diagnostics.items(); return response; }
    const schema::Schema& schema = entry->schema;
    const tftypes::TypePtr type = schema.ValueType();
    std::optional<Value> prior = Decode(request.prior_state, *type, "the prior state", &diagnostics);
    std::optional<Value> proposed = Decode(request.proposed_new_state, *type, "the proposed new state", &diagnostics);
    std::optional<Value> config = Decode(request.config, *type, "the configuration", &diagnostics);
    if (!prior || !proposed || !config) { response.diagnostics = diagnostics.items(); return response; }

    if (proposed->kind() != Value::Kind::kObject) {  // destroying (or nothing known yet)
      Encode(*proposed, *type, "the planned state", &response.planned_state, &diagnostics);
      response.diagnostics = diagnostics.items();
      return response;
    }
    std::map<std::string, Value> attributes = proposed->Entries();
    auto config_of = [&](const std::string& name) { return config->kind() == Value::Kind::kObject ? config->Attr(name) : Value::Null(); };

    for (const auto& [name, attribute] : schema.attributes) {  // 1
      if (attribute.default_value && config_of(name).IsNull()) attributes[name] = *attribute.default_value;
    }
    if (Value::Object(attributes) != *prior) {  // 2
      for (const auto& [name, attribute] : schema.attributes) {
        if (attribute.computed && !attribute.default_value && config_of(name).IsNull()) attributes[name] = Value::Unknown();
      }
    }
    std::vector<tftypes::AttributePath> replace;
    auto add_replace = [&](const tftypes::AttributePath& path) {
      for (const auto& known : replace) if (known == path) return;
      replace.push_back(path);
    };
    for (const auto& [name, attribute] : schema.attributes) {  // 3
      Value current = attributes[name];
      for (const planmodifier::PlanModifier& modify : attribute.plan_modifiers) {
        planmodifier::Request call{path::Root(name), config_of(name), current,
                                   prior->IsNull() ? Value::Null() : prior->Attr(name),
                                   *config, Value::Object(attributes), *prior};
        planmodifier::Response result{current, false, {}};
        modify(call, result);
        diagnostics.Append(result.diagnostics);
        current = result.plan_value;
        if (result.requires_replace) add_replace(path::Root(name).raw());
      }
      attributes[name] = current;
    }
    Value planned = Value::Object(attributes);
    std::unique_ptr<resource::Resource> instance = Instantiate(*entry, &diagnostics);
    if (auto* modifying = dynamic_cast<resource::ResourceWithModifyPlan*>(instance.get())) {  // 4
      resource::ModifyPlanResponse result{tfsdk::Plan(&schema, planned), {}, {}};
      modifying->ModifyPlan({tfsdk::Config(&schema, *config), tfsdk::Plan(&schema, planned), tfsdk::State(&schema, *prior)}, &result);
      diagnostics.Append(result.diagnostics);
      planned = result.plan.Raw();
      for (const path::Path& path : result.requires_replace) add_replace(path.raw());
    }
    Encode(planned, *type, "the planned state", &response.planned_state, &diagnostics);
    response.requires_replace = replace;
    response.diagnostics = diagnostics.items();
    return response;
  }

  // No prior state: Create. No planned state: Delete. Otherwise Update. Each starts from the state the framework
  // documents in resource.h.
  pb::ApplyResourceChangeResponse ApplyResourceChange(const pb::ApplyResourceChangeRequest& request) override {
    diag::Diagnostics diagnostics;
    pb::ApplyResourceChangeResponse response;
    response.private_state = request.planned_private;
    const ResourceEntry* entry = Lookup(request.type_name, &diagnostics);
    if (!entry) { response.diagnostics = diagnostics.items(); return response; }
    const schema::Schema& schema = entry->schema;
    const tftypes::TypePtr type = schema.ValueType();
    std::optional<Value> prior = Decode(request.prior_state, *type, "the prior state", &diagnostics);
    std::optional<Value> planned = Decode(request.planned_state, *type, "the planned state", &diagnostics);
    std::optional<Value> config = Decode(request.config, *type, "the configuration", &diagnostics);
    if (!prior || !planned || !config) { response.diagnostics = diagnostics.items(); return response; }
    std::unique_ptr<resource::Resource> instance = Instantiate(*entry, &diagnostics);
    if (diagnostics.HasError()) { response.diagnostics = diagnostics.items(); return response; }

    Value new_state;
    if (planned->IsNull()) {  // destroy
      resource::DeleteResponse result{tfsdk::State(&schema, *prior), {}};
      instance->Delete({tfsdk::State(&schema, *prior)}, &result);
      diagnostics.Append(result.diagnostics);
      new_state = result.diagnostics.HasError() ? result.state.Raw() : Value::Null();
    } else if (prior->IsNull()) {  // create
      resource::CreateResponse result{tfsdk::State(&schema, Value::Null()), {}};
      instance->Create({tfsdk::Config(&schema, *config), tfsdk::Plan(&schema, *planned)}, &result);
      diagnostics.Append(result.diagnostics);
      new_state = result.state.Raw();
      if (!result.diagnostics.HasError() && new_state.IsNull()) {
        diagnostics.AddError("Missing Resource State After Create",
                             "The provider created the resource but did not set its state.");
      }
    } else {  // update
      resource::UpdateResponse result{tfsdk::State(&schema, *planned), {}};
      instance->Update({tfsdk::Config(&schema, *config), tfsdk::Plan(&schema, *planned), tfsdk::State(&schema, *prior)}, &result);
      diagnostics.Append(result.diagnostics);
      new_state = result.state.Raw();
    }
    Encode(new_state, *type, "the new state", &response.new_state, &diagnostics);
    response.diagnostics = diagnostics.items();
    return response;
  }

 private:
  struct ResourceEntry {
    resource::ResourceFactory factory;
    schema::Schema schema;
  };
  struct Registry {
    schema::Schema provider_schema;
    std::map<std::string, ResourceEntry> resources;
    diag::Diagnostics diagnostics;
  };

  // Asks the provider and its resources for their names and schemas, once.
  const Registry& GetRegistry() {
    std::call_once(registry_once_, [this] {
      provider::MetadataResponse metadata;
      provider_->Metadata({}, &metadata);
      provider::SchemaResponse schema;
      provider_->Schema({}, &schema);
      registry_.provider_schema = schema.schema;
      registry_.diagnostics.Append(schema.diagnostics);
      registry_.diagnostics.Append(WithContext(schema.schema.ValidateImplementation(), "provider"));
      for (resource::ResourceFactory& factory : provider_->Resources()) {
        std::unique_ptr<resource::Resource> instance = factory();
        resource::MetadataResponse name;
        instance->Metadata({metadata.type_name}, &name);
        resource::SchemaResponse resource_schema;
        instance->Schema({}, &resource_schema);
        registry_.diagnostics.Append(resource_schema.diagnostics);
        registry_.diagnostics.Append(WithContext(resource_schema.schema.ValidateImplementation(), "resource " + name.type_name));
        if (registry_.resources.count(name.type_name)) {
          registry_.diagnostics.AddError("Duplicate Resource Type", "The provider defines the resource type \"" + name.type_name + "\" twice.");
          continue;
        }
        registry_.resources[name.type_name] = {std::move(factory), std::move(resource_schema.schema)};
      }
    });
    return registry_;
  }

  static diag::Diagnostics WithContext(const diag::Diagnostics& in, const std::string& where) {
    diag::Diagnostics out;
    for (const pb::Diagnostic& d : in.items()) out.AddError(d.summary, "In the schema of the " + where + ": " + d.detail);
    return out;
  }

  const ResourceEntry* Lookup(const std::string& type_name, diag::Diagnostics* diagnostics) {
    const Registry& registry = GetRegistry();
    auto found = registry.resources.find(type_name);
    if (found != registry.resources.end()) return &found->second;
    diagnostics->AddError("Unknown resource type", absl::StrCat("This provider has no resource type \"", type_name, "\"."));
    return nullptr;
  }

  // A fresh instance, configured with what the provider's Configure produced.
  std::unique_ptr<resource::Resource> Instantiate(const ResourceEntry& entry, diag::Diagnostics* diagnostics) {
    std::unique_ptr<resource::Resource> instance = entry.factory();
    if (auto* configurable = dynamic_cast<resource::ResourceWithConfigure*>(instance.get())) {
      resource::ConfigureRequest request;
      {
        std::lock_guard<std::mutex> lock(data_mutex_);
        request.provider_data = provider_data_;
      }
      resource::ConfigureResponse response;
      configurable->Configure(request, &response);
      diagnostics->Append(response.diagnostics);
    }
    return instance;
  }

  static void ValidateAttributes(const schema::Schema& schema, const Value& config, diag::Diagnostics* diagnostics) {
    if (config.kind() != Value::Kind::kObject) return;
    for (const auto& [name, attribute] : schema.attributes) {
      for (const validator::Validator& validate : attribute.validators) {
        validator::Response response;
        validate({path::Root(name), config.Attr(name), config}, response);
        diagnostics->Append(response.diagnostics);
      }
    }
  }

  // An empty DynamicValue is null. A value that does not fit the type is an error diagnostic, and nothing is returned.
  static std::optional<Value> Decode(const pb::DynamicValue& dynamic, const tftypes::Type& type, const char* what,
                                     diag::Diagnostics* diagnostics) {
    absl::StatusOr<Value> value = dynamic.Unmarshal(type);
    if (!value.ok()) {
      diagnostics->AddError(absl::StrCat("Cannot read ", what), std::string(value.status().message()));
      return std::nullopt;
    }
    return *std::move(value);
  }
  static bool Encode(const Value& value, const tftypes::Type& type, const char* what, pb::DynamicValue* out,
                     diag::Diagnostics* diagnostics) {
    absl::StatusOr<pb::DynamicValue> dynamic = pb::DynamicValue::New(value, type);
    if (!dynamic.ok()) {
      diagnostics->AddError(absl::StrCat("Cannot write ", what), std::string(dynamic.status().message()));
      return false;
    }
    *out = *std::move(dynamic);
    return true;
  }

  static pb::Schema ToProtocol(const schema::Schema& schema) {
    pb::Schema out;
    out.version = schema.version;
    out.block.description = schema.description;
    for (const auto& [name, a] : schema.attributes) {
      out.block.attributes.push_back({name, a.type, a.description, a.required, a.optional, a.computed, a.sensitive});
    }
    return out;
  }

  std::unique_ptr<provider::Provider> provider_;
  std::once_flag registry_once_;
  Registry registry_;
  std::mutex data_mutex_;
  std::any provider_data_;  // set by ConfigureProvider; read by every request that makes a resource
};

}  // namespace

namespace providerserver {

std::unique_ptr<tfprotov6::ProviderServer> NewProtocol6(std::unique_ptr<provider::Provider> provider) {
  return std::make_unique<FrameworkServer>(std::move(provider));
}

int Serve(int argc, char** argv, ProviderFactory factory) {
  return tf6server::Serve(argc, argv, [factory = std::move(factory)] { return NewProtocol6(factory()); });
}

}  // namespace providerserver
}  // namespace tfframework
