#!/bin/bash
# Negative tests: do the unit tests notice when the framework is broken?
# A test that cannot fail proves nothing. This script breaks a COPY of the framework on purpose, one rule at a time,
# builds it and runs the unit tests, which must FAIL ("go red"). The copy holds this project and its sibling
# tofu-plugin-cpp (which it needs to build). Your sources and build/ are never touched, and the script proves it: it
# fingerprints both project folders before and after. Do not edit or rebuild them while it runs.
#   ONLY='regex'   runs only the breaks whose description matches (the unbroken baseline still runs first)
set -u
[ $# -eq 0 ] || { echo "no options (ONLY='regex' picks which breaks to run)"; exit 2; }
REPO=$(cd "$(dirname "$0")/../.." && pwd); PARENT=$(dirname "$REPO")
FW=tofu-plugin-framework-cpp LOW=tofu-plugin-cpp
[ "$(basename "$REPO")" = "$FW" ] && [ -d "$PARENT/$LOW" ] || { echo "expected $FW and $LOW side by side in $PARENT"; exit 2; }
COPY=$(mktemp -d); trap 'rm -rf "$COPY"' EXIT
PASSED=0; FAILED=0
expect() { if [ "$2" = "$3" ]; then PASSED=$((PASSED+1)); echo "PASS  $1"; else FAILED=$((FAILED+1)); echo "FAIL  $1"; echo "        wanted: $3"; echo "        got:    $2"; fi; }
fingerprint() {
  for project in $FW $LOW; do
    ( cd "$PARENT/$project" && find . -path ./.git -prune -o -type f -printf '%p %s %T@\n' | sort | cksum
      find . -path ./.git -prune -o -type f -exec cksum {} + | sort | cksum )
  done
}
BEFORE=$(fingerprint)
PREFIX=${CMAKE_PREFIX_PATH:-$(sed -n 's/^CMAKE_PREFIX_PATH:[A-Z]*=//p' "$REPO/build/CMakeCache.txt" 2>/dev/null)}
PREFIX=${PREFIX:-$HOME/.local/grpc-openssl}
echo "== copy the projects to $COPY and build them (first build is the slow one)"
for project in $FW $LOW; do
  mkdir "$COPY/$project"
  tar -C "$PARENT/$project" --exclude=./build --exclude=./.git --exclude=./.cache -cf - . | tar -C "$COPY/$project" -xf -
  cp -r "$COPY/$project" "$COPY/pristine-$project"
done
cmake -S "$COPY/$FW" -B "$COPY/$FW/build" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$PREFIX" >"$COPY/cmake.log" 2>&1 \
  && make -C "$COPY/$FW/build" -j4 >"$COPY/make.log" 2>&1 || { echo "The copy did not build. See $COPY/cmake.log and make.log"; exit 2; }
UNIT=$COPY/$FW/build/unit-tests
colour() { "$UNIT" >/dev/null 2>&1 && echo green || echo red; }
expect "unbroken copy: the unit tests are green" "$(colour)" green

LAST=
broken() {  # broken <what> <file, relative to the folder holding the projects> <exact text, once> <replacement>
  local what=$1 file=$2 old=$3 new=$4
  if [ -n "${ONLY:-}" ] && ! [[ $what =~ $ONLY ]]; then return; fi
  if [ -n "$LAST" ]; then cp "$COPY/pristine-${LAST%%/*}/${LAST#*/}" "$COPY/$LAST"; fi
  LAST=$file
  python3 -c 'import sys; f,old,new=sys.argv[1:]; s=open(f).read(); assert s.count(old)==1; open(f,"w").write(s.replace(old,new))' \
    "$COPY/$file" "$old" "$new" 2>/dev/null || { expect "$what: break applied to the source" "no" yes; return; }
  expect "$what: broken copy builds" "$(make -C "$COPY/$FW/build" -j4 >"$COPY/make-break.log" 2>&1 && echo yes || echo no)" yes
  expect "$what: the unit tests go red" "$(colour)" red
}
FWS=$FW/src/fwserver.cpp STOCK=$FW/src/stock.cpp SCH=$FW/src/schema.cpp TY=$FW/include/tfframework/types.h TS=$FW/include/tfframework/tfsdk.h
echo "== now break the framework, one rule at a time: the unit tests MUST go red"
broken "defaults are not applied"                   $FWS 'if (attribute.default_value && config_of(name).IsNull()) attributes[name] = *attribute.default_value;' '(void)0;'
broken "a configured value is replaced by the default" $FWS 'if (attribute.default_value && config_of(name).IsNull())' 'if (attribute.default_value)'
broken "computed attributes are not marked unknown" $FWS 'attributes[name] = Value::Unknown();' '(void)0;'
broken "a default is also marked unknown"           $FWS 'if (attribute.computed && !attribute.default_value && config_of(name).IsNull())' 'if (attribute.computed && config_of(name).IsNull())'
broken "an unchanged plan is still marked unknown"  $FWS 'if (Value::Object(attributes) != *prior) {  // 2' 'if (true) {  // 2'
broken "a configured computed value is made unknown" $FWS 'if (attribute.computed && !attribute.default_value && config_of(name).IsNull())' 'if (attribute.computed && !attribute.default_value)'
broken "plan modifiers are not run"                 $FWS 'modify(call, result);' '(void)modify;'
broken "a modifier's plan value is dropped"         $FWS 'current = result.plan_value;' '(void)0;'
broken "RequiresReplace is not reported"            $FWS 'if (result.requires_replace) add_replace(path::Root(name).raw());' '(void)0;'
broken "ModifyPlan is not called"                   $FWS 'dynamic_cast<resource::ResourceWithModifyPlan*>(instance.get())' 'static_cast<resource::ResourceWithModifyPlan*>(nullptr)'
broken "ModifyPlan replace paths are dropped"       $FWS 'for (const path::Path& path : result.requires_replace) add_replace(path.raw());' '(void)0;'
broken "attribute validators are not run"           $FWS 'validate({path::Root(name), config.Attr(name), config}, response);' '(void)validate;'
broken "the resource ValidateConfig is not run"     $FWS 'dynamic_cast<resource::ResourceWithValidateConfig*>(instance.get())' 'static_cast<resource::ResourceWithValidateConfig*>(nullptr)'
broken "the provider ValidateConfig is not run"     $FWS 'dynamic_cast<provider::ProviderWithValidateConfig*>(provider_.get())' 'static_cast<provider::ProviderWithValidateConfig*>(nullptr)'
broken "resources are not configured"               $FWS 'dynamic_cast<resource::ResourceWithConfigure*>(instance.get())' 'static_cast<resource::ResourceWithConfigure*>(nullptr)'
broken "provider data is not kept"                  $FWS 'provider_data_ = std::move(response.resource_data);' '(void)0;'
broken "Create is not given a null state"           $FWS 'resource::CreateResponse result{tfsdk::State(&schema, Value::Null()), {}};' 'resource::CreateResponse result{tfsdk::State(&schema, *planned), {}};'
broken "a missing state after Create is accepted"   $FWS 'if (!result.diagnostics.HasError() && new_state.IsNull()) {' 'if (false) {'
broken "an update does not start from the plan"     $FWS 'resource::UpdateResponse result{tfsdk::State(&schema, *planned), {}};' 'resource::UpdateResponse result{tfsdk::State(&schema, *prior), {}};'
broken "a destroyed resource keeps its state"       $FWS 'new_state = result.diagnostics.HasError() ? result.state.Raw() : Value::Null();' 'new_state = result.state.Raw();'
broken "a failed delete loses the state"            $FWS 'new_state = result.diagnostics.HasError() ? result.state.Raw() : Value::Null();' 'new_state = Value::Null();'
broken "Read starts from nothing"                   $FWS 'resource::ReadResponse result{tfsdk::State(&entry->schema, *current), {}};' 'resource::ReadResponse result{tfsdk::State(&entry->schema, Value::Null()), {}};'
broken "private data is not passed through"         $FWS 'response.private_state = request.private_state;' '(void)0;'
broken "state of an older version is not upgraded"  $FWS 'found->second.upgrade(call, &result);' '(void)call;'
broken "an unknown version is accepted"             $FWS 'if (found == upgraders.end() || !found->second.upgrade) {' 'if (false) {'
broken "the sensitive flag is not sent"             $FWS 'a.optional, a.computed, a.sensitive}' 'a.optional, a.computed, false}'
broken "the schema version is not sent"             $FWS 'out.version = schema.version;' '(void)0;'
broken "schema mistakes are not reported"           $FWS 'registry_.diagnostics.Append(WithContext(resource_schema.schema.ValidateImplementation(), "resource " + name.type_name));' '(void)0;'
broken "duplicate resource types are accepted"      $FWS 'if (registry_.resources.count(name.type_name)) {' 'if (false) {'
broken "RequiresReplace ignores new resources"      $STOCK 'if (request.state.IsNull()) return;  // being created' '(void)0;'
broken "RequiresReplace replaces without a change"  $STOCK 'if (request.plan_value == request.state_value) return;' '(void)0;'
broken "UseStateForUnknown does nothing"            $STOCK 'response.plan_value = request.state_value;' '(void)0;'
broken "UseStateForUnknown overrides a configured value" $STOCK 'if (!request.config_value.IsNull()) return;          // the configuration decides (it may itself be unknown)' '(void)0;'
broken "string length is not checked"               $STOCK 'if (length < static_cast<size_t>(min) || length > static_cast<size_t>(max)) {' 'if (false) {'
broken "an int range is not checked"                $STOCK 'if (value.ValueInt64() < min || value.ValueInt64() > max) {' 'if (false) {'
broken "a float range is not checked"               $STOCK 'if (!(value.ValueFloat64() >= min && value.ValueFloat64() <= max)) {' 'if (false) {'
broken "a pattern is not checked"                   $STOCK 'if (!std::regex_match(value.ValueString(), pattern)) {' 'if (false) {'
broken "a validator is called for null values"      $FW/include/tfframework/validator.h 'if (value.IsNull() || value.IsUnknown()) return;' '(void)0;'
broken "attribute without a flag is accepted"       $SCH 'if (!a.required && !a.optional && !a.computed) error("must be required, optional or computed.");' '(void)0;'
broken "a default without computed is accepted"     $SCH 'if (a.default_value && !a.computed) error("has a default, so it must be computed.");' '(void)0;'
broken "null reads as unknown"                      $TY 'if (value.IsNull()) { *out = T::Null(); return absl::OkStatus(); }' 'if (value.IsNull()) { *out = T::Unknown(); return absl::OkStatus(); }'
broken "a fraction is read as an int64"             $TY 'if (!v.IsInteger()) return Mismatch("a whole number that fits an int64", v);' '(void)0;'
broken "a null state can not be written to"         $TS 'if (raw_.kind() != tftypes::Value::Kind::kObject) raw_ = NullObject();' '(void)0;'
broken "a model with missing fields is accepted"    $TS 'diagnostics->AddError("Model Mismatch", "The schema has an attribute' 'diagnostics->AddError("Ok", "The schema has an attribute'
echo "== finally: the real projects must be exactly as they were"
expect "your sources and build/ folders are untouched (content, size, modification times)" "$(fingerprint)" "$BEFORE"
echo; echo "passed $PASSED, failed $FAILED"; [ "$FAILED" = 0 ]
