# Building tofu-plugin-framework-cpp

Needs `tofu-plugin-cpp` (the low-level framework) and gRPC: setup in `../tofu-plugin-cpp/BUILDING.md` and its `docs/setup.md`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
tools/run-all.sh --skip-negative        # a few seconds; without the option it also runs the negative suite (about 10 minutes)
```

`tofu-plugin-cpp` is found as an installed package (`-DCMAKE_PREFIX_PATH=<prefix>`), or else as the sibling folder `../tofu-plugin-cpp` (or `-DTOFU_PLUGIN_CPP_SOURCE_DIR=<folder>`), which is then built along. To use this library from another project: `cmake --install build --prefix ~/.local/tofu-cpp` and `find_package(TofuPluginFramework)`; link `tofu::framework`. Troubleshooting of gRPC problems: `../tofu-plugin-cpp/BUILDING.md`.
