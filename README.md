# tofu-plugin-framework-cpp

The **high-level C++ framework** for writing OpenTofu / Terraform providers. It is the C++ counterpart of Go's
[terraform-plugin-framework](https://github.com/hashicorp/terraform-plugin-framework): type-safe schemas, a clear
separation of null, unknown and zero values, structured plan modifiers and validators, and a framework that owns the
protocol rules (defaults, unknown computed attributes, create / update / delete dispatch) so that a provider does not.
It is built on `tofu-plugin-cpp` (the low-level framework) and uses only its public API.

```cpp
class MyProvider : public tfframework::provider::Provider { /* Metadata, Schema, Configure, Resources */ };
int main(int argc, char** argv) {
  return tfframework::providerserver::Serve(argc, argv, [] { return std::make_unique<MyProvider>(); });
}
```

A resource declares a `schema::Schema` (attributes with `.required`, `.optional`, `.computed`, `.sensitive`,
`.default_value`, `.validators`, `.plan_modifiers`) and implements Create / Read / Update / Delete; abilities
(`ResourceWithConfigure`, `ResourceWithValidateConfig`, `ResourceWithModifyPlan`, `ResourceWithUpgradeState`) are extra
base classes. Values are `types::String`, `Bool`, `Int64`, `Float64`, `List<E>`, `Set<E>`, `Map<E>`, read and written
through `tfsdk::Config / Plan / State`. A complete provider: `~/workaround-provider`.

- Build: [BUILDING.md](BUILDING.md). Tests: [tools/TESTING.md](tools/TESTING.md).
- Use it: `find_package(TofuPluginFramework)` after `cmake --install`, or have the folder next to yours; link `tofu::framework`.
- What is not covered yet (functions, data sources, import, nested attributes, ...): the session notes in
  `~/claude-sessions/tofu-provider-cpp/framework-separation/03-framework.md`.
# tofu-cpp-framework
