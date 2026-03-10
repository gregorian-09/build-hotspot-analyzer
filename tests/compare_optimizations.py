#!/usr/bin/env python3

import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional

sys.path.insert(0, str(Path(__file__).parent))
from lsp_test_client import LSPClient


def analyze_project(client: LSPClient, project_root: Path, build_dir: Path) -> Optional[Dict]:
    print(f"\nAnalyzing {project_root.name}...")

    response = client.execute_command("bha.analyze", [{
        "projectRoot": str(project_root),
        "buildDir": str(build_dir),
        "rebuild": False
    }])

    if response and "result" in response:
        return response["result"]
    elif response and "error" in response:
        print(f"  Analysis failed: {response['error'].get('message', 'Unknown error')}")
        return None
    return None


def filter_high_impact_suggestions(analysis_result: Dict) -> List[Dict]:
    suggestions = analysis_result.get("suggestions", [])
    high_impact = []

    for sug in suggestions:
        priority = sug.get("priority", 2)
        confidence = sug.get("confidence", 0.0)
        impact = sug.get("estimatedImpact", {})
        time_saved = impact.get("timeSavedMs", 0)

        if priority == 0 and confidence >= 0.7 and time_saved >= 1000:
            high_impact.append(sug)

    return high_impact


def apply_suggestion(client: LSPClient, suggestion_id: str) -> bool:
    response = client.execute_command("bha.applySuggestion", [{
        "suggestionId": suggestion_id
    }])

    if response and "result" in response:
        result = response["result"]
        return result.get("success", False)
    return False


def get_build_metrics(analysis_result: Dict) -> Dict:
    metrics = analysis_result.get("baselineMetrics", {})
    return {
        "totalDurationMs": metrics.get("totalDurationMs", 0),
        "filesCompiled": metrics.get("filesCompiled", 0)
    }


