#pragma once

// resource: what a resource type implements (the counterpart of the framework's resource package).
//
// A resource has a type name, a schema, and four operations on the outside world: Create, Read, Update, Delete. The
// framework does the rest of the protocol: it decodes what Core sends, applies defaults, marks computed attributes as
// unknown when the plan changes, runs validators and plan modifiers, decides which of Create, Update and Delete to call,
// and encodes the answer. Optional abilities are separate interfaces that a resource also derives from:
//   ResourceWithConfigure        receives what the provider's Configure produced (a client, settings, ...)
//   ResourceWithValidateConfig   checks that involve several attributes
//   ResourceWithModifyPlan       changes the plan after the attribute plan modifiers ran
//   ResourceWithUpgradeState     reads state stored with an older schema version
// Not covered yet: ImportState, MoveState, ResourceWithIdentity, ConfigValidators.

#include <any>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tfframework/diag.h"
#include "tfframework/schema.h"
#include "tfframework/tfsdk.h"

namespace tfframework::resource {

struct MetadataRequest {
  std::string provider_type_name;  // the provider's type name, e.g. "workaround"
};
struct MetadataResponse {
  std::string type_name;  // the resource's full type name, e.g. "workaround_example"
};
struct SchemaRequest {};
struct SchemaResponse {
  schema::Schema schema;
  diag::Diagnostics diagnostics;
};

struct CreateRequest {
  tfsdk::Config config;
  tfsdk::Plan plan;
};
struct CreateResponse {
  tfsdk::State state;  // starts null: the provider must set it
  diag::Diagnostics diagnostics;
};
struct ReadRequest {
  tfsdk::State state;
};
struct ReadResponse {
  tfsdk::State state;  // starts as the current state; RemoveResource() says the resource is gone
  diag::Diagnostics diagnostics;
};
struct UpdateRequest {
  tfsdk::Config config;
  tfsdk::Plan plan;
  tfsdk::State state;  // the prior state
};
struct UpdateResponse {
  tfsdk::State state;  // starts as the plan
  diag::Diagnostics diagnostics;
};
struct DeleteRequest {
  tfsdk::State state;
};
struct DeleteResponse {
  tfsdk::State state;  // starts as the prior state; becomes null by itself when Delete reports no error
  diag::Diagnostics diagnostics;
};

class Resource {
 public:
  virtual ~Resource() = default;
  virtual void Metadata(const MetadataRequest&, MetadataResponse*) = 0;
  virtual void Schema(const SchemaRequest&, SchemaResponse*) = 0;
  virtual void Create(const CreateRequest&, CreateResponse*) = 0;
  virtual void Read(const ReadRequest&, ReadResponse*) = 0;
  virtual void Update(const UpdateRequest&, UpdateResponse*) = 0;
  virtual void Delete(const DeleteRequest&, DeleteResponse*) = 0;
};

// Creates a fresh instance; the framework makes one per request.
using ResourceFactory = std::function<std::unique_ptr<Resource>()>;

// --- optional abilities -----------------------------------------------------------------------------------------------

struct ConfigureRequest {
  std::any provider_data;  // what provider::ConfigureResponse.resource_data held; empty if the provider is not configured
};
struct ConfigureResponse {
  diag::Diagnostics diagnostics;
};
class ResourceWithConfigure {
 public:
  virtual ~ResourceWithConfigure() = default;
  virtual void Configure(const ConfigureRequest&, ConfigureResponse*) = 0;
};

struct ValidateConfigRequest {
  tfsdk::Config config;
};
struct ValidateConfigResponse {
  diag::Diagnostics diagnostics;
};
class ResourceWithValidateConfig {
 public:
  virtual ~ResourceWithValidateConfig() = default;
  virtual void ValidateConfig(const ValidateConfigRequest&, ValidateConfigResponse*) = 0;
};

struct ModifyPlanRequest {
  tfsdk::Config config;
  tfsdk::Plan plan;
  tfsdk::State state;  // null (Raw().IsNull()) when the resource is being created
};
struct ModifyPlanResponse {
  tfsdk::Plan plan;  // starts as the request's plan
  std::vector<path::Path> requires_replace;
  diag::Diagnostics diagnostics;
};
class ResourceWithModifyPlan {
 public:
  virtual ~ResourceWithModifyPlan() = default;
  virtual void ModifyPlan(const ModifyPlanRequest&, ModifyPlanResponse*) = 0;
};

struct UpgradeStateRequest {
  int64_t version;                      // the schema version of the stored state
  tfprotov6::RawState raw_state;        // as stored (JSON)
  std::optional<tfsdk::State> state;    // the same, read with StateUpgrader::prior_schema, when that is given
};
struct UpgradeStateResponse {
  tfsdk::State state;  // starts null; the upgrader must set it (with the current schema)
  diag::Diagnostics diagnostics;
};
struct StateUpgrader {
  std::optional<schema::Schema> prior_schema;
  std::function<void(const UpgradeStateRequest&, UpgradeStateResponse*)> upgrade;
};
class ResourceWithUpgradeState {
 public:
  virtual ~ResourceWithUpgradeState() = default;
  // Keyed by the version it upgrades FROM. State already at the current version needs no upgrader.
  virtual std::map<int64_t, StateUpgrader> UpgradeState() = 0;
};

}  // namespace tfframework::resource
