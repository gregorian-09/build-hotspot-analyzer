#!/bin/bash

# Build Hotspot Analyzer - BHA Commands Testing Script
# Runs various BHA commands on existing trace files to validate analysis

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_ROOT="${TEST_ROOT:-${SCRIPT_DIR}/cli}"
BHA_BINARY="${BHA_BINARY:-${SCRIPT_DIR}/../build/bha}"

LOG_FILE="${TEST_ROOT}/bha_cmd_results.log"
# shellcheck disable=SC2034
SUMMARY_FILE="${TEST_ROOT}/bha_cmd_summary.txt"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
NC='\033[0m'

TOTAL_COMMANDS=0
SUCCESSFUL_COMMANDS=0
FAILED_COMMANDS=0
declare -A COMMAND_RESULTS

log() {
    local level="$1"
    shift
    echo -e "[$level] $*" | tee -a "$LOG_FILE"
}

log_detail() {
    echo -e "[DETAIL] $*" | tee -a "$LOG_FILE"
}

log_command() {
    echo -e "\n${MAGENTA}━━━ $* ━━━${NC}" | tee -a "$LOG_FILE"
}

log_subsection() {
    echo -e "\n${BLUE}╔════════════════════════════════════════════════════════════════╗${NC}" | tee -a "$LOG_FILE"
    echo -e "${BLUE}║${NC} ${CYAN}$*${NC}" | tee -a "$LOG_FILE"
    echo -e "${BLUE}╚════════════════════════════════════════════════════════════════╝${NC}\n" | tee -a "$LOG_FILE"
}

run_bha_cmd() {
    local cmd_name="$1"
    local cmd_desc="$2"
    shift 2
    local full_cmd="$*"

    TOTAL_COMMANDS=$((TOTAL_COMMANDS + 1))
    log_command "$cmd_desc"
    log_detail "Command: ${CYAN}$full_cmd${NC}"

    # shellcheck disable=SC2155
    local start_time=$(date +%s.%N)

    if eval "$full_cmd" > /dev/null 2>&1; then
        # shellcheck disable=SC2155
        local end_time=$(date +%s.%N)
        # shellcheck disable=SC2155
        local duration=$(echo "$end_time - $start_time" | bc 2>/dev/null || echo "?")
        log_detail "${GREEN}✓ Success${NC} (${duration}s)"
        SUCCESSFUL_COMMANDS=$((SUCCESSFUL_COMMANDS + 1))
        COMMAND_RESULTS["$cmd_name"]="${COMMAND_RESULTS[$cmd_name]:-}✓"
        return 0
    else
        # shellcheck disable=SC2155
        local end_time=$(date +%s.%N)
        # shellcheck disable=SC2155
        local duration=$(echo "$end_time - $start_time" | bc 2>/dev/null || echo "?")
        log_detail "${RED}✗ Failed${NC} (${duration}s)"
        FAILED_COMMANDS=$((FAILED_COMMANDS + 1))
        COMMAND_RESULTS["$cmd_name"]="${COMMAND_RESULTS[$cmd_name]:-}✗"
        return 1
    fi
}

run_bha_commands() {
    local trace_dir="$1"
    local output_base="$2"
    local project_name="$3"
    local compiler="$4"

    rm -rf "$output_base/analyze" "$output_base/suggest" "$output_base/export" "$output_base/report"

    mkdir -p "$output_base/analyze" "$output_base/suggest" "$output_base/export"
    mkdir -p "$output_base/report"

    local trace_files
    trace_files=$(find "$trace_dir" -type f \( -name "*.json" -o -name "*.su" -o -name "*.bha.txt" \) 2>/dev/null)
    local trace_count
    trace_count=$(echo "$trace_files" | grep -c . || echo 0)
    if [ "$trace_count" -eq 0 ]; then
        log "WARN" "No trace or memory files found in $trace_dir"
        return 1
    fi
    log "INFO" "Found ${GREEN}$trace_count${NC} trace/memory files for analysis"

    log_subsection "Command: ANALYZE"
    run_bha_cmd "analyze" "bha analyze (text output)" "\"$BHA_BINARY\" analyze \"$trace_dir\" > \"$output_base/analyze/analysis.txt\"" || true
    run_bha_cmd "analyze" "bha analyze --format json" "\"$BHA_BINARY\" analyze \"$trace_dir\" --format json > \"$output_base/analyze/analysis.json\"" || true

    log_subsection "Command: SUGGEST"
    run_bha_cmd "suggest" "bha suggest (basic)" "\"$BHA_BINARY\" suggest \"$trace_dir\" > \"$output_base/suggest/suggestions.txt\"" || true
    run_bha_cmd "suggest" "bha suggest --format json" "\"$BHA_BINARY\" suggest \"$trace_dir\" --format json > \"$output_base/suggest/suggestions.json\"" || true

    log_subsection "Command: EXPORT"
    run_bha_cmd "export" "bha export --format json" "\"$BHA_BINARY\" export \"$trace_dir\" --format json --pretty --include-suggestions -o \"$output_base/export/report.json\"" || true
    run_bha_cmd "export" "bha export --format html" "\"$BHA_BINARY\" export \"$trace_dir\" --format html --include-suggestions --title 'BHA Report: $project_name ($compiler)' -o \"$output_base/export/report.html\"" || true

    local total_files
    total_files=$(find "$output_base" -type f 2>/dev/null | wc -l)
    log "INFO" "Generated: ${GREEN}$total_files${NC} files"

    return 0
}

