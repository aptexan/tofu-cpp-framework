# Testing tofu-plugin-framework-cpp

The framework has no binary of its own, so its tests are **unit tests** that call it the way Core does: they build a small provider with the framework (`tests/server-tests.cpp`: a provider and a resource that use defaults, validators, plan modifiers, `ModifyPlan`, `ValidateConfig`, `UpgradeState`, `Configure`) and drive it through `tfprotov6::ProviderServer` (`GetProviderSchema`, `ValidateResourceConfig`, `PlanResourceChange`, `ApplyResourceChange`, ...). No gRPC, no OpenTofu. The attribute value types are tested in `tests/types-tests.cpp`. The end-to-end proof, with the real OpenTofu, is in `../workaround-provider/tools/check-data.sh`.

```sh
tools/run-all.sh --skip-negative      # the unit tests only: a few seconds
tools/run-all.sh                      # also the negative suite
```

Each check is one line (`PASS` / `FAIL`, with wanted and got on a failure), as in the other projects (see `../tofu-plugin-cpp/tools/TESTING.md`). `--skip-negative` leaves out the checks that expect an error (an invalid configuration, a missing state) and the negative suite.

**The tests are tested** (negative suite): `tools/negative-tests/negative-check.sh` copies this project and its sibling `tofu-plugin-cpp`, breaks the framework on purpose, one rule at a time (a default not applied, a computed attribute not marked unknown, `RequiresReplace` or `UseStateForUnknown` doing nothing, a validator or `ModifyPlan` not called, a destroyed resource keeping its state, an unknown schema mistake accepted, ...), rebuilds, and requires the unit tests to go red. It first requires the unbroken copy to be green, checks that every break builds and applies exactly once, and proves at the end that your sources and `build/` folders were not touched. `ONLY='regex'` runs a subset.
