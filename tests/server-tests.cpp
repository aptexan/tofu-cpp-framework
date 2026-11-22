// The framework's behaviour, through tfprotov6: a small provider is built with the framework and driven with the calls
// that Core makes (GetProviderSchema, Validate, Plan, Apply, ...). Each check names the rule it tests; the rules follow
// terraform-plugin-framework (defaults, unknown computed attributes, plan modifiers, create/update/delete dispatch).

#include <algorithm>
#include <regex>

#include "check.h"
#include "tfframework/providerserver.h"

using namespace tfframework;
using tftypes::Type;
using tftypes::Value;
namespace pb = tfprotov6;

namespace {

std::vector<std::string> g_log;  // what the resource was asked to do

// --- the provider under test ------------------------------------------------------------------------------------------
//   t_thing: id computed (kept), name required (replaces), size optional 1..10, level optional+computed default 3,
//            note computed, tags optional map

struct ThingModel {
  types::String id, name, note;
  types::Int64 size, level;
  types::Map<types::String> tags;
  static auto Fields() {
    return tfsdk::Fields(tfsdk::Attr("id", &ThingModel::id), tfsdk::Attr("name", &ThingModel::name),
                         tfsdk::Attr("note", &ThingModel::note), tfsdk::Attr("size", &ThingModel::size),
                         tfsdk::Attr("level", &ThingModel::level), tfsdk::Attr("tags", &ThingModel::tags));
  }
};
struct OldModel {  // state of schema version 0
  types::String old_name;
  static auto Fields() { return tfsdk::Fields(tfsdk::Attr("old_name", &OldModel::old_name)); }
};
struct TooFewFields {  // a model that forgot attributes
  types::String id;
  static auto Fields() { return tfsdk::Fields(tfsdk::Attr("id", &TooFewFields::id)); }
};

class ThingResource final : public resource::Resource,
                    public resource::ResourceWithConfigure,
                    public resource::ResourceWithValidateConfig,
                    public resource::ResourceWithModifyPlan,
                    public resource::ResourceWithUpgradeState {
 public:
  void Metadata(const resource::MetadataRequest& req, resource::MetadataResponse* resp) override {
    resp->type_name = req.provider_type_name + "_thing";
  }
  void Schema(const resource::SchemaRequest&, resource::SchemaResponse* resp) override {
    resp->schema = schema::Schema{
        .description = "A thing.",
        .version = 1,
        .attributes = {
            {"id", {.type = types::StringType(), .computed = true, .plan_modifiers = {planmodifier::UseStateForUnknown()}}},
            {"name", {.type = types::StringType(), .required = true,
                      .validators = {validator::StringLengthBetween(1, 5)},
                      .plan_modifiers = {planmodifier::RequiresReplace()}}},
            {"note", {.type = types::StringType(), .computed = true}},
            {"size", {.type = types::Int64Type(), .optional = true, .validators = {validator::Int64Between(1, 10)}}},
            {"level", {.type = types::Int64Type(), .optional = true, .computed = true, .default_value = defaults::Int64(3)}},
            {"tags", {.type = types::MapType(types::StringType()), .description = "tags", .optional = true, .sensitive = true}},
        }};
  }
  void Configure(const resource::ConfigureRequest& req, resource::ConfigureResponse*) override {
    prefix_ = req.provider_data.has_value() ? std::any_cast<std::string>(req.provider_data) : "unconfigured";
  }
  void ValidateConfig(const resource::ValidateConfigRequest& req, resource::ValidateConfigResponse* resp) override {
    ThingModel model;
    resp->diagnostics.Append(req.config.Get(&model));
    if (model.size.IsSet() && model.name.IsSet() && model.name.ValueString() == "x") {
      resp->diagnostics.AddAttributeWarning(path::Root("size"), "size with x", "size is ignored for x.");
    }
  }
  void ModifyPlan(const resource::ModifyPlanRequest& req, resource::ModifyPlanResponse* resp) override {
    ThingModel plan, state;
    resp->diagnostics.Append(req.plan.Get(&plan));
    if (plan.name.IsSet() && plan.name.ValueString() == "boom") {
      resp->diagnostics.AddAttributeError(path::Root("name"), "No boom", "boom is not allowed.");
    }
    if (!req.state.Raw().IsNull()) {
      resp->diagnostics.Append(req.state.Get(&state));
      if (!(plan.tags == state.tags)) resp->requires_replace.push_back(path::Root("tags"));
    }
  }
  std::map<int64_t, resource::StateUpgrader> UpgradeState() override {
    resource::StateUpgrader from0;
    from0.prior_schema = schema::Schema{.attributes = {{"old_name", {.type = types::StringType(), .optional = true}}}};
    from0.upgrade = [](const resource::UpgradeStateRequest& req, resource::UpgradeStateResponse* resp) {
      OldModel old;
      resp->diagnostics.Append(req.state->Get(&old));
      ThingModel now;
      now.id = types::StringValue("upgraded");
      now.name = old.old_name;
      now.size = types::Int64Null();
      now.level = types::Int64Value(3);
      now.note = types::StringValue("upgraded");
      now.tags = types::Map<types::String>::Null();
      resp->diagnostics.Append(resp->state.Set(now));
    };
    return {{0, from0}};
  }

  void Create(const resource::CreateRequest& req, resource::CreateResponse* resp) override {
    ThingModel plan;
    resp->diagnostics.Append(req.plan.Get(&plan));
    g_log.push_back("create " + plan.name.ValueString());
    if (plan.name.ValueString() == "nostate") return;  // forgets to set the state
    if (plan.name.ValueString() == "fail") {
      resp->diagnostics.AddError("Cannot create", "no");
      return;
    }
    plan.id = types::StringValue(prefix_ + "-1");
    plan.note = types::StringValue("created");
    resp->diagnostics.Append(resp->state.Set(plan));
  }
  void Read(const resource::ReadRequest& req, resource::ReadResponse* resp) override {
    ThingModel state;
    resp->diagnostics.Append(req.state.Get(&state));
    g_log.push_back("read " + state.name.ValueString());
    if (state.name.ValueString() == "gone") resp->state.RemoveResource();
    if (state.name.ValueString() == "drift") {
      resp->diagnostics.Append(resp->state.SetAttribute(path::Root("note"), types::StringValue("drifted")));
    }
  }
  void Update(const resource::UpdateRequest& req, resource::UpdateResponse* resp) override {
    ThingModel plan;
    resp->diagnostics.Append(req.plan.Get(&plan));
    g_log.push_back("update " + plan.name.ValueString());
    // The state starts as the plan: only what is unknown has to be filled in.
    resp->diagnostics.Append(resp->state.SetAttribute(path::Root("note"), types::StringValue("updated")));
  }
  void Delete(const resource::DeleteRequest& req, resource::DeleteResponse* resp) override {
    ThingModel state;
    resp->diagnostics.Append(req.state.Get(&state));
    g_log.push_back("delete " + state.name.ValueString());
    if (state.name.ValueString() == "stuck") resp->diagnostics.AddError("Cannot delete", "stuck");
  }

 private:
  std::string prefix_;
};

class TestProvider final : public provider::Provider, public provider::ProviderWithValidateConfig {
 public:
  void Metadata(const provider::MetadataRequest&, provider::MetadataResponse* resp) override { resp->type_name = "t"; }
  void Schema(const provider::SchemaRequest&, provider::SchemaResponse* resp) override {
    resp->schema = schema::Schema{.description = "The test provider.",
                                  .attributes = {{"prefix", {.type = types::StringType(), .optional = true}}}};
  }
  void Configure(const provider::ConfigureRequest& req, provider::ConfigureResponse* resp) override {
    types::String prefix;
    resp->diagnostics.Append(req.config.GetAttribute(path::Root("prefix"), &prefix));
    resp->resource_data = std::string(prefix.IsSet() ? prefix.ValueString() : "p");
  }
  void ValidateConfig(const provider::ValidateConfigRequest& req, provider::ValidateConfigResponse* resp) override {
    types::String prefix;
    req.config.GetAttribute(path::Root("prefix"), &prefix);
    if (prefix.IsSet() && prefix.ValueString() == "BAD") {
      resp->diagnostics.AddAttributeError(path::Root("prefix"), "Bad prefix", "BAD is not a prefix.");
    }
  }
  std::vector<resource::ResourceFactory> Resources() override {
    return {[] { return std::make_unique<ThingResource>(); }};
  }
};

// A provider whose schema is wrong, to see the framework say so.
class BadResource final : public resource::Resource {
 public:
  void Metadata(const resource::MetadataRequest& req, resource::MetadataResponse* resp) override { resp->type_name = req.provider_type_name + "_bad"; }
  void Schema(const resource::SchemaRequest&, resource::SchemaResponse* resp) override {
    resp->schema.attributes = {
        {"nothing", {.type = types::StringType()}},
        {"default_without_computed", {.type = types::StringType(), .optional = true, .default_value = defaults::String("x")}},
    };
  }
  void Create(const resource::CreateRequest&, resource::CreateResponse*) override {}
  void Read(const resource::ReadRequest&, resource::ReadResponse*) override {}
  void Update(const resource::UpdateRequest&, resource::UpdateResponse*) override {}
  void Delete(const resource::DeleteRequest&, resource::DeleteResponse*) override {}
};
class BadProvider final : public provider::Provider {
 public:
  void Metadata(const provider::MetadataRequest&, provider::MetadataResponse* resp) override { resp->type_name = "t"; }
  void Schema(const provider::SchemaRequest&, provider::SchemaResponse*) override {}
  void Configure(const provider::ConfigureRequest&, provider::ConfigureResponse*) override {}
  std::vector<resource::ResourceFactory> Resources() override {
    return {[] { return std::make_unique<BadResource>(); }, [] { return std::make_unique<BadResource>(); }};
  }
};

// --- helpers ----------------------------------------------------------------------------------------------------------

const char* kAttributes[] = {"id", "name", "note", "size", "level", "tags"};

tftypes::TypePtr ThingType() {
  return Type::Object({{"id", Type::String()}, {"name", Type::String()}, {"note", Type::String()}, {"size", Type::Number()},
                       {"level", Type::Number()}, {"tags", Type::Map(Type::String())}});
}
// A thing: the attributes given, every other attribute null.
Value Thing(std::map<std::string, Value> given) {
  std::map<std::string, Value> all;
  for (const char* name : kAttributes) all[name] = Value::Null();
  for (auto& [name, value] : given) all[name] = std::move(value);
  return Value::Object(std::move(all));
}
Value Str(const char* s) { return Value::String(s); }
Value Num(int64_t n) { return Value::Number(n); }

pb::DynamicValue Dyn(const Value& value, tftypes::TypePtr type = ThingType()) { return pb::DynamicValue::New(value, *type).value(); }
Value Read(const pb::DynamicValue& dynamic, tftypes::TypePtr type = ThingType()) { return dynamic.Unmarshal(*type).value(); }

std::string Describe(const pb::Diagnostics& diagnostics) {
  if (diagnostics.empty()) return "none";
  std::string text;
  for (const pb::Diagnostic& d : diagnostics) {
    if (!text.empty()) text += "; ";
    text += d.severity == pb::Diagnostic::Severity::kError ? "error: " : "warning: ";
    text += d.summary + " [" + (d.attribute ? d.attribute->ToString() : "") + "]";
  }
  return text;
}
std::string Paths(const std::vector<tftypes::AttributePath>& paths) {
  std::string text;
  for (const auto& p : paths) text += (text.empty() ? "" : ", ") + p.ToString();
  return text.empty() ? "none" : text;
}
std::string Log() {
  std::string text;
  for (const auto& entry : g_log) text += (text.empty() ? "" : ", ") + entry;
  g_log.clear();
  return text.empty() ? "nothing" : text;
}

struct Fixture {
  std::unique_ptr<pb::ProviderServer> server = providerserver::NewProtocol6(std::make_unique<TestProvider>());