main() {
    # shellcheck disable=SC2188
    > "$LOG_FILE"

    echo -e "\n${CYAN}╔════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║      BHA Commands Testing (Analyze, Suggest, Export)          ║${NC}"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════════╝${NC}\n"

    echo -e "${YELLOW}This script runs BHA commands on existing trace files.${NC}"
    echo -e "${YELLOW}Ensure you have run ./build_repos.sh first to generate traces.${NC}\n"

    if [ ! -f "$BHA_BINARY" ]; then
        echo -e "${RED}Error: BHA binary not found at $BHA_BINARY${NC}"
        echo -e "${RED}Please build BHA first or set BHA_BINARY environment variable.${NC}"
        exit 1
    fi

    echo -e "${GREEN}BHA Binary found: $BHA_BINARY${NC}"
    echo -e "${YELLOW}Scanning for trace files in: $TEST_ROOT${NC}\n"

    local total=0
    local success=0
    local failed=0

    for build_system_dir in "$TEST_ROOT"/{cmake,make,meson}; do
        [ ! -d "$build_system_dir" ] && continue
        # shellcheck disable=SC2155
        local build_system=$(basename "$build_system_dir")

        for compiler_dir in "$build_system_dir"/*; do
            [ ! -d "$compiler_dir" ] && continue
            # shellcheck disable=SC2155
            local compiler=$(basename "$compiler_dir")

            for project_dir in "$compiler_dir"/*; do
                [ ! -d "$project_dir" ] && continue
                # shellcheck disable=SC2155
                local project=$(basename "$project_dir")
                local trace_dir="$project_dir/traces"
                local output_dir="$project_dir/output"

                if [ ! -d "$trace_dir" ]; then
                    continue
                fi

                # shellcheck disable=SC2155
                local trace_count=$(find "$trace_dir" -type f \( -name "*.json" -o -name "*.su" -o -name "*.bha.txt" \) 2>/dev/null | wc -l)
                [ "$trace_count" -eq 0 ] && continue

                echo -e "\n${YELLOW}═══════════════════════════════════════════════════════════${NC}"
                echo -e "${YELLOW}Project: ${GREEN}$project${NC} | Compiler: ${CYAN}$compiler${NC} | System: ${BLUE}$build_system${NC}"
                echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}\n"

                TOTAL_COMMANDS=0
                SUCCESSFUL_COMMANDS=0
                FAILED_COMMANDS=0
                COMMAND_RESULTS=()

                if run_bha_commands "$trace_dir" "$output_dir" "$project" "$compiler"; then
                    success=$((success + 1))
                    echo -e "\n${GREEN}✓ Analysis complete${NC}"
                else
                    failed=$((failed + 1))
                    echo -e "\n${RED}✗ Analysis had issues${NC}"
                fi

                echo -e "${CYAN}Commands: $TOTAL_COMMANDS | Success: ${GREEN}$SUCCESSFUL_COMMANDS${NC} | Failed: ${RED}$FAILED_COMMANDS${NC}"
                total=$((total + 1))
            done
        done
    done

    echo -e "\n${CYAN}╔════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║                        Summary                                 ║${NC}"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════════╝${NC}\n"
    echo -e "Total projects analyzed: ${YELLOW}$total${NC}"
    echo -e "Successful: ${GREEN}$success${NC}"
    echo -e "Failed: ${RED}$failed${NC}"
    echo -e "\nLog: ${CYAN}$LOG_FILE${NC}\n"
}

main "$@"