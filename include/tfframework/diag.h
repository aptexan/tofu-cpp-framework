#pragma once

// diag: how a provider reports problems (the counterpart of the framework's diag package). Errors and warnings can be
// about the whole block or point at one attribute, which is what Core shows to the user.

#include <string>
#include <vector>

#include "tfframework/path.h"
#include "tfprotov6/tfprotov6.h"

namespace tfframework::diag {

class Diagnostics {
 public:
  void AddError(std::string summary, std::string detail) { Add(tfprotov6::Diagnostic::Severity::kError, std::move(summary), std::move(detail), std::nullopt); }
  void AddWarning(std::string summary, std::string detail) { Add(tfprotov6::Diagnostic::Severity::kWarning, std::move(summary), std::move(detail), std::nullopt); }
  void AddAttributeError(const path::Path& at, std::string summary, std::string detail) {
    Add(tfprotov6::Diagnostic::Severity::kError, std::move(summary), std::move(detail), at.raw());
  }
  void AddAttributeWarning(const path::Path& at, std::string summary, std::string detail) {
    Add(tfprotov6::Diagnostic::Severity::kWarning, std::move(summary), std::move(detail), at.raw());
  }
  void Append(const Diagnostics& other) { items_.insert(items_.end(), other.items_.begin(), other.items_.end()); }

  bool HasError() const {
    for (const auto& d : items_) if (d.severity == tfprotov6::Diagnostic::Severity::kError) return true;
    return false;
  }
  bool empty() const { return items_.empty(); }
  const std::vector<tfprotov6::Diagnostic>& items() const { return items_; }

 private:
  void Add(tfprotov6::Diagnostic::Severity severity, std::string summary, std::string detail,
           std::optional<tftypes::AttributePath> at) {
    items_.push_back({severity, std::move(summary), std::move(detail), std::move(at)});
  }
  std::vector<tfprotov6::Diagnostic> items_;
};

}  // namespace tfframework::diag
