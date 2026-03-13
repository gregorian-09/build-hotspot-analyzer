#!/usr/bin/env python3

import argparse
import json
import re
import statistics
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple


REPO_ROOT = Path(__file__).resolve().parents[1]
LSP_TESTS_DIR = REPO_ROOT / "lsp" / "tests"
if str(LSP_TESTS_DIR) not in sys.path:
    sys.path.insert(0, str(LSP_TESTS_DIR))

from lsp_test_client import (  # type: ignore[import-not-found]
    LSPClient,
    PROJECT_ANALYSIS_TIMEOUTS,
    PROJECT_CMAKE_SUBDIR,
    build_command,
    default_server_path,
    ensure_compile_commands,
    ensure_trace_link,
)


DEFAULT_EXCLUDED_PROJECTS: Set[str] = set()


@dataclass(frozen=True)
class ProjectCase:
    project: str
    build_system: str
    compiler: str
    matrix_project_dir: Path
    repo_dir: Path
    build_dir: Path
    trace_dir: Path
    project_root: Path


def format_duration(ms: Optional[int]) -> str:
    if ms is None:
        return "n/a"
    if abs(ms) >= 1000:
        return f"{ms / 1000.0:.2f}s"
    return f"{ms}ms"


def to_int(value: Any) -> Optional[int]:
    if value is None:
        return None
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, (int, float)):
        return int(value)
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def project_key(case: ProjectCase) -> str:
    return f"{case.build_system}/{case.compiler}/{case.project}"


def safe_name(value: str) -> str:
    return "".join(ch if (ch.isalnum() or ch in {"-", "_", "."}) else "_" for ch in value)


