#!/usr/bin/env bash
# Phase H — Master acceptance script for opencode-meta-qt.
#
# Runs the full Phase H acceptance gate end to end:
#   1. Configure + build the project (cmake + parallel build).
#   2. Run the precise ctest smoke set that locks the apply-path /
#      storage / contract / cross-view guarantees.
#   3. Seed a starter Team fixture (mirroring StorageManager::
#      seedDefaultsIfNeeded) and emit its rendered opencode.json
#      (v1 + v2 sidecar, per Phase G5).
#   4. Apply the v2 sidecar parity workaround documented in
#      tests/test_cross_view_smoke.cpp — strip the v2 camelCase
#      sibling keys into a v1-only mirror — and gate the run on
#      `opencode debug config` exiting 0 with empty stderr
#      against the mirror. The full v1+v2 emission is checked
#      separately as informational only (the installed opencode
#      1.17.x accepts the v1 half but rejects the v2 sibling;
#      see ROADMAP.md Phase G5 caveat — this script must flip
#      to a single full-file run once opencode ships dual-shape
#      parity).
#
# Exit codes:
#   0   → every stage passed.
#   1   → configuration or build error.
#   2   → ctest failure (one or more of the 6 required tests).
#   3   → opencode debug config failed against the v1-only mirror
#        (the Phase G5 workaround) — every emission must
#        round-trip through this gate.
#   4   → required runtime tool missing (cmake, ctest, or
#        opencode on PATH).
#
# Usage:
#   scripts/acceptance-phase-h.sh           # run with default logger
#   LOG_FILE=/tmp/h.log scripts/...        # also tee to a file
#
# The script is location-aware: it derives REPO_ROOT from its own
# filesystem location (resolves symlinks) so it works whether called
# from the project root, the scripts/ subdirectory, or anywhere
# else on the box.

set -Eeuo pipefail
shopt -s inherit_errexit 2>/dev/null || true
IFS=$'\n\t'