  pb::PlanResourceChangeResponse Plan(const Value& prior, const Value& proposed, const Value& config) {
    return server->PlanResourceChange({"t_thing", Dyn(prior), Dyn(proposed), Dyn(config), ""});
  }
  pb::ApplyResourceChangeResponse Apply(const Value& prior, const Value& planned, const Value& config = Value::Null()) {
    return server->ApplyResourceChange({"t_thing", Dyn(prior), Dyn(planned), Dyn(config), ""});
  }
  void Configure(const char* prefix) {
    auto type = Type::Object({{"prefix", Type::String()}});
    server->ConfigureProvider({"1", Dyn(Value::Object({{"prefix", Str(prefix)}}), type)});
  }
};

}  // namespace

void RunServerTests() {
  const Value stored = Thing({{"id", Str("p-1")}, {"name", Str("abc")}, {"note", Str("created")}, {"level", Num(3)}});

  Section("schema: what Core is told");
  {
    Fixture f;
    pb::GetProviderSchemaResponse schema = f.server->GetProviderSchema({});
    Expect("no diagnostics", Describe(schema.diagnostics), "none");
    Expect("the resource is named provider type + _thing", schema.resource_schemas.count("t_thing"), size_t{1});
    const pb::Schema& thing = schema.resource_schemas["t_thing"];
    Expect("the schema version", thing.version, int64_t{1});
    Expect("six attributes, sorted by name", thing.block.attributes.size(), size_t{6});
    Expect("first attribute", thing.block.attributes[0].name, "id");
    Expect("id is computed only", thing.block.attributes[0].computed && !thing.block.attributes[0].optional && !thing.block.attributes[0].required, true);
    auto attribute = [&](const std::string& name) {
      return *std::find_if(thing.block.attributes.begin(), thing.block.attributes.end(),
                           [&](const pb::SchemaAttribute& a) { return a.name == name; });
    };
    Expect("attributes are sorted by name", thing.block.attributes[1].name, "level");
    Expect("name is required", attribute("name").required, true);
    Expect("level is optional and computed", attribute("level").optional && attribute("level").computed, true);
    Expect("tags is sensitive", attribute("tags").sensitive, true);
    Expect("name is not sensitive", attribute("name").sensitive, false);
    Expect("tags carries its description", attribute("tags").description, "tags");
    Expect("tags type", attribute("tags").type->ToJson(), "[\"map\",\"string\"]");
    Expect("the provider schema", schema.provider.block.attributes.size(), size_t{1});
    Expect("no data sources", schema.data_source_schemas.size(), size_t{0});
  }
  SectionNegative("schema: mistakes in a schema are reported by the framework");
  if (!g_skip_negative) {
    auto server = providerserver::NewProtocol6(std::make_unique<BadProvider>());
    pb::GetProviderSchemaResponse schema = server->GetProviderSchema({});
    std::string all = Describe(schema.diagnostics);
    Expect("an attribute with no flag is reported", all.find("Invalid Schema") != std::string::npos, true);
    bool has_nothing = false, has_default = false, has_duplicate = false;
    for (const auto& d : schema.diagnostics) {
      has_nothing = has_nothing || d.detail.find("\"nothing\" must be required, optional or computed") != std::string::npos;
      has_default = has_default || d.detail.find("\"default_without_computed\" has a default, so it must be computed") != std::string::npos;
      has_duplicate = has_duplicate || d.summary == "Duplicate Resource Type";
    }
    Expect("... naming the attribute", has_nothing, true);
    Expect("a default without computed is reported", has_default, true);
    Expect("two resources with one name are reported", has_duplicate, true);
  }

  Section("validate: attribute validators and the resource's ValidateConfig");
  {
    Fixture f;
    auto validate = [&](const Value& config) { return Describe(f.server->ValidateResourceConfig({"t_thing", Dyn(config)}).diagnostics); };
    Expect("a good configuration", validate(Thing({{"name", Str("abc")}, {"size", Num(5)}})), "none");
    Expect("a null attribute is not validated", validate(Thing({{"name", Str("abc")}})), "none");
    Expect("an unknown attribute is not validated", validate(Thing({{"name", Value::Unknown()}, {"size", Value::Unknown()}})), "none");
    Expect("a warning from ValidateConfig points at its attribute", validate(Thing({{"name", Str("x")}, {"size", Num(1)}})),
           "warning: size with x [size]");
    if (!g_skip_negative) {
      Expect("a name that is too long", validate(Thing({{"name", Str("toolong")}})), "error: Invalid Attribute Value Length [name]");
      Expect("an empty name", validate(Thing({{"name", Str("")}})), "error: Invalid Attribute Value Length [name]");
      Expect("a size out of range", validate(Thing({{"name", Str("abc")}, {"size", Num(11)}})), "error: Invalid Attribute Value [size]");
      Expect("two mistakes: both are reported", validate(Thing({{"name", Str("toolong")}, {"size", Num(0)}})),
             "error: Invalid Attribute Value Length [name]; error: Invalid Attribute Value [size]");
      Expect("an unknown resource type", Describe(f.server->ValidateResourceConfig({"t_other", Dyn(Thing({}))}).diagnostics),
             "error: Unknown resource type []");
      Expect("an unknown data source type", Describe(f.server->ValidateDataResourceConfig({"t_data", Dyn(Thing({}))}).diagnostics),
             "error: Unknown data source type []");
      pb::DynamicValue bad;
      bad.msgpack = "\x01";
      Expect("a configuration that does not fit the schema", Describe(f.server->ValidateResourceConfig({"t_thing", bad}).diagnostics),
             "error: Cannot read the configuration []");
    }
  }
  Section("validate: the provider block");
  {
    Fixture f;
    auto type = Type::Object({{"prefix", Type::String()}});
    auto validate = [&](const Value& v) { return Describe(f.server->ValidateProviderConfig({Dyn(v, type)}).diagnostics); };
    Expect("a good provider configuration", validate(Value::Object({{"prefix", Str("ok")}})), "none");
    if (!g_skip_negative) {
      Expect("the provider's ValidateConfig runs", validate(Value::Object({{"prefix", Str("BAD")}})), "error: Bad prefix [prefix]");
    }
  }

  Section("plan: a new resource");
  {
    Fixture f;
    Value config = Thing({{"name", Str("abc")}});
    pb::PlanResourceChangeResponse plan = f.Plan(Value::Null(), config, config);
    Expect("no diagnostics", Describe(plan.diagnostics), "none");
    Value planned = Read(plan.planned_state);
    Expect("the default fills an optional+computed attribute", planned.Attr("level").DebugString(), Num(3).DebugString());
    Expect("a computed attribute becomes unknown (id)", planned.Attr("id").IsUnknown(), true);
    Expect("a computed attribute becomes unknown (note)", planned.Attr("note").IsUnknown(), true);
    Expect("an optional attribute stays null", planned.Attr("size").IsNull(), true);
    Expect("the configured value is kept", planned.Attr("name").DebugString(), Str("abc").DebugString());
    Expect("nothing needs replacing", Paths(plan.requires_replace), "none");
  }
  Section("plan: an existing resource");
  {
    Fixture f;
    Value config = Thing({{"name", Str("abc")}});
    Value same = Thing({{"id", Str("p-1")}, {"name", Str("abc")}, {"note", Str("created")}, {"level", Num(3)}});
    pb::PlanResourceChangeResponse none = f.Plan(stored, same, config);
    Expect("no change: the plan is the prior state", Read(none.planned_state) == stored, true);
    Expect("no change: nothing unknown", Read(none.planned_state).IsWhollyKnown(), true);

    Value changed = Thing({{"id", Str("p-1")}, {"name", Str("abc")}, {"note", Str("created")}, {"size", Num(4)}, {"level", Num(3)}});
    Value changed_config = Thing({{"name", Str("abc")}, {"size", Num(4)}});
    pb::PlanResourceChangeResponse update = f.Plan(stored, changed, changed_config);
    Value planned = Read(update.planned_state);
    Expect("a change: UseStateForUnknown keeps the id", planned.Attr("id").DebugString(), Str("p-1").DebugString());
    Expect("a change: a computed attribute without a modifier becomes unknown", planned.Attr("note").IsUnknown(), true);
    Expect("a change: a default stays known", planned.Attr("level").DebugString(), Num(3).DebugString());
    Expect("a change: no replacement", Paths(update.requires_replace), "none");

    Value renamed = Thing({{"id", Str("p-1")}, {"name", Str("xyz")}, {"note", Str("created")}, {"level", Num(3)}});
    pb::PlanResourceChangeResponse replace = f.Plan(stored, renamed, Thing({{"name", Str("xyz")}}));
    Expect("a new name: RequiresReplace names the attribute", Paths(replace.requires_replace), "name");

    Value tagged = Thing({{"id", Str("p-1")}, {"name", Str("abc")}, {"note", Str("created")}, {"level", Num(3)},
                          {"tags", Value::Map({{"a", Str("b")}})}});
    pb::PlanResourceChangeResponse retag = f.Plan(stored, tagged, Thing({{"name", Str("abc")}, {"tags", Value::Map({{"a", Str("b")}})}}));
    Expect("ModifyPlan can require a replacement too", Paths(retag.requires_replace), "tags");
    if (!g_skip_negative) {
      pb::PlanResourceChangeResponse boom = f.Plan(Value::Null(), Thing({{"name", Str("boom")}}), Thing({{"name", Str("boom")}}));
      Expect("ModifyPlan can fail the plan, pointing at an attribute", Describe(boom.diagnostics), "error: No boom [name]");
    }
    pb::PlanResourceChangeResponse destroy = f.Plan(stored, Value::Null(), Value::Null());
    Expect("a destroy plans null", Read(destroy.planned_state).IsNull(), true);
  }
  Section("plan: a configured value for a computed attribute is respected");
  {
    Fixture f;
    Value config = Thing({{"name", Str("abc")}, {"level", Num(9)}});
    Value planned = Read(f.Plan(Value::Null(), config, config).planned_state);
    Expect("level set in the configuration is not replaced by the default", planned.Attr("level").DebugString(), Num(9).DebugString());
  }

  Section("apply: create, update, delete");
  {
    Fixture f;
    f.Configure("px");
    Value planned = Thing({{"name", Str("abc")}, {"id", Value::Unknown()}, {"note", Value::Unknown()}, {"level", Num(3)}});
    pb::ApplyResourceChangeResponse created = f.Apply(Value::Null(), planned);
    Expect("create is called", Log(), "create abc");
    Expect("create: no diagnostics", Describe(created.diagnostics), "none");
    Value state = Read(created.new_state);
    Expect("create: the resource got what the provider's Configure handed over", state.Attr("id").DebugString(), Str("px-1").DebugString());
    Expect("create: the new state is what Create set", state.Attr("note").DebugString(), Str("created").DebugString());

    Value update_plan = Thing({{"id", Str("px-1")}, {"name", Str("abc")}, {"note", Value::Unknown()}, {"size", Num(2)}, {"level", Num(3)}});
    pb::ApplyResourceChangeResponse updated = f.Apply(state, update_plan);
    Expect("update is called", Log(), "update abc");
    Value new_state = Read(updated.new_state);
    Expect("update starts from the plan", new_state.Attr("size").DebugString(), Num(2).DebugString());
    Expect("update: what it set", new_state.Attr("note").DebugString(), Str("updated").DebugString());

    pb::ApplyResourceChangeResponse deleted = f.Apply(new_state, Value::Null());
    Expect("delete is called", Log(), "delete abc");
    Expect("delete: no diagnostics", Describe(deleted.diagnostics), "none");
    Expect("delete: the new state is null", Read(deleted.new_state).IsNull(), true);
    if (!g_skip_negative) {
      pb::ApplyResourceChangeResponse stuck = f.Apply(Thing({{"id", Str("i")}, {"name", Str("stuck")}, {"note", Str("n")}, {"level", Num(3)}}), Value::Null());
      Log();
      Expect("a failed delete is reported", Describe(stuck.diagnostics), "error: Cannot delete []");
      Expect("... and the state is kept", Read(stuck.new_state).Attr("name").DebugString(), Str("stuck").DebugString());
      pb::ApplyResourceChangeResponse failed = f.Apply(Value::Null(), Thing({{"name", Str("fail")}, {"id", Value::Unknown()}, {"note", Value::Unknown()}, {"level", Num(3)}}));
      Log();
      Expect("a failed create is reported", Describe(failed.diagnostics), "error: Cannot create []");
      pb::ApplyResourceChangeResponse nostate = f.Apply(Value::Null(), Thing({{"name", Str("nostate")}, {"id", Value::Unknown()}, {"note", Value::Unknown()}, {"level", Num(3)}}));
      Log();
      Expect("a create that sets no state is an error", Describe(nostate.diagnostics), "error: Missing Resource State After Create []");
      pb::ApplyResourceChangeResponse unknown = f.server->ApplyResourceChange({"t_other", Dyn(Value::Null()), Dyn(Value::Null()), Dyn(Value::Null()), ""});
      Expect("an unknown resource type", Describe(unknown.diagnostics), "error: Unknown resource type []");
    }
  }

  Section("read");
  {
    Fixture f;
    pb::ReadResourceResponse unchanged = f.server->ReadResource({"t_thing", Dyn(stored), "keep"});
    Expect("Read is called", Log(), "read abc");
    Expect("the state is returned as it was", Read(unchanged.new_state) == stored, true);
    Expect("private data is passed through", unchanged.private_state, "keep");
    Value gone = Thing({{"id", Str("i")}, {"name", Str("gone")}, {"note", Str("n")}, {"level", Num(3)}});
    Expect("RemoveResource makes the new state null", Read(f.server->ReadResource({"t_thing", Dyn(gone), ""}).new_state).IsNull(), true);
    Log();
    Value drift = Thing({{"id", Str("i")}, {"name", Str("drift")}, {"note", Str("n")}, {"level", Num(3)}});
    Expect("Read can change one attribute", Read(f.server->ReadResource({"t_thing", Dyn(drift), ""}).new_state).Attr("note").DebugString(),
           Str("drifted").DebugString());
    Log();
  }

  Section("upgrade state");
  {
    Fixture f;
    pb::UpgradeResourceStateRequest current;
    current.type_name = "t_thing";
    current.version = 1;
    current.raw_state.json = "{\"id\":\"p-1\",\"name\":\"abc\",\"note\":\"created\",\"size\":null,\"level\":3,\"tags\":null}";
    pb::UpgradeResourceStateResponse same = f.server->UpgradeResourceState(current);
    Expect("the current version is read as it is", Read(same.upgraded_state) == stored, true);
    pb::UpgradeResourceStateRequest old = current;
    old.version = 0;
    old.raw_state.json = "{\"old_name\":\"was\"}";
    pb::UpgradeResourceStateResponse upgraded = f.server->UpgradeResourceState(old);
    Expect("an older version goes through its upgrader", Describe(upgraded.diagnostics), "none");
    Expect("... which sees the prior schema", Read(upgraded.upgraded_state).Attr("name").DebugString(), Str("was").DebugString());
    Expect("... and writes the current one", Read(upgraded.upgraded_state).Attr("id").DebugString(), Str("upgraded").DebugString());
    if (!g_skip_negative) {
      pb::UpgradeResourceStateRequest future = current;
      future.version = 7;
      Expect("a version nobody can read", Describe(f.server->UpgradeResourceState(future).diagnostics), "error: Unsupported state version []");
      pb::UpgradeResourceStateRequest flat = current;
      flat.raw_state.json.clear();
      flat.raw_state.flatmap["id"] = "x";
      Expect("a flatmap state", Describe(f.server->UpgradeResourceState(flat).diagnostics), "error: Unsupported state format []");
      pb::UpgradeResourceStateRequest broken = current;
      broken.raw_state.json = "{\"id\": 5}";
      Expect("a stored state that does not fit", Describe(f.server->UpgradeResourceState(broken).diagnostics), "error: Cannot read the stored state []");
    }
  }

  Section("models");
  {
    schema::Schema schema{.attributes = {{"id", {.type = types::StringType(), .computed = true}}, {"extra", {.type = types::StringType(), .optional = true}}}};
    tfsdk::State state(&schema, Value::Null());
    Expect("SetAttribute on a null state makes an object", state.SetAttribute(path::Root("id"), types::StringValue("x")).empty(), true);
    Expect("... with the other attributes null", state.Raw().Attr("extra").IsNull(), true);
    types::String id;
    Expect("GetAttribute reads it back", state.GetAttribute(path::Root("id"), &id).empty() && id.ValueString() == "x", true);
    if (!g_skip_negative) {
      TooFewFields model;
      Expect("a model with too few fields is refused", Describe(state.Get(&model).items()), "error: Model Mismatch []");
      types::Int64 wrong;
      Expect("a wrong type is a conversion error pointing at the attribute", Describe(state.GetAttribute(path::Root("id"), &wrong).items()),
             "error: Value Conversion Error [id]");
      Expect("a path that is not there", Describe(state.GetAttribute(path::Root("nope"), &id).items()), "error: Value Read Error [nope]");
      tfsdk::State null_state(&schema, Value::Null());
      TooFewFields none;
      Expect("reading a null value into a model is refused", Describe(null_state.Get(&none).items()), "error: Value Conversion Error []");
    }
  }
}