def run_git_command(args: List[str]) -> str:
    try:
        proc = subprocess.run(
            args,
            cwd=REPO_ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            return ""
        return proc.stdout.strip()
    except OSError:
        return ""


def execute_command_with_timeout(
    client: LSPClient,
    command: str,
    arguments: Optional[List[Any]],
    timeout_seconds: int,
) -> Tuple[Optional[Dict[str, Any]], bool]:
    holder: Dict[str, Optional[Dict[str, Any]]] = {"response": None}

    def worker() -> None:
        holder["response"] = client.execute_command(command, arguments or [])

    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    thread.join(timeout_seconds)
    if thread.is_alive():
        if client.process:
            try:
                client.process.terminate()
            except Exception:
                pass
        return None, True
    return holder["response"], False


def discover_cases(
    tests_root: Path,
    compiler_filter: str,
    build_system_filter: str,
    selected_projects: Optional[Set[str]],
) -> List[ProjectCase]:
    cases: List[ProjectCase] = []
    for build_system_dir in sorted(tests_root.iterdir()):
        if not build_system_dir.is_dir():
            continue
        build_system = build_system_dir.name
        if build_system_filter != "all" and build_system != build_system_filter:
            continue

        for compiler_dir in sorted(build_system_dir.iterdir()):
            if not compiler_dir.is_dir():
                continue
            compiler = compiler_dir.name
            if compiler_filter != "all" and compiler != compiler_filter:
                continue

            for matrix_project_dir in sorted(compiler_dir.iterdir()):
                if not matrix_project_dir.is_dir():
                    continue
                project = matrix_project_dir.name
                if selected_projects and project not in selected_projects:
                    continue

                build_dir = matrix_project_dir / "build"
                trace_dir = matrix_project_dir / "traces"
                repo_dir = tests_root / "repos" / project
                source_subdir = PROJECT_CMAKE_SUBDIR.get(project, "")
                project_root = (repo_dir / source_subdir).resolve()

                cases.append(ProjectCase(
                    project=project,
                    build_system=build_system,
                    compiler=compiler,
                    matrix_project_dir=matrix_project_dir.resolve(),
                    repo_dir=repo_dir.resolve(),
                    build_dir=build_dir.resolve(),
                    trace_dir=trace_dir.resolve(),
                    project_root=project_root,
                ))
    return cases


class BenchmarkRunner:
    def __init__(self, args: argparse.Namespace, run_dir: Path):
        self.args = args
        self.run_dir = run_dir
        self.logs_dir = run_dir / "logs"
        self.suggestion_edits_dir = run_dir / "suggestion_edits"
        self.logs_dir.mkdir(parents=True, exist_ok=True)
        self.suggestion_edits_dir.mkdir(parents=True, exist_ok=True)
        self.records: List[Dict[str, Any]] = []
        self.generated_suggestion_files: Dict[str, str] = {}
        self.discover_applied_ids: Dict[str, Set[str]] = {}
        self.discover_status: Dict[str, str] = {}

    def _base_record(self, case: ProjectCase, mode: str) -> Dict[str, Any]:
        return {
            "mode": mode,
            "project": case.project,
            "buildSystem": case.build_system,
            "compiler": case.compiler,
            "projectKey": project_key(case),
            "recordKey": f"{mode}:{project_key(case)}",
            "projectRoot": str(case.project_root),
            "repoDir": str(case.repo_dir),
            "buildDir": str(case.build_dir),
            "traceDir": str(case.trace_dir),
            "status": "pending",
            "reason": "not-run",
            "baselineTimeMs": None,
            "postApplyTimeMs": None,
            "deltaMs": None,
            "deltaPercent": None,
            "baselineDisplay": "n/a",
            "postApplyDisplay": "n/a",
            "deltaDisplay": "n/a",
            "suggestionsFound": 0,
            "suggestionsApplied": 0,
            "suggestionsFailed": 0,
            "editsSupplied": 0,
            "changedFilesCount": 0,
            "applyCommand": "",
            "rollbackTriggered": False,
            "buildValidation": {
                "requested": False,
                "ran": False,
                "success": None,
                "errorCount": 0,
            },
            "applyErrors": [],
            "backupId": "",
            "revert": {"attempted": False, "success": None},
            "logs": {},
            "suggestionEdits": {
                "captured": False,
                "count": 0,
                "totalTextEdits": 0,
                "file": "",
            },
            "replaySource": {
                "requestedDir": str(self.args.replay_edits_dir) if self.args.replay_edits_dir else "",
                "file": "",
                "usedGeneratedFromDiscover": False,
            },
            "existingExports": self._collect_existing_exports(case),
            "startedAt": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"),
            "finishedAt": "",
            "elapsedSeconds": 0.0,
        }

    def _finalize_record(self, record: Dict[str, Any], start: float) -> Dict[str, Any]:
        record["finishedAt"] = datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")
        record["elapsedSeconds"] = round(time.time() - start, 3)
        record["baselineDisplay"] = format_duration(to_int(record["baselineTimeMs"]))
        record["postApplyDisplay"] = format_duration(to_int(record["postApplyTimeMs"]))
        record["deltaDisplay"] = format_duration(to_int(record["deltaMs"]))
        return record

    def _collect_existing_exports(self, case: ProjectCase) -> List[str]:
        export_dir = case.matrix_project_dir / "output" / "export"
        if not export_dir.exists():
            return []
        return [str(path) for path in sorted(export_dir.glob("*")) if path.is_file()]

    def _project_log_paths(self, case: ProjectCase, mode: str) -> Dict[str, str]:
        base = safe_name(f"{mode}-{case.project}-{case.build_system}-{case.compiler}")
        paths: Dict[str, str] = {"lspStderr": str(self.logs_dir / f"{base}.lsp.err")}
        build_log = Path(f"/tmp/bha-build-{case.project}.log")
        if build_log.exists():
            paths["buildLog"] = str(build_log)
        return paths

    def _clean_repo(self, case: ProjectCase, force: bool = False) -> Tuple[bool, str]:
        if not force and not self.args.clean_repo_before_case:
            return True, ""
        git_dir = case.repo_dir / ".git"
        tests_repos_root = (self.args.tests_root / "repos").resolve()
        try:
            relative = case.repo_dir.resolve().relative_to(tests_repos_root)
            if not relative.parts:
                return False, "invalid-repo-path"
        except ValueError:
            return False, "repo-outside-tests-repos"
        if not git_dir.exists():
            return False, "repo-not-git"

        reset = subprocess.run(
            ["git", "-C", str(case.repo_dir), "reset", "--hard"],
            check=False,
            capture_output=True,
            text=True,
        )
        if reset.returncode != 0:
            return False, f"git-reset-failed:{reset.stderr.strip()}"
        clean = subprocess.run(
            ["git", "-C", str(case.repo_dir), "clean", "-fd"],
            check=False,
            capture_output=True,
            text=True,
        )
        if clean.returncode != 0:
            return False, f"git-clean-failed:{clean.stderr.strip()}"
        return True, ""

    @staticmethod
    def _has_trace_files(case: ProjectCase) -> bool:
        return case.trace_dir.exists() and any(case.trace_dir.glob("*.json"))

    def _bootstrap_traces(self, case: ProjectCase, mode: str) -> Tuple[bool, str]:
        case.trace_dir.mkdir(parents=True, exist_ok=True)
        command = build_command(
            self.args.bha_path,
            case.build_system,
            case.compiler,
            case.project,
            case.project_root,
            case.build_dir,
            case.trace_dir,
        )
        log_name = safe_name(f"{mode}-{case.project}-{case.build_system}-{case.compiler}-trace-bootstrap.log")
        log_path = self.logs_dir / log_name
        try:
            proc = subprocess.run(
                command,
                cwd=REPO_ROOT,
                shell=True,
                check=False,
                capture_output=True,
                text=True,
                timeout=self.args.build_timeout,
            )
        except subprocess.TimeoutExpired:
            return False, f"trace-bootstrap-timeout-{self.args.build_timeout}s"

        output = f"$ {command}\n\n[stdout]\n{proc.stdout}\n\n[stderr]\n{proc.stderr}\n"
        log_path.write_text(output, encoding="utf-8")

        if proc.returncode != 0:
            combined = f"{proc.stdout}\n{proc.stderr}"
            dep_matches = re.findall(
                r"(?:Run-time dependency|Dependency)\s+([A-Za-z0-9_.+-]+)\s+found:\s+NO(?:\s+found\s+\S+\s+but need:\s+'([^']+)')?",
                combined,
            )
            if dep_matches:
                dependency, requirement = dep_matches[-1]
                requirement = (requirement or "").strip()
                if requirement:
                    return False, f"missing-external-dependency-{dependency}-{requirement}"
                return False, f"missing-external-dependency-{dependency}"
            return False, f"trace-bootstrap-failed-rc{proc.returncode}"
        if not self._has_trace_files(case):
            return False, "trace-bootstrap-produced-no-traces"
        return True, ""

    def _initialize_lsp(self, case: ProjectCase, stderr_path: Path) -> Tuple[Optional[LSPClient], Optional[str]]:
        client = LSPClient(self.args.server_path, cwd=case.project_root, stderr_path=stderr_path)
        try:
            client.start()
            settings = {
                "optimization": {
                    "autoApplyAll": False,
                    "showPreviewBeforeApply": False,
                    "rebuildAfterApply": True,
                    "rollbackOnBuildFailure": True,
                    "buildCommand": build_command(
                        self.args.bha_path,
                        case.build_system,
                        case.compiler,
                        case.project,
                        case.project_root,
                        case.build_dir,
                        case.trace_dir,
                    ),
                    "buildTimeout": self.args.build_timeout,
                    "keepBackups": True,
                    "allowMissingCompileCommands": True,
                    "includeUnsafeSuggestions": False,
                    "minConfidence": self.args.min_confidence,
                    "backupDirectory": str((case.project_root / ".lsp-optimization-backup").resolve()),
                }
            }
            response = client.initialize(case.project_root.as_uri(), settings)
            if not response or "result" not in response:
                return None, "initialize-failed"
            client.send_notification("workspace/didChangeConfiguration", {"settings": settings})
            client.send_notification("initialized")
            return client, None
        except Exception as exc:
            return None, f"initialize-exception: {exc}"

    def _precheck_case(self, case: ProjectCase, record: Dict[str, Any], excluded_projects: Set[str]) -> bool:
        if case.project in excluded_projects:
            record["status"] = "excluded"
            record["reason"] = "excluded-project"
            return False
        if not case.repo_dir.exists():
            record["status"] = "setup_failed"
            record["reason"] = "repo-missing"
            return False
        if not case.project_root.exists():
            record["status"] = "setup_failed"
            record["reason"] = "project-root-missing"
            return False
        if not self._has_trace_files(case):
            ok, reason = self._bootstrap_traces(case, str(record.get("mode", "case")))
            if not ok:
                if isinstance(reason, str) and reason.startswith("missing-external-dependency-"):
                    record["status"] = "no_suggestions"
                    record["reason"] = reason
                else:
                    record["status"] = "build_failed"
                    record["reason"] = reason or "trace-bootstrap-failed"
                return False
        return True

    def _analyze(
        self,
        client: LSPClient,
        case: ProjectCase,
        timeout_seconds: int,
    ) -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
        args = [{
            "projectRoot": str(case.project_root),
            "buildDir": str(case.build_dir),
            "rebuild": False,
        }]
        response, timed_out = execute_command_with_timeout(client, "bha.analyze", args, timeout_seconds)
        if timed_out:
            return None, f"analyze-timeout-{timeout_seconds}s"
        if not response or "result" not in response:
            return None, "analyze-failed"
        return response["result"], None

    def _capture_suggestion_edits(
        self,
        client: LSPClient,
        case: ProjectCase,
        suggestions: List[Dict[str, Any]],
    ) -> Dict[str, Any]:
        details_bundle: List[Dict[str, Any]] = []
        total_text_edits = 0
        for suggestion in suggestions:
            suggestion_id = suggestion.get("id")
            if not isinstance(suggestion_id, str) or not suggestion_id:
                continue
            detail_resp = client.execute_command("bha.getSuggestionDetails", [{"suggestionId": suggestion_id}])
            if not detail_resp or "result" not in detail_resp:
                details_bundle.append({
                    "id": suggestion_id,
                    "title": suggestion.get("title", ""),
                    "detailFetch": "failed",
                    "textEditCount": 0,
                    "textEdits": [],
                })
                continue

            detail = detail_resp["result"] or {}
            text_edits = detail.get("textEdits")
            if not isinstance(text_edits, list):
                text_edits = detail.get("text_edits")
            if not isinstance(text_edits, list):
                text_edits = []
            total_text_edits += len(text_edits)
            details_bundle.append({
                "id": suggestion_id,
                "title": detail.get("title", suggestion.get("title", "")),
                "type": detail.get("type", suggestion.get("type", "")),
                "priority": detail.get("priority", suggestion.get("priority")),
                "confidence": detail.get("confidence", suggestion.get("confidence")),
                "autoApplicable": bool(detail.get("autoApplicable", False)),
                "applicationMode": detail.get("applicationMode", ""),
                "autoApplyBlockedReason": detail.get("autoApplyBlockedReason"),
                "estimatedImpact": detail.get("estimatedImpact", suggestion.get("estimatedImpact")),
                "filesToCreate": detail.get("filesToCreate", []),
                "filesToModify": detail.get("filesToModify", []),
                "textEditCount": len(text_edits),
                "textEdits": text_edits,
            })

        payload = {
            "projectKey": project_key(case),
            "capturedAt": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"),
            "suggestionCount": len(details_bundle),
            "totalTextEdits": total_text_edits,
            "suggestions": details_bundle,
        }
        file_name = safe_name(f"{case.build_system}-{case.compiler}-{case.project}-suggestions.json")
        out_file = self.suggestion_edits_dir / file_name
        out_file.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        self.generated_suggestion_files[project_key(case)] = str(out_file)
        return {
            "captured": True,
            "count": len(details_bundle),
            "totalTextEdits": total_text_edits,
            "file": str(out_file),
        }

    def _extract_edits(
        self,
        payload: Dict[str, Any],
        allowed_ids: Optional[Set[str]] = None,
        selection_override: Optional[str] = None,
    ) -> List[Dict[str, Any]]:
        edits: List[Dict[str, Any]] = []
        selection = selection_override or self.args.replay_selection
        if selection == "all-edits" and isinstance(payload.get("edits"), list):
            edits.extend(payload["edits"])
        if selection == "all-edits" and isinstance(payload.get("textEdits"), list):
            edits.extend(payload["textEdits"])
        if selection == "all-edits" and isinstance(payload.get("text_edits"), list):
            edits.extend(payload["text_edits"])
        suggestions = payload.get("suggestions")
        if isinstance(suggestions, list):
            for item in suggestions:
                if not isinstance(item, dict):
                    continue
                if allowed_ids is not None:
                    sid = item.get("id")
                    if not isinstance(sid, str) or sid not in allowed_ids:
                        continue
                if selection == "auto-direct-only":
                    if not bool(item.get("autoApplicable", False)):
                        continue
                    mode = str(item.get("applicationMode") or "").strip().lower()
                    if mode != "direct-edits":
                        continue
                te = item.get("textEdits")
                if isinstance(te, list):
                    edits.extend(te)
                te = item.get("text_edits")
                if isinstance(te, list):
                    edits.extend(te)
                te = item.get("edits")
                if isinstance(te, list):
                    edits.extend(te)
        filtered: List[Dict[str, Any]] = []
        for edit in edits:
            if not isinstance(edit, dict):
                continue
            if "file" in edit or "path" in edit:
                filtered.append(edit)
        return filtered

    @staticmethod
    def _extract_auto_apply_candidates(payload: Dict[str, Any]) -> List[Dict[str, Any]]:
        result: List[Dict[str, Any]] = []
        suggestions = payload.get("suggestions")
        if not isinstance(suggestions, list):
            return result
        for item in suggestions:
            if not isinstance(item, dict):
                continue
            sid = item.get("id")
            if not isinstance(sid, str) or not sid:
                continue
            if not bool(item.get("autoApplicable", False)):
                continue
            mode = str(item.get("applicationMode") or "").strip().lower()
            if mode not in {"direct-edits", "external-refactor"}:
                continue
            candidate: Dict[str, Any] = {
                "id": sid,
                "title": str(item.get("title") or ""),
                "type": item.get("type"),
                "mode": mode,
            }
            if mode == "direct-edits":
                edits: List[Dict[str, Any]] = []
                for key in ("textEdits", "text_edits", "edits"):
                    value = item.get(key)
                    if isinstance(value, list):
                        for edit in value:
                            if isinstance(edit, dict) and ("file" in edit or "path" in edit):
                                edits.append(edit)
                if not edits:
                    continue
                candidate["edits"] = edits
            result.append(candidate)
        return result

    @staticmethod
    def _is_vendored_path(path: str) -> bool:
        normalized = path.replace("\\", "/").lower()
        markers = ("/3rdparty/", "/third_party/", "/vendor/", "/external/", "/subprojects/")
        return any(marker in normalized for marker in markers)

    def _filter_candidates_for_safety(self, candidates: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        safe: List[Dict[str, Any]] = []
        for candidate in candidates:
            suggestion_type = candidate.get("type")
            # Conservative rule: avoid auto-removing includes inside vendored/third-party code.
            if suggestion_type == 4:
                touches_vendor = False
                for edit in candidate.get("edits", []):
                    edit_file = str(edit.get("file") or edit.get("path") or "")
                    if edit_file and self._is_vendored_path(edit_file):
                        touches_vendor = True
                        break
                if touches_vendor:
                    continue
            safe.append(candidate)
        return safe

    def _apply_edits_batch(
        self,
        client: LSPClient,
        case: ProjectCase,
        edits: List[Dict[str, Any]],
    ) -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
        response, timed_out = execute_command_with_timeout(
            client,
            "bha.applyEdits",
            [{
                "projectRoot": str(case.project_root),
                "skipRebuild": False,
                "edits": edits,
            }],
            self.args.apply_timeout,
        )
        if timed_out:
            return None, f"apply-timeout-{self.args.apply_timeout}s"
        if not response or "result" not in response:
            return None, "apply-edits-command-failed"
        return response["result"] or {}, None

    def _apply_suggestion(
        self,
        client: LSPClient,
        suggestion_id: str,
    ) -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
        response, timed_out = execute_command_with_timeout(
            client,
            "bha.applySuggestion",
            [{
                "suggestionId": suggestion_id,
                "skipConsent": True,
                "skipRebuild": False,
            }],
            self.args.apply_timeout,
        )
        if timed_out:
            return None, f"apply-timeout-{self.args.apply_timeout}s"
        if not response or "result" not in response:
            return None, "apply-suggestion-command-failed"
        return response["result"] or {}, None

    def _resolve_replay_file(self, case: ProjectCase) -> Tuple[Optional[Path], bool]:
        key = project_key(case)
        if self.args.replay_edits_dir:
            base = self.args.replay_edits_dir
            name = safe_name(f"{case.build_system}-{case.compiler}-{case.project}-suggestions.json")
            candidates = [
                base / name,
                base / "suggestion_edits" / name,
                base / f"{case.project}.json",
            ]
            for candidate in candidates:
                if candidate.exists():
                    return candidate.resolve(), False
        generated = self.generated_suggestion_files.get(key)
        if generated:
            return Path(generated).resolve(), True
        return None, False

    def _compute_delta(self, record: Dict[str, Any]) -> None:
        baseline_ms = to_int(record.get("baselineTimeMs"))
        post_ms = to_int(record.get("postApplyTimeMs"))
        if baseline_ms is None or post_ms is None:
            return
        delta_ms = baseline_ms - post_ms
        record["deltaMs"] = delta_ms
        if baseline_ms > 0:
            record["deltaPercent"] = round((delta_ms / baseline_ms) * 100.0, 3)
        else:
            record["deltaPercent"] = 0.0

    @staticmethod
    def _first_error_message(errors: Any) -> str:
        if isinstance(errors, list):
            for err in errors:
                if isinstance(err, dict):
                    msg = err.get("message")
                    if isinstance(msg, str) and msg.strip():
                        return msg.strip()
                elif isinstance(err, str) and err.strip():
                    return err.strip()
        return ""

    def _maybe_revert(self, client: Optional[LSPClient], backup_id: str, record: Dict[str, Any]) -> None:
        if not client or not backup_id or self.args.keep_changes:
            return
        try:
            revert_resp = client.execute_command("bha.revertChanges", [{"backupId": backup_id}])
            ok = bool(revert_resp and "result" in revert_resp and revert_resp["result"].get("success"))
            record["revert"] = {"attempted": True, "success": ok}
        except Exception:
            record["revert"] = {"attempted": True, "success": False}

    def run_discover_case(self, case: ProjectCase, excluded_projects: Set[str]) -> Dict[str, Any]:
        start = time.time()
        record = self._base_record(case, "discover")
        record["applyCommand"] = "bha.applyEdits(auto-direct)"
        record["logs"] = self._project_log_paths(case, "discover")

        if not self._precheck_case(case, record, excluded_projects):
            self.discover_status[project_key(case)] = record["status"]
            return self._finalize_record(record, start)

        clean_ok, clean_err = self._clean_repo(case)
        if not clean_ok:
            record["status"] = "setup_failed"
            record["reason"] = clean_err or "repo-clean-failed"
            self.discover_status[project_key(case)] = record["status"]
            return self._finalize_record(record, start)

        ensure_trace_link(case.build_dir, case.trace_dir)
        ensure_compile_commands(case.build_dir)

        stderr_path = Path(record["logs"]["lspStderr"])
        client, init_error = self._initialize_lsp(case, stderr_path)
        if client is None:
            record["status"] = "apply_failed"
            record["reason"] = init_error or "initialize-failed"
            self.discover_status[project_key(case)] = record["status"]
            return self._finalize_record(record, start)

        backup_id = ""
        try:
            analyze_timeout = PROJECT_ANALYSIS_TIMEOUTS.get(case.project, self.args.analysis_timeout)
            baseline_result, analyze_error = self._analyze(client, case, analyze_timeout)
            if analyze_error:
                record["status"] = "analysis_timeout" if "timeout" in analyze_error else "apply_failed"
                record["reason"] = f"baseline-{analyze_error}"
                self.discover_status[project_key(case)] = record["status"]
                return self._finalize_record(record, start)

            suggestions = (baseline_result or {}).get("suggestions") or []
            baseline_metrics = (baseline_result or {}).get("baselineMetrics") or {}
            record["baselineTimeMs"] = to_int(baseline_metrics.get("totalDurationMs"))
            record["suggestionsFound"] = len(suggestions)
            record["suggestionEdits"] = self._capture_suggestion_edits(client, case, suggestions)

            if not suggestions:
                record["status"] = "no_suggestions"
                record["reason"] = "no-suggestions-generated"
                record["postApplyTimeMs"] = record["baselineTimeMs"]
                record["deltaMs"] = 0
                record["deltaPercent"] = 0.0
                self.discover_status[project_key(case)] = record["status"]
                return self._finalize_record(record, start)

            details_file = Path(str(record["suggestionEdits"].get("file") or ""))
            details_payload: Dict[str, Any] = {}
            if details_file.exists():
                try:
                    parsed = json.loads(details_file.read_text(encoding="utf-8"))
                    if isinstance(parsed, dict):
                        details_payload = parsed
                except Exception:
                    details_payload = {}

            candidates = self._extract_auto_apply_candidates(details_payload)
            candidates = self._filter_candidates_for_safety(candidates)
            if not candidates:
                record["status"] = "no_suggestions"
                record["reason"] = "no-safe-auto-applicable-suggestions"
                record["postApplyTimeMs"] = record["baselineTimeMs"]
                record["deltaMs"] = 0
                record["deltaPercent"] = 0.0
                self.discover_applied_ids[project_key(case)] = set()
                self.discover_status[project_key(case)] = record["status"]
                return self._finalize_record(record, start)

            direct_candidates = [c for c in candidates if str(c.get("mode")) == "direct-edits"]
            external_candidates = [c for c in candidates if str(c.get("mode")) == "external-refactor"]

            applied_ids: Set[str] = set()
            failed_ids: Set[str] = set()
            apply_errors: List[Any] = []
            changed_files: Set[str] = set()
            last_build_validation = record["buildValidation"]
            rollback_triggered = False

            if direct_candidates:
                bulk_edits: List[Dict[str, Any]] = []
                for candidate in direct_candidates:
                    bulk_edits.extend(candidate.get("edits", []))

                bulk_result, bulk_error = self._apply_edits_batch(client, case, bulk_edits)
                if bulk_error is None and bulk_result is not None and bool(bulk_result.get("success")):
                    applied_ids.update({str(candidate["id"]) for candidate in direct_candidates})
                    backup_id = str(bulk_result.get("backupId") or "")
                    record["backupId"] = backup_id
                    changed_files.update(
                        str(path) for path in (bulk_result.get("changedFiles") or []) if isinstance(path, str)
                    )
                    last_build_validation = bulk_result.get("buildValidation") or last_build_validation
                else:
                    if bulk_error:
                        apply_errors.append({"message": bulk_error})
                    elif bulk_result is not None:
                        apply_errors.extend(bulk_result.get("errors") or [])
                        rollback_triggered = rollback_triggered or bool((bulk_result.get("rollback") or {}).get("attempted"))
                    for candidate in direct_candidates:
                        edits = candidate.get("edits") or []
                        candidate_result, candidate_error = self._apply_edits_batch(client, case, edits)
                        if candidate_error:
                            failed_ids.add(str(candidate["id"]))
                            apply_errors.append({"message": candidate_error, "suggestionId": candidate["id"]})
                            continue
                        if not candidate_result:
                            failed_ids.add(str(candidate["id"]))
                            apply_errors.append({"message": "apply-edits-empty-result", "suggestionId": candidate["id"]})
                            continue
                        if bool(candidate_result.get("success")):
                            applied_ids.add(str(candidate["id"]))
                            backup_id = str(candidate_result.get("backupId") or backup_id)
                            for path in (candidate_result.get("changedFiles") or []):
                                if isinstance(path, str):
                                    changed_files.add(path)
                            last_build_validation = candidate_result.get("buildValidation") or last_build_validation
                        else:
                            failed_ids.add(str(candidate["id"]))
                            apply_errors.extend(candidate_result.get("errors") or [])
                            rollback_triggered = rollback_triggered or bool((candidate_result.get("rollback") or {}).get("attempted"))

            for candidate in external_candidates:
                suggestion_id = str(candidate.get("id") or "")
                if not suggestion_id:
                    continue
                candidate_result, candidate_error = self._apply_suggestion(client, suggestion_id)
                if candidate_error:
                    failed_ids.add(suggestion_id)
                    apply_errors.append({"message": candidate_error, "suggestionId": suggestion_id})
                    continue
                if not candidate_result:
                    failed_ids.add(suggestion_id)
                    apply_errors.append({"message": "apply-suggestion-empty-result", "suggestionId": suggestion_id})
                    continue
                if bool(candidate_result.get("success")):
                    applied_ids.add(suggestion_id)
                    backup_id = str(candidate_result.get("backupId") or backup_id)
                    for path in (candidate_result.get("changedFiles") or []):
                        if isinstance(path, str):
                            changed_files.add(path)
                    last_build_validation = candidate_result.get("buildValidation") or last_build_validation
                else:
                    failed_ids.add(suggestion_id)
                    apply_errors.extend(candidate_result.get("errors") or [])
                    rollback_triggered = rollback_triggered or bool((candidate_result.get("rollback") or {}).get("attempted"))

            record["backupId"] = backup_id
            record["suggestionsApplied"] = len(applied_ids)
            record["suggestionsFailed"] = len(failed_ids)
            record["applyErrors"] = apply_errors
            record["changedFilesCount"] = len(changed_files)
            record["buildValidation"] = last_build_validation
            record["rollbackTriggered"] = rollback_triggered
            self.discover_applied_ids[project_key(case)] = applied_ids

            if not applied_ids:
                record["status"] = "no_suggestions"
                first_error = self._first_error_message(apply_errors)
                record["reason"] = first_error or "no-safe-suggestions-applied"
                record["postApplyTimeMs"] = record["baselineTimeMs"]
                record["deltaMs"] = 0
                record["deltaPercent"] = 0.0
                self.discover_status[project_key(case)] = record["status"]
                return self._finalize_record(record, start)

            post_result, post_error = self._analyze(client, case, analyze_timeout)
            if post_error:
                record["status"] = "analysis_timeout" if "timeout" in post_error else "build_failed"
                record["reason"] = f"post-{post_error}"
                self.discover_status[project_key(case)] = record["status"]
                return self._finalize_record(record, start)
            post_metrics = (post_result or {}).get("baselineMetrics") or {}
            record["postApplyTimeMs"] = to_int(post_metrics.get("totalDurationMs"))
            self._compute_delta(record)
            record["status"] = "success"
            record["reason"] = "completed"
            self.discover_status[project_key(case)] = record["status"]
            return self._finalize_record(record, start)
        finally:
            self._maybe_revert(client, backup_id, record)
            if self.args.clean_repo_before_case and not self.args.keep_changes:
                self._clean_repo(case, force=True)
            if client:
                try:
                    client.shutdown()
                except Exception:
                    pass
                client.stop()

    def run_replay_case(self, case: ProjectCase, excluded_projects: Set[str]) -> Dict[str, Any]:
        start = time.time()
        record = self._base_record(case, "replay")
        record["applyCommand"] = "bha.applyEdits"
        record["logs"] = self._project_log_paths(case, "replay")

        if not self._precheck_case(case, record, excluded_projects):
            return self._finalize_record(record, start)

        clean_ok, clean_err = self._clean_repo(case)
        if not clean_ok:
            record["status"] = "setup_failed"
            record["reason"] = clean_err or "repo-clean-failed"
            return self._finalize_record(record, start)

        replay_file, from_generated = self._resolve_replay_file(case)
        if replay_file is None:
            record["status"] = "precondition_failed"
            record["reason"] = "replay-edits-missing"
            return self._finalize_record(record, start)
        record["replaySource"]["file"] = str(replay_file)
        record["replaySource"]["usedGeneratedFromDiscover"] = from_generated

        try:
            payload = json.loads(replay_file.read_text(encoding="utf-8"))
        except Exception as exc:
            record["status"] = "input_invalid"
            record["reason"] = f"replay-edits-invalid-json: {exc}"
            return self._finalize_record(record, start)

        payload_obj = payload if isinstance(payload, dict) else {}
        allowed_ids: Optional[Set[str]] = None
        selection = self.args.replay_selection
        if self.args.replay_selection == "discover-applied-only":
            allowed_ids = self.discover_applied_ids.get(project_key(case), set())
            if not allowed_ids:
                selection = "auto-direct-only"
                record["reason"] = "discover-applied-ids-missing-fallback-auto-direct-only"
        edits = self._extract_edits(payload_obj, allowed_ids, selection_override=selection)
        record["editsSupplied"] = len(edits)
        if not edits:
            record["status"] = "no_suggestions"
            record["reason"] = "replay-edits-empty-after-filter"
            return self._finalize_record(record, start)

        ensure_trace_link(case.build_dir, case.trace_dir)
        ensure_compile_commands(case.build_dir)

        stderr_path = Path(record["logs"]["lspStderr"])
        client, init_error = self._initialize_lsp(case, stderr_path)
        if client is None:
            record["status"] = "apply_failed"
            record["reason"] = init_error or "initialize-failed"
            return self._finalize_record(record, start)

        backup_id = ""
        try:
            analyze_timeout = PROJECT_ANALYSIS_TIMEOUTS.get(case.project, self.args.analysis_timeout)
            baseline_result, analyze_error = self._analyze(client, case, analyze_timeout)
            if analyze_error:
                record["status"] = "analysis_timeout" if "timeout" in analyze_error else "apply_failed"
                record["reason"] = f"baseline-{analyze_error}"
                return self._finalize_record(record, start)

            suggestions = (baseline_result or {}).get("suggestions") or []
            baseline_metrics = (baseline_result or {}).get("baselineMetrics") or {}
            record["suggestionsFound"] = len(suggestions)
            record["baselineTimeMs"] = to_int(baseline_metrics.get("totalDurationMs"))

            apply_resp, apply_timeout = execute_command_with_timeout(
                client,
                "bha.applyEdits",
                [{
                    "projectRoot": str(case.project_root),
                    "skipRebuild": False,
                    "edits": edits,
                }],
                self.args.apply_timeout,
            )
            if apply_timeout:
                record["status"] = "build_failed"
                record["reason"] = f"apply-timeout-{self.args.apply_timeout}s"
                return self._finalize_record(record, start)
            if not apply_resp or "result" not in apply_resp:
                record["status"] = "apply_failed"
                record["reason"] = "apply-edits-command-failed"
                return self._finalize_record(record, start)

            apply_result = apply_resp["result"] or {}
            backup_id = str(apply_result.get("backupId") or "")
            record["backupId"] = backup_id
            record["applyErrors"] = apply_result.get("errors") or []
            record["changedFilesCount"] = len(apply_result.get("changedFiles") or [])
            record["buildValidation"] = apply_result.get("buildValidation") or record["buildValidation"]
            rollback = apply_result.get("rollback") or {}
            record["rollbackTriggered"] = bool(rollback.get("attempted"))

            if not bool(apply_result.get("success")):
                first_error = self._first_error_message(record["applyErrors"])
                if record["rollbackTriggered"]:
                    record["status"] = "rollback_triggered"
                    record["reason"] = str(rollback.get("reason") or first_error or "rollback-attempted")
                elif (record["buildValidation"] or {}).get("ran") and not (record["buildValidation"] or {}).get("success"):
                    record["status"] = "build_failed"
                    record["reason"] = first_error or "build-validation-failed"
                else:
                    record["status"] = "apply_failed"
                    record["reason"] = first_error or "apply-edits-unsuccessful"
                return self._finalize_record(record, start)

            post_result, post_error = self._analyze(client, case, analyze_timeout)
            if post_error:
                record["status"] = "analysis_timeout" if "timeout" in post_error else "build_failed"
                record["reason"] = f"post-{post_error}"
                return self._finalize_record(record, start)

            post_metrics = (post_result or {}).get("baselineMetrics") or {}
            record["postApplyTimeMs"] = to_int(post_metrics.get("totalDurationMs"))
            self._compute_delta(record)
            record["status"] = "success"
            record["reason"] = "completed"
            return self._finalize_record(record, start)
        finally:
            self._maybe_revert(client, backup_id, record)
            if self.args.clean_repo_before_case and not self.args.keep_changes:
                self._clean_repo(case, force=True)
            if client:
                try:
                    client.shutdown()
                except Exception:
                    pass
                client.stop()

    def _aggregate(self, rows: List[Dict[str, Any]]) -> Dict[str, Any]:
        status_counts: Dict[str, int] = {}
        for row in rows:
            status = str(row.get("status") or "unknown")
            status_counts[status] = status_counts.get(status, 0) + 1

        comparable = [
            row for row in rows
            if to_int(row.get("baselineTimeMs")) is not None and to_int(row.get("postApplyTimeMs")) is not None
        ]
        successful = [row for row in rows if row.get("status") == "success"]

        total_baseline = sum(to_int(row.get("baselineTimeMs")) or 0 for row in comparable)
        total_post = sum(to_int(row.get("postApplyTimeMs")) or 0 for row in comparable)
        total_delta = total_baseline - total_post
        total_delta_percent = (total_delta / total_baseline * 100.0) if total_baseline > 0 else 0.0

        success_delta_percent = [
            float(row.get("deltaPercent"))
            for row in successful
            if isinstance(row.get("deltaPercent"), (int, float))
        ]
        mean_delta_percent = statistics.mean(success_delta_percent) if success_delta_percent else 0.0
        median_delta_percent = statistics.median(success_delta_percent) if success_delta_percent else 0.0

        return {
            "records": len(rows),
            "attempted": sum(1 for row in rows if row.get("status") != "excluded"),
            "successful": len(successful),
            "statusCounts": status_counts,
            "totalBaselineMs": total_baseline,
            "totalPostApplyMs": total_post,
            "totalDeltaMs": total_delta,
            "totalDeltaPercent": round(total_delta_percent, 3),
            "totalBaselineDisplay": format_duration(total_baseline),
            "totalPostApplyDisplay": format_duration(total_post),
            "totalDeltaDisplay": format_duration(total_delta),
            "meanDeltaPercentSuccessOnly": round(mean_delta_percent, 3),
            "medianDeltaPercentSuccessOnly": round(median_delta_percent, 3),
        }

    def build_summary(self) -> Dict[str, Any]:
        by_mode: Dict[str, Dict[str, Any]] = {}
        for mode in sorted({str(row.get("mode", "unknown")) for row in self.records}):
            by_mode[mode] = self._aggregate([row for row in self.records if row.get("mode") == mode])

        successful = [row for row in self.records if row.get("status") == "success"]
        sorted_success = sorted(successful, key=lambda row: to_int(row.get("deltaMs")) or 0, reverse=True)
        top_improvements = sorted_success[:5]
        top_regressions = [row for row in sorted_success if (to_int(row.get("deltaMs")) or 0) < 0][:5]

        return {
            "runMeta": {
                "timestampUtc": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"),
                "branch": run_git_command(["git", "branch", "--show-current"]),
                "commit": run_git_command(["git", "rev-parse", "HEAD"]),
                "testsRoot": str(self.args.tests_root),
                "outputDir": str(self.run_dir),
                "mode": self.args.mode,
                "replaySelection": self.args.replay_selection,
                "compilerFilter": self.args.compiler,
                "buildSystemFilter": self.args.build_system,
                "excludedProjects": sorted(self.args.excluded_projects),
                "replayEditsDir": str(self.args.replay_edits_dir) if self.args.replay_edits_dir else "",
                "keepChanges": self.args.keep_changes,
                "cleanRepoBeforeCase": self.args.clean_repo_before_case,
            },
            "totals": self._aggregate(self.records),
            "byMode": by_mode,
            "topImprovements": [
                {
                    "recordKey": row["recordKey"],
                    "deltaMs": row.get("deltaMs"),
                    "deltaDisplay": row.get("deltaDisplay"),
                    "deltaPercent": row.get("deltaPercent"),
                }
                for row in top_improvements
            ],
            "topRegressions": [
                {
                    "recordKey": row["recordKey"],
                    "deltaMs": row.get("deltaMs"),
                    "deltaDisplay": row.get("deltaDisplay"),
                    "deltaPercent": row.get("deltaPercent"),
                }
                for row in top_regressions
            ],
        }

    def write_outputs(self) -> None:
        summary = self.build_summary()
        failures = [
            row for row in self.records
            if row.get("status") not in {"success", "no_suggestions"}
        ]
        (self.run_dir / "results.json").write_text(
            json.dumps({"records": self.records}, indent=2),
            encoding="utf-8",
        )
        (self.run_dir / "summary.json").write_text(
            json.dumps(summary, indent=2),
            encoding="utf-8",
        )
        (self.run_dir / "failures.json").write_text(
            json.dumps({"failures": failures}, indent=2),
            encoding="utf-8",
        )
        (self.run_dir / "summary.md").write_text(
            self._render_markdown_summary(summary),
            encoding="utf-8",
        )

    def _render_markdown_summary(self, summary: Dict[str, Any]) -> str:
        totals = summary.get("totals", {})
        by_mode = summary.get("byMode", {})
        lines = [
            "# Repo Apply/Rebuild Benchmark Summary",
            "",
            f"- Timestamp (UTC): `{summary.get('runMeta', {}).get('timestampUtc', '')}`",
            f"- Branch: `{summary.get('runMeta', {}).get('branch', '')}`",
            f"- Commit: `{summary.get('runMeta', {}).get('commit', '')}`",
            f"- Mode: `{summary.get('runMeta', {}).get('mode', '')}`",
            f"- Replay selection: `{summary.get('runMeta', {}).get('replaySelection', '')}`",
            f"- Clean repo before case: `{summary.get('runMeta', {}).get('cleanRepoBeforeCase', '')}`",
            f"- Tests root: `{summary.get('runMeta', {}).get('testsRoot', '')}`",
            f"- Output dir: `{summary.get('runMeta', {}).get('outputDir', '')}`",
            "",
            "## Overall Totals",
            "",
            f"- Records: **{totals.get('records', 0)}**",
            f"- Attempted: **{totals.get('attempted', 0)}**",
            f"- Successful: **{totals.get('successful', 0)}**",
            f"- Fleet baseline: **{totals.get('totalBaselineDisplay', 'n/a')}**",
            f"- Fleet post-apply: **{totals.get('totalPostApplyDisplay', 'n/a')}**",
            f"- Fleet delta: **{totals.get('totalDeltaDisplay', 'n/a')} ({totals.get('totalDeltaPercent', 0.0):.3f}%)**",
            "",
            "## Mode Breakdown",
            "",
        ]

        for mode in sorted(by_mode.keys()):
            data = by_mode[mode]
            lines.append(
                f"- `{mode}`: records={data.get('records', 0)}, attempted={data.get('attempted', 0)}, "
                f"success={data.get('successful', 0)}, delta={data.get('totalDeltaDisplay', 'n/a')} "
                f"({data.get('totalDeltaPercent', 0.0):.3f}%)"
            )

        lines.extend([
            "",
            "## Status Counts",
            "",
        ])
        for status, count in sorted((totals.get("statusCounts") or {}).items()):
            lines.append(f"- `{status}`: **{count}**")

        lines.extend([
            "",
            "## Per-Record Results",
            "",
            "| Mode | Project | System | Compiler | Status | Before | After | Delta | Delta % | Applied | Found | Edits Supplied |",
            "|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|",
        ])

        for row in sorted(self.records, key=lambda r: r["recordKey"]):
            delta_percent = row.get("deltaPercent")
            delta_percent_str = "n/a" if delta_percent is None else f"{float(delta_percent):.3f}%"
            applied = row.get("suggestionsApplied", 0) if row.get("mode") == "discover" else row.get("changedFilesCount", 0)
            lines.append(
                f"| `{row['mode']}` | `{row['project']}` | `{row['buildSystem']}` | `{row['compiler']}` | "
                f"`{row['status']}` | {row['baselineDisplay']} | {row['postApplyDisplay']} | "
                f"{row['deltaDisplay']} | {delta_percent_str} | {applied} | {row['suggestionsFound']} | {row.get('editsSupplied', 0)} |"
            )

        failures = [row for row in self.records if row.get("status") not in {"success", "no_suggestions"}]
        if failures:
            lines.extend(["", "## Failures", ""])
            for row in failures:
                lines.append(f"- `{row['recordKey']}`: `{row['status']}` ({row.get('reason', '')})")

        lines.append("")
        return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run repo-wide benchmark for discover+apply and/or replay-edits apply paths.",
    )
    parser.add_argument("--tests-root", type=Path, default=(REPO_ROOT / "tests" / "cli"))
    parser.add_argument("--output-root", type=Path, default=(REPO_ROOT / "tests" / "cli" / "benchmarks"))
    parser.add_argument("--mode", choices=["discover", "replay", "both"], default="both")
    parser.add_argument("--replay-edits-dir", type=Path, default=None)
    parser.add_argument(
        "--replay-selection",
        choices=["discover-applied-only", "auto-direct-only", "all-edits"],
        default="discover-applied-only",
        help="Which edit subset to replay from suggestion files.",
    )
    parser.add_argument("--compiler", choices=["all", "clang", "gcc"], default="clang")
    parser.add_argument("--build-system", choices=["all", "cmake", "make", "meson"], default="all")
    parser.add_argument("--project", action="append", default=[])
    parser.add_argument("--exclude-project", action="append", default=[])
    parser.add_argument("--include-rocksdb", action="store_true")
    parser.add_argument("--analysis-timeout", type=int, default=300)
    parser.add_argument("--apply-timeout", type=int, default=7800)
    parser.add_argument("--build-timeout", type=int, default=7200)
    parser.add_argument("--min-confidence", type=float, default=0.5)
    parser.add_argument("--server-path", type=Path, default=default_server_path())
    parser.add_argument("--bha-path", type=Path, default=(REPO_ROOT / "build" / "bha"))
    parser.add_argument("--keep-changes", action="store_true")
    parser.add_argument(
        "--clean-repo-before-case",
        action="store_true",
        default=True,
        help="Hard reset + clean each repo before each case for reproducible runs (default: enabled).",
    )
    parser.add_argument(
        "--no-clean-repo-before-case",
        dest="clean_repo_before_case",
        action="store_false",
        help="Disable repo cleanup before each case.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.tests_root = args.tests_root.resolve()
    args.output_root = args.output_root.resolve()
    args.server_path = args.server_path.resolve()
    args.bha_path = args.bha_path.resolve()
    if args.replay_edits_dir:
        args.replay_edits_dir = args.replay_edits_dir.resolve()

    if not args.tests_root.exists():
        print(f"Error: tests root not found: {args.tests_root}")
        return 1
    if not args.server_path.exists():
        print(f"Error: bha-lsp not found: {args.server_path}")
        return 1
    if not args.bha_path.exists():
        print(f"Error: bha binary not found: {args.bha_path}")
        return 1
    if args.mode == "replay" and args.replay_edits_dir and not args.replay_edits_dir.exists():
        print(f"Error: replay edits dir not found: {args.replay_edits_dir}")
        return 1
    if args.mode == "replay" and args.replay_selection == "discover-applied-only":
        args.replay_selection = "auto-direct-only"

    selected_projects = set(args.project) if args.project else None
    excluded_projects = set(DEFAULT_EXCLUDED_PROJECTS)
    if args.include_rocksdb:
        excluded_projects.discard("rocksdb")
    excluded_projects.update(args.exclude_project)
    args.excluded_projects = excluded_projects

    cases = discover_cases(args.tests_root, args.compiler, args.build_system, selected_projects)
    if not cases:
        print("No project cases discovered.")
        return 0

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    run_dir = args.output_root / timestamp
    run_dir.mkdir(parents=True, exist_ok=True)

    print(f"Discovered {len(cases)} project cases")
    print(f"Mode: {args.mode}")
    print(f"Replay selection: {args.replay_selection}")
    print(f"Clean repo before case: {args.clean_repo_before_case}")
    print(f"Output: {run_dir}")
    print(f"Excluded projects: {', '.join(sorted(excluded_projects)) if excluded_projects else '(none)'}")
    if args.replay_edits_dir:
        print(f"Replay edits dir: {args.replay_edits_dir}")

    runner = BenchmarkRunner(args, run_dir)
    for idx, case in enumerate(cases, start=1):
        print(f"[{idx}/{len(cases)}] {project_key(case)}")
        if args.mode in {"discover", "both"}:
            discover_record = runner.run_discover_case(case, excluded_projects)
            runner.records.append(discover_record)
            print(
                f"  discover -> status={discover_record['status']} "
                f"delta={discover_record['deltaDisplay']} "
                f"found={discover_record['suggestionsFound']} "
                f"applied={discover_record['suggestionsApplied']}"
            )
        if args.mode in {"replay", "both"}:
            replay_record = runner.run_replay_case(case, excluded_projects)
            runner.records.append(replay_record)
            print(
                f"  replay   -> status={replay_record['status']} "
                f"delta={replay_record['deltaDisplay']} "
                f"edits={replay_record['editsSupplied']} "
                f"changedFiles={replay_record['changedFilesCount']}"
            )

    runner.write_outputs()
    print("\nArtifacts written:")
    print(f"  - {run_dir / 'results.json'}")
    print(f"  - {run_dir / 'summary.json'}")
    print(f"  - {run_dir / 'summary.md'}")
    print(f"  - {run_dir / 'failures.json'}")
    print(f"  - {run_dir / 'suggestion_edits'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