# --- Script self-location ----------------------------------------------------
SCRIPT_PATH="${BASH_SOURCE[0]}"
while [ -L "$SCRIPT_PATH" ]; do
  SCRIPT_DIR=$(cd -P "$(dirname "$SCRIPT_PATH")" && pwd)
  SCRIPT_PATH=$(readlink "$SCRIPT_PATH")
  [[ "$SCRIPT_PATH" != /* ]] && SCRIPT_PATH="$SCRIPT_DIR/$SCRIPT_PATH"
done
SCRIPT_DIR=$(cd -P "$(dirname "$SCRIPT_PATH")" && pwd)
REPO_ROOT=$(cd -P "$SCRIPT_DIR/.." && pwd)

# --- Tunables (override from the environment) -------------------------------
BUILD_DIR="${BUILD_DIR:-build}"                # legacy project, no presets
PARALLEL_JOBS="${PARALLEL_JOBS:-$(nproc 2>/dev/null || echo 2)}"
LOG_FILE="${LOG_FILE:-}"
OPENCODE_BIN="${OPENCODE_BIN:-opencode}"
# Timeout in seconds. coreutils `timeout` does not accept fractions /
# `ms` suffix across all versions, so we use whole seconds.
OPENCODE_TIMEOUT_SECONDS="${OPENCODE_TIMEOUT_SECONDS:-30}"
CMAKE_GENERATOR="${CMAKE_GENERATOR:-}"

# The 6 tests that lock the Phase H bar (order is irrelevant; the regex
# is a single anchored alternation).
SMOKE_TESTS_REGEX='(test_team_renderer|test_apply_team|test_starter_team_apply|test_teams_storage|test_contract_checker|test_cross_view_smoke)'

# Runtime fixtures — mirrored from StorageManager::seedDefaultsIfNeeded
# (see src/storage/StorageManager.cpp:741). Values are the literal
# defaults the seeder writes; keeping them inline here means the
# acceptance script has no dependency on the binary being run from a
# specific HOME or storage state.
STARTER_MODEL_ID='anthropic/claude-sonnet-4-6'
STARTER_TEAM_NAME='Starter Team'
SCHEMA_URL='https://opencode.ai/config.json'

# v2 sidecar parity workaround keys (mirrors test_cross_view_smoke.cpp:387-400).
V2_TOP_LEVEL_KEYS='agents|permissions|providers|snapshots|smallModel|attachments'
V2_AGENT_FIELD_KEYS='system|disabled|request|permissions'

# --- Pretty logging ---------------------------------------------------------
if [ -t 1 ] && [ -z "$LOG_FILE" ]; then
  C_RESET=$'\033[0m'
  C_BOLD=$'\033[1m'
  C_RED=$'\033[31m'
  C_GREEN=$'\033[32m'
  C_YELLOW=$'\033[33m'
  C_BLUE=$'\033[34m'
  C_DIM=$'\033[2m'
else
  C_RESET=''; C_BOLD=''; C_RED=''; C_GREEN=''; C_YELLOW=''; C_BLUE=''; C_DIM=''
fi

log()    { printf '%b[phase-h]%b %s\n' "$C_BLUE" "$C_RESET" "$*"; }
ok()     { printf '%b[  ok  ]%b %s\n' "$C_GREEN" "$C_RESET" "$*"; }
warn()   { printf '%b[ warn ]%b %s\n' "$C_YELLOW" "$C_RESET" "$*"; }
fail()   { printf '%b[ fail ]%b %s\n' "$C_RED"   "$C_RESET" "$*"; }
hdr()    {
  printf '\n%b== %s ==%b\n' "$C_BOLD" "$*" "$C_RESET"
}

# Accumulated failures; tracked separately so internal helpers can
# register without short-circuiting the whole stage.
declare -i FAILED=0
declare -a FAILED_STAGES=()

stage_failed() {
  FAILED+=1
  FAILED_STAGES+=("$1")
}

# --- Logging sink (tee when LOG_FILE is set) --------------------------------
if [ -n "$LOG_FILE" ]; then
  mkdir -p "$(dirname "$LOG_FILE")"
  exec > >(tee -a "$LOG_FILE") 2>&1
fi

# ERR trap — fires on any failed command. We DISABLE it during the
# gate blocks where we expect non-zero exits (the opencode debug
# config runs), then re-enable it. This is more reliable than
# `set +e ... set -e` because the trap fires unconditionally under
# `set -E` even when -e is disabled.
on_error() {
  local rc=$?
  local line=${BASH_LINENO[0]}
  fail "Aborting at line ${line} with exit ${rc}"
  exit "$rc"
}
trap on_error ERR

# Helper for code paths that MUST tolerate non-zero exits (e.g. the
# opencode debug config informational runs). Stores the previous
# trap so we can restore it cleanly.
with_trap_suppressed() {
  local prev
  prev=$(trap -p ERR || true)
  trap - ERR
  "$@"
  local rc=$?
  eval "${prev:-trap - ERR}"
  return $rc
}

# --- Toolchain preflight ----------------------------------------------------
hdr "preflight"
TOOLS_OK=1
for tool in cmake ctest "$OPENCODE_BIN" jq; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    fail "required tool not found on PATH: ${tool}"
    TOOLS_OK=0
  else
    ok "${tool}: $(command -v "$tool")"
  fi
done

if [ "$TOOLS_OK" -eq 0 ]; then
  fail "preflight failed — install the missing tools above and re-run"
  exit 4
fi

# Make sure the build directory is in scope from here on.
cd "$REPO_ROOT"
log "repo root : $REPO_ROOT"
log "build dir : $BUILD_DIR  (legacy project, no CMakePresets.json)"
# Show resolved path so callers can see exactly where the build went.
log "resolved  : $(cd "$REPO_ROOT" && cd "$BUILD_DIR" 2>/dev/null && pwd || echo "<not yet created>")"

# --- Stage 1: configure + build ---------------------------------------------
hdr "stage 1 — configure + build"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] \
   || [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
  log "no CMakeCache.txt / compile_commands.json — running cmake configure"
  CMAKE_ARGS=(-S "$REPO_ROOT" -B "$BUILD_DIR"
              -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
  if [ -n "$CMAKE_GENERATOR" ]; then
    CMAKE_ARGS+=(-G "$CMAKE_GENERATOR")
  fi
  cmake "${CMAKE_ARGS[@]}"
else
  log "CMakeCache.txt present — skipping fresh configure (will rebuild)"
fi

log "building (parallel jobs: ${PARALLEL_JOBS})"
if ! cmake --build "$BUILD_DIR" --parallel "$PARALLEL_JOBS"; then
  fail "cmake --build returned non-zero"
  exit 1
fi
ok "build complete"

# Sanity: the symlink the editor expects. Cheap to repair if missing.
if [ ! -e "$REPO_ROOT/compile_commands.json" ] \
   || [ "$(readlink "$REPO_ROOT/compile_commands.json" 2>/dev/null || true)" \
        != "$BUILD_DIR/compile_commands.json" ]; then
  warn "compile_commands.json symlink missing or wrong target — repairing"
  ln -sfn "$BUILD_DIR/compile_commands.json" "$REPO_ROOT/compile_commands.json"
fi

# --- Stage 2: ctest smoke set ----------------------------------------------
hdr "stage 2 — ctest smoke set"

if ! ctest --test-dir "$BUILD_DIR" \
           --output-on-failure -R "$SMOKE_TESTS_REGEX"; then
  fail "ctest failed for one or more of: ${SMOKE_TESTS_REGEX}"
  stage_failed "ctest"
else
  ok "all 6 smoke tests passed"
fi

# Belt-and-braces: enumerate which tests actually ran and confirm every
# required name appeared in ctest's test list. Defensive against silent
# regex typos or future test renames.
CTEST_LIST=$(ctest --test-dir "$BUILD_DIR" -N -R "$SMOKE_TESTS_REGEX" \
              | awk '/^ +Test #/ {print $3}' | sort -u)
REQUIRED_TESTS=(test_team_renderer test_apply_team test_starter_team_apply \
                test_teams_storage test_contract_checker test_cross_view_smoke)
MISSING=()
for t in "${REQUIRED_TESTS[@]}"; do
  if ! grep -qx "$t" <<<"$CTEST_LIST"; then
    MISSING+=("$t")
  fi
done
if [ "${#MISSING[@]}" -ne 0 ]; then
  fail "the following required tests are NOT registered with ctest: ${MISSING[*]}"
  stage_failed "ctest-coverage"
else
  ok "ctest registry covers all 6 required tests"
fi

# --- Stage 3: opencode debug config against a starter-team fixture ----------
hdr "stage 3 — opencode debug config on a fixture Team"

# 3a. Stage two working tempdirs — one for the GATE (v1-only
# mirror), one for the informational full v1+v2 run.
# `opencode debug config` reads `opencode.json` from CWD with no
# --config flag, so each run needs its own isolated tempdir carrying
# exactly one `opencode.json`. Everything cleans up via the EXIT
# trap.
TMPDIR_V1=$(mktemp -d -t phase-h-v1.XXXXXX)
TMPDIR_V12=$(mktemp -d -t phase-h-v12.XXXXXX)
cleanup_tempdirs() { rm -rf "$TMPDIR_V1" "$TMPDIR_V12"; }
trap cleanup_tempdirs EXIT

# 3b. Synthesize the v1+v2 fixture exactly as TeamRenderer::render
# would produce it for StorageManager::seedDefaultsIfNeeded's
# "starter-team" — two Specialist bindings (build + plan), both
# bound to anthropic/claude-sonnet-4-6, both primary.
LOG_FILE_V12="$TMPDIR_V12/opencode.json"

jq -n \
  --arg schema "$SCHEMA_URL" \
  --arg model "$STARTER_MODEL_ID" \
  --arg team_name "$STARTER_TEAM_NAME" \
  '
  def agent(role; mode):
    {
      model: $model,
      prompt: ("seeded " + $team_name + " agent \u2014 role=" + role),
      description: ("Seeded " + role + " role"),
      mode: mode,
      system: ("seeded " + $team_name + " agent \u2014 role=" + role)
    };

  {
    "$schema": $schema,
    "default_agent": "build",
    "agent": {
      "build": agent("build"; "primary"),
      "plan":  agent("plan";  "subagent")
    },
    "agents": {
      "build": agent("build"; "primary"),
      "plan":  agent("plan";  "subagent")
    }
  }
  ' > "$LOG_FILE_V12"

# 3c. Apply the v2 sidecar parity workaround (mirrors
# tests/test_cross_view_smoke.cpp:387-424 lines that strip the v2
# camelCase siblings + per-agent v2 fields into a v1-only mirror).
# Installed opencode 1.17.x still rejects `Unrecognized key: agents`
# against the v1 shape, so the gate uses the mirror file.
LOG_FILE_V1="$TMPDIR_V1/opencode.json"
jq '
  def strip_v2_agent_fields:
    del(.system, .disabled, .request, .permissions);

  (
    [ "agents", "permissions", "providers",
      "snapshots", "smallModel", "attachments" ] as $drop
    | with_entries(select(.key as $k | ($drop | index($k)) | not))
  )
  | if has("agent") then
      .agent = (.agent | with_entries(.value |= strip_v2_agent_fields))
    else . end
' "$LOG_FILE_V12" > "$LOG_FILE_V1"

log "v1-only mirror written  : $LOG_FILE_V1"
log "v1+v2 fixture written   : $LOG_FILE_V12"

# 3d. GATE — opencode debug config against the v1-only mirror.
# This is the binding contract per ROADMAP.md §12.3 /
# OPENCODE-CONFIG-INTROSPECTION.md. We capture stdout/stderr/exit
# in a single run; the previous `set -E` + ERR trap combo forced
# us to suppress the ERR trap around this command (the trap fires
# unconditionally when the inner subshell returns non-zero, and
# that's exactly when we DON'T want to abort the gate).
log "running: ${OPENCODE_BIN} debug config  (cwd=$TMPDIR_V1, file=$LOG_FILE_V1)"
ERR_V1=$(mktemp)
OUT_V1=$(mktemp)
with_trap_suppressed bash -c '
  cd "$1" && timeout "${2}s" "$3" debug config
  echo "EXIT=$?"
' -- "$TMPDIR_V1" "$OPENCODE_TIMEOUT_SECONDS" "$OPENCODE_BIN" \
      >"$OUT_V1" 2>"$ERR_V1"
RC_V1=$(grep -E '^EXIT=' "$OUT_V1" 2>/dev/null | tail -1 | sed 's/^EXIT=//') || RC_V1=1
RC_V1=${RC_V1:-1}

if [ "$RC_V1" -eq 124 ]; then
  fail "opencode debug config timed out after ${OPENCODE_TIMEOUT_SECONDS}s"
  stage_failed "debug-config-v1"
elif [ "$RC_V1" -ne 0 ] || [ -s "$ERR_V1" ]; then
  fail "opencode debug config REJECTED the v1-only mirror (rc=$RC_V1)"
  sed 's/^/    stdout: /' "$OUT_V1" 2>/dev/null | head -20 || true
  sed 's/^/    stderr: /' "$ERR_V1" 2>/dev/null | head -20 || true
  stage_failed "debug-config-v1"
else
  ok "opencode debug config ACCEPTED the v1-only mirror (rc=0, empty stderr)"
fi
rm -f "$ERR_V1" "$OUT_V1"

# 3e. Informational — also exercise the FULL v1+v2 emission so the
# next agent sees exactly the failure mode the G5 caveat describes.
# Failures here DO NOT fail the overall run; they are logged for the
# ROADMAP.md decision log.
log "running (informational): ${OPENCODE_BIN} debug config on full v1+v2 file"
ERR_V12=$(mktemp)
OUT_V12=$(mktemp)
with_trap_suppressed bash -c '
  cd "$1" && timeout "${2}s" "$3" debug config
' -- "$TMPDIR_V12" "$OPENCODE_TIMEOUT_SECONDS" "$OPENCODE_BIN" \
      >"$OUT_V12" 2>"$ERR_V12" || true

if [ ! -s "$ERR_V12" ]; then
  warn "opencode now accepts the full v1+v2 emission — recheck the G5 caveat"
  warn "and the Phase H script can drop the v2-stripping step."
else
  warn "full v1+v2 emission was rejected — expected per"
  warn "Phase G5 caveat (installed opencode 1.17.x lacks v2 sidecar"
  warn "support). The v1-only mirror above is the gate; this is logged"
  warn "only for visibility."
  printf '    stderr: %s\n' "$(head -c 400 "$ERR_V12")"
fi
rm -f "$ERR_V12" "$OUT_V12"

# --- Final report -----------------------------------------------------------
hdr "summary"

if [ "$FAILED" -eq 0 ]; then
  ok "Phase H acceptance PASSED"
  printf '%b== Final verdict: PASS ==%b\n' "$C_GREEN$C_BOLD" "$C_RESET"
  exit 0
fi

printf '%b== Final verdict: FAIL ==%b\n' "$C_RED$C_BOLD" "$C_RESET"
printf 'failed stages:\n'
for s in "${FAILED_STAGES[@]}"; do
  printf '  - %s\n' "$s"
done

# Order of exit codes mirrors the per-stage outcomes we logged above.
if printf '%s\n' "${FAILED_STAGES[@]}" | grep -qx 'debug-config-v1'; then
  exit 3
fi
if printf '%s\n' "${FAILED_STAGES[@]}" | grep -qE '^(ctest|ctest-coverage)$'; then
  exit 2
fi
exit 1
