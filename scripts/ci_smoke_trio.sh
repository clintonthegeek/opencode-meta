#!/usr/bin/env bash
# scripts/ci_smoke_trio.sh
#
# Phase C0-4 / D4-4 — Pin the apply-path smoke trio + stock-fidelity
# test as a runnable, CI-friendly script. Locks the regression bar
# across all future changes:
#   test_team_renderer
#   test_apply_team
#   test_starter_team_apply
#   test_contract_checker        (Phase G3 unit test, slotted in for
#                                  compliance work per ROADMAP.md §2)
#   test_seed_stock_fidelity      (Phase D4-4 — explicit reversal of
#                                  the D1-6 "regex update lands after
#                                  D1 exit gate" hold. Now unified
#                                  into the canonical regression bar
#                                  so every CI run asserts the
#                                  stock-aligned seed shape.)
#   test_legacy_storage_unaffected_by_seed
#                                (Phase D4-4 — migration safety rail.)
#   test_seed_opt_out_path        (Phase D4-4 — v0-fiction escape
#                                  hatch.)
#
# 8 tests total. Per Phase D4-4 the hash-update was made in the same
# commit as the rest of Phase D4 so CI flips in lock-step with the
# migration contract, not via a separate C5-1 / D-8-style flag flip.
#
# Usage:
#   scripts/ci_smoke_trio.sh                          # default: build/, no log
#   LOG_FILE=/tmp/smoke.log scripts/ci_smoke_trio.sh  # also tee output
#   BUILD_DIR=build-dev scripts/ci_smoke_trio.sh      # preset build dir
#
# Exit codes:
#   0   → every smoke test passed.
#   1   → ctest reported a non-zero exit (one or more tests failed).
#   2   → toolchain preflight failed (cmake / ctest missing).
#   3   → the smoke regex matched fewer than the 8 expected targets
#         (defensive against silent test renames / regex typos).
#
# The script is location-aware: it resolves REPO_ROOT from its own
# filesystem location so it works whether called from the project
# root, the scripts/ subdirectory, or anywhere else on the box.

set -Eeuo pipefail
shopt -s inherit_errexit 2>/dev/null || true
IFS=$'\n\t'

# --- Script self-location ---------------------------------------------------
SCRIPT_PATH="${BASH_SOURCE[0]}"
while [ -L "$SCRIPT_PATH" ]; do
  SCRIPT_DIR=$(cd -P "$(dirname "$SCRIPT_PATH")" && pwd)
  SCRIPT_PATH=$(readlink "$SCRIPT_PATH")
  [[ "$SCRIPT_PATH" != /* ]] && SCRIPT_PATH="$SCRIPT_DIR/$SCRIPT_PATH"
done
SCRIPT_DIR=$(cd -P "$(dirname "$SCRIPT_PATH")" && pwd)
REPO_ROOT=$(cd -P "$SCRIPT_DIR/.." && pwd)

# --- Tunables (override from the environment) ------------------------------
BUILD_DIR="${BUILD_DIR:-build}"
LOG_FILE="${LOG_FILE:-}"

# Phase D4-4 / C0-4 — D4-4 added test_seed_stock_fidelity +
# test_legacy_storage_unaffected_by_seed + test_seed_opt_out_path to
# the canonical regression bar, and D-8 reverses the deferred
# `test_agent_markdown` hold so CI now exercises the Agent Markdown
# feature too. 8 tests total. The regex is anchored alternation;
# ctest -R enumerates this exact set.
SMOKE_TESTS_REGEX='^(test_team_renderer|test_apply_team|test_starter_team_apply|test_contract_checker|test_seed_stock_fidelity|test_legacy_storage_unaffected_by_seed|test_seed_opt_out_path|test_agent_markdown)$'
REQUIRED_TESTS=(test_team_renderer test_apply_team
                test_starter_team_apply test_contract_checker
                test_seed_stock_fidelity
                test_legacy_storage_unaffected_by_seed
                test_seed_opt_out_path
                test_agent_markdown)

# --- Pretty logging (only when stdout is a TTY and no log file is set) -----
if [ -t 1 ] && [ -z "$LOG_FILE" ]; then
  C_RESET=$'\033[0m'; C_BOLD=$'\033[1m'
  C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_BLUE=$'\033[34m'
else
  C_RESET=''; C_BOLD=''; C_RED=''; C_GREEN=''; C_BLUE=''
fi

log()    { printf '%b[smoke ]%b %s\n' "$C_BLUE"  "$C_RESET" "$*"; }
ok()     { printf '%b[  ok  ]%b %s\n' "$C_GREEN" "$C_RESET" "$*"; }
fail()   { printf '%b[ fail ]%b %s\n' "$C_RED"   "$C_RESET" "$*"; }
hdr()    { printf '\n%b== %s ==%b\n' "$C_BOLD" "$*" "$C_RESET"; }

# --- Logging sink (tee when LOG_FILE is set) --------------------------------
if [ -n "$LOG_FILE" ]; then
  mkdir -p "$(dirname "$LOG_FILE")"
  exec > >(tee -a "$LOG_FILE") 2>&1
fi

# --- Toolchain preflight ----------------------------------------------------
hdr "preflight"
TOOLS_OK=1
for tool in cmake ctest; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    fail "required tool not found on PATH: ${tool}"
    TOOLS_OK=0
  else
    ok "${tool}: $(command -v "$tool")"
  fi
done
if [ "$TOOLS_OK" -eq 0 ]; then
  fail "preflight failed — install the missing tools above and re-run"
  exit 2
fi

# --- Smoke run --------------------------------------------------------------
cd "$REPO_ROOT"
log "repo root : $REPO_ROOT"
log "build dir : $BUILD_DIR"
log "regex     : ${SMOKE_TESTS_REGEX}"

hdr "ctest smoke trio"
if ! ctest --test-dir "$BUILD_DIR" \
           --output-on-failure \
           -R "$SMOKE_TESTS_REGEX"; then
  fail "ctest returned non-zero for one or more smoke tests"
  exit 1
fi
ok "all 8 smoke tests passed"

# --- Belt-and-braces: enumerate which tests ctest actually ran --------------
# Defensive against silent regex typos or future test renames that would
# otherwise shrink the bar without anyone noticing.
#
# The awk pattern is anchored on `^ +Test +#` (one-or-more spaces between
# "Test" and "#") because the `ctest -N` formatter pads with TWO spaces
# for single-digit test numbers ("Test  #2: ...") and ONE space for
# double-digit numbers ("Test #11: ..."). A literal `Test #` pattern only
# matches the latter and silently misses the former — covered during
# C1-3 / D-1 cleanup after the test count crossed the 10-test mark.
hdr "coverage check"
CTEST_LIST=$(ctest --test-dir "$BUILD_DIR" -N -R "$SMOKE_TESTS_REGEX" \
              | awk '/^ +Test +#/ {print $3}' | sort -u)
MISSING=()
for t in "${REQUIRED_TESTS[@]}"; do
  if ! grep -qx "$t" <<<"$CTEST_LIST"; then
    MISSING+=("$t")
  fi
done
if [ "${#MISSING[@]}" -ne 0 ]; then
  fail "ctest registry is missing required tests: ${MISSING[*]}"
  exit 3
fi
ok "ctest registry covers all 8 required tests"

hdr "summary"
printf '%b== Final verdict: PASS ==%b\n' "$C_GREEN$C_BOLD" "$C_RESET"
exit 0
