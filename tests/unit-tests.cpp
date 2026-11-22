// Unit tests of the framework: attribute value types, and the framework's behaviour as seen through tfprotov6 (the same
// calls Core makes, without gRPC and without OpenTofu). See tools/check-unit.sh.
//   --skip-negative   leave out the checks that expect an error (bad input, invalid configuration)

#include <cstring>

#include "check.h"

void RunTypesTests();
void RunServerTests();

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--skip-negative") == 0) {
      g_skip_negative = true;
    } else {
      std::cerr << "unknown option: " << argv[i] << " (the only option is --skip-negative)\n";
      return 2;
    }
  }
  if (g_skip_negative) std::cout << "== negative checks are skipped (--skip-negative)\n";
  RunTypesTests();
  RunServerTests();
  return Summary();
}
