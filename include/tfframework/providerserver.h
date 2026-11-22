#pragma once

// providerserver: turns a provider::Provider into a running plugin (the counterpart of the framework's providerserver
// package). A provider's main() is one line:
//
//     int main(int argc, char** argv) {
//       return tfframework::providerserver::Serve(argc, argv, [] { return std::make_unique<MyProvider>(); });
//     }

#include <functional>
#include <memory>

#include "tfframework/provider.h"
#include "tfprotov6/tfprotov6.h"

namespace tfframework::providerserver {

using ProviderFactory = std::function<std::unique_ptr<provider::Provider>()>;

// The protocol-level server that implements the framework's behaviour for `provider`. Useful in tests, which can call
// it directly without gRPC, and for anyone who wants to run it with their own tf6server setup.
std::unique_ptr<tfprotov6::ProviderServer> NewProtocol6(std::unique_ptr<provider::Provider> provider);

// Runs the plugin: see tf6server::Serve. Returns the exit status of the process.
int Serve(int argc, char** argv, ProviderFactory factory);

}  // namespace tfframework::providerserver
