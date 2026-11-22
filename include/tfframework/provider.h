#pragma once

// provider: what a provider implements (the counterpart of the framework's provider package): its type name, the schema
// of its configuration block, what to do when Core configures it, and the resource types it offers.
// Not covered yet: data sources, functions, ephemeral resources, ProviderWithConfigValidators, provider meta.

#include <any>
#include <string>
#include <vector>

#include "tfframework/diag.h"
#include "tfframework/resource.h"
#include "tfframework/schema.h"
#include "tfframework/tfsdk.h"

namespace tfframework::provider {

struct MetadataRequest {};
struct MetadataResponse {
  std::string type_name;  // e.g. "workaround": its resources are named workaround_<something>
  std::string version;
};
struct SchemaRequest {};
struct SchemaResponse {
  schema::Schema schema;
  diag::Diagnostics diagnostics;
};
struct ConfigureRequest {
  tfsdk::Config config;
};
struct ConfigureResponse {
  std::any resource_data;  // handed to every resource's Configure (ResourceWithConfigure)
  diag::Diagnostics diagnostics;
};

class Provider {
 public:
  virtual ~Provider() = default;
  virtual void Metadata(const MetadataRequest&, MetadataResponse*) = 0;
  virtual void Schema(const SchemaRequest&, SchemaResponse*) = 0;
  virtual void Configure(const ConfigureRequest&, ConfigureResponse*) = 0;
  virtual std::vector<resource::ResourceFactory> Resources() = 0;
};

struct ValidateConfigRequest {
  tfsdk::Config config;
};
struct ValidateConfigResponse {
  diag::Diagnostics diagnostics;
};
class ProviderWithValidateConfig {
 public:
  virtual ~ProviderWithValidateConfig() = default;
  virtual void ValidateConfig(const ValidateConfigRequest&, ValidateConfigResponse*) = 0;
};

}  // namespace tfframework::provider