class OptimizationComparison:
    def __init__(self, bha_lsp_path: Path, test_root: Path):
        self.bha_lsp_path = bha_lsp_path
        self.test_root = test_root
        self.results = []

    def rebuild_project(self, project_root: Path, build_dir: Path, build_system: str, compiler: str) -> bool:
        bha_binary = self.bha_lsp_path.parent / "bha"

        cmd = [
            str(bha_binary),
            "build",
            "--build-system", build_system,
            "--compiler", compiler,
            "--build-dir", str(build_dir)
        ]

        print(f"  Rebuilding with: {' '.join(cmd)}")

        try:
            result = subprocess.run(
                cmd,
                cwd=project_root,
                capture_output=True,
                text=True,
                timeout=300
            )
            return result.returncode == 0
        except subprocess.TimeoutExpired:
            print("  Rebuild timeout")
            return False
        except Exception as e:
            print(f"  Rebuild error: {e}")
            return False

    def compare_project(self, project_path: Path, build_system: str, compiler: str):
        print(f"\n{'='*80}")
        print(f"Project: {project_path.name}")
        print(f"Build System: {build_system}")
        print(f"Compiler: {compiler}")
        print(f"{'='*80}")

        build_dir = project_path / "build"
        traces_dir = project_path / "traces"
        repo_dir = self.test_root.parent / "repos" / project_path.name.split('/')[-1]

        if not traces_dir.exists() or not any(traces_dir.glob("*.json")):
            print(f"  Skipping: No trace files found in {traces_dir}")
            return

        if not repo_dir.exists():
            print(f"  Skipping: Repository not found at {repo_dir}")
            return

        client = LSPClient(self.bha_lsp_path)

        try:
            client.start()
            time.sleep(0.5)

            client.initialize(str(repo_dir))
            client.send_notification("initialized")

            baseline_analysis = analyze_project(client, repo_dir, traces_dir)
            if not baseline_analysis:
                print("  Failed to get baseline analysis")
                return

            baseline_metrics = get_build_metrics(baseline_analysis)
            print(f"\n  Baseline build time: {baseline_metrics['totalDurationMs'] / 1000:.2f}s")
            print(f"  Files compiled: {baseline_metrics['filesCompiled']}")

            high_impact = filter_high_impact_suggestions(baseline_analysis)
            print(f"\n  Found {len(high_impact)} high-impact suggestions")

            if len(high_impact) == 0:
                print("  No high-impact suggestions to apply")
                self.results.append({
                    "project": project_path.name,
                    "buildSystem": build_system,
                    "compiler": compiler,
                    "baselineTimeMs": baseline_metrics["totalDurationMs"],
                    "optimizedTimeMs": baseline_metrics["totalDurationMs"],
                    "improvement": 0.0,
                    "suggestionsApplied": 0,
                    "status": "no_suggestions"
                })
                return

            for i, sug in enumerate(high_impact[:3], 1):
                print(f"\n  Suggestion {i}: {sug['title']}")
                print(f"    Priority: HIGH, Confidence: {sug['confidence']*100:.0f}%")
                print(f"    Estimated savings: {sug['estimatedImpact']['timeSavedMs']/1000:.2f}s")

            print(f"\n  Applying {len(high_impact[:3])} suggestions...")
            applied_count = 0

            for sug in high_impact[:3]:
                if apply_suggestion(client, sug["id"]):
                    applied_count += 1
                    print(f"    Applied: {sug['title']}")
                else:
                    print(f"    Failed: {sug['title']}")

            if applied_count == 0:
                print("  No suggestions were successfully applied")
                self.results.append({
                    "project": project_path.name,
                    "buildSystem": build_system,
                    "compiler": compiler,
                    "baselineTimeMs": baseline_metrics["totalDurationMs"],
                    "optimizedTimeMs": baseline_metrics["totalDurationMs"],
                    "improvement": 0.0,
                    "suggestionsApplied": 0,
                    "status": "apply_failed"
                })
                return

            print(f"\n  Rebuilding project...")
            if not self.rebuild_project(repo_dir, build_dir, build_system, compiler):
                print("  Rebuild failed")
                self.results.append({
                    "project": project_path.name,
                    "buildSystem": build_system,
                    "compiler": compiler,
                    "baselineTimeMs": baseline_metrics["totalDurationMs"],
                    "optimizedTimeMs": baseline_metrics["totalDurationMs"],
                    "improvement": 0.0,
                    "suggestionsApplied": applied_count,
                    "status": "rebuild_failed"
                })
                return

            print(f"  Re-analyzing...")
            optimized_analysis = analyze_project(client, repo_dir, traces_dir)
            if not optimized_analysis:
                print("  Failed to get optimized analysis")
                return

            optimized_metrics = get_build_metrics(optimized_analysis)
            print(f"\n  Optimized build time: {optimized_metrics['totalDurationMs'] / 1000:.2f}s")

            improvement_ms = baseline_metrics["totalDurationMs"] - optimized_metrics["totalDurationMs"]
            improvement_pct = (improvement_ms / baseline_metrics["totalDurationMs"]) * 100 if baseline_metrics["totalDurationMs"] > 0 else 0

            print(f"  Improvement: {improvement_ms/1000:.2f}s ({improvement_pct:.1f}%)")

            self.results.append({
                "project": project_path.name,
                "buildSystem": build_system,
                "compiler": compiler,
                "baselineTimeMs": baseline_metrics["totalDurationMs"],
                "optimizedTimeMs": optimized_metrics["totalDurationMs"],
                "improvementMs": improvement_ms,
                "improvementPercent": improvement_pct,
                "suggestionsApplied": applied_count,
                "status": "success"
            })

        except Exception as e:
            print(f"  Error: {e}")
            import traceback
            traceback.print_exc()
        finally:
            client.stop()

    def print_summary(self):
        print(f"\n\n{'='*80}")
        print("OPTIMIZATION COMPARISON SUMMARY")
        print(f"{'='*80}\n")

        successful = [r for r in self.results if r["status"] == "success"]

        if not successful:
            print("No successful comparisons to report.")
            return

        print(f"{'Project':<20} {'System':<8} {'Compiler':<8} {'Before (s)':<12} {'After (s)':<12} {'Improvement'}")
        print("-" * 80)

        total_baseline = 0
        total_optimized = 0

        for result in successful:
            project_name = result["project"].split('/')[-1]
            before = result["baselineTimeMs"] / 1000
            after = result["optimizedTimeMs"] / 1000
            improvement = result.get("improvementPercent", 0)

            print(f"{project_name:<20} {result['buildSystem']:<8} {result['compiler']:<8} "
                  f"{before:>10.2f}s  {after:>10.2f}s  {improvement:>6.1f}%")

            total_baseline += result["baselineTimeMs"]
            total_optimized += result["optimizedTimeMs"]

        print("-" * 80)

        if total_baseline > 0:
            total_improvement = ((total_baseline - total_optimized) / total_baseline) * 100
            print(f"{'TOTAL':<20} {'':<8} {'':<8} "
                  f"{total_baseline/1000:>10.2f}s  {total_optimized/1000:>10.2f}s  {total_improvement:>6.1f}%")

        print(f"\n\nSuccessful: {len(successful)}/{len(self.results)} projects")

        output_file = Path(__file__).parent / "optimization_comparison.json"
        with open(output_file, 'w') as f:
            json.dump(self.results, f, indent=2)
        print(f"\nFull results saved to: {output_file}")

def main():
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent
    candidates = [
        repo_root / "build" / "lsp" / "bha-lsp",
        repo_root / "build" / "bha-lsp",
        repo_root / "cmake-build-debug" / "bha-lsp",
    ]
    bha_lsp_path = next((path for path in candidates if path.exists()), candidates[0])
    test_root = script_dir / "cli"

    if not bha_lsp_path.exists():
        print(f"Error: bha-lsp not found at {bha_lsp_path}")
        sys.exit(1)

    print("Build Hotspot Analyzer - Optimization Comparison")
    print(f"LSP Server: {bha_lsp_path}")
    print(f"Test Root: {test_root}")

    comparison = OptimizationComparison(bha_lsp_path, test_root)

    for build_system_dir in sorted(test_root.iterdir()):
        if not build_system_dir.is_dir() or build_system_dir.name in ["repos", "__pycache__"]:
            continue

        build_system = build_system_dir.name

        for compiler_dir in sorted(build_system_dir.iterdir()):
            if not compiler_dir.is_dir():
                continue

            compiler = compiler_dir.name

            for project_dir in sorted(compiler_dir.iterdir()):
                if not project_dir.is_dir():
                    continue

                comparison.compare_project(project_dir, build_system, compiler)

    comparison.print_summary()

if __name__ == "__main__":
    main()
