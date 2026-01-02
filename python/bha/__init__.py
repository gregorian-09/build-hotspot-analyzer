"""
BHA - Build Hotspot Analyzer

A Python library for analyzing and optimizing C++ build performance.
Identifies compilation bottlenecks and provides actionable suggestions.
"""

import os
from typing import List, Optional, Any

__version__ = "1.0.0"
__author__ = "BHA Contributors"

_HAS_NATIVE = False
_native = None

try:
    from . import _bha_native as native_mod
    _native = native_mod
    _HAS_NATIVE = True
except ImportError as e:
    _HAS_NATIVE = False
    _native = None

from .types import (
    Duration,
    CompilerType,
    SuggestionType,
    Priority,
    Confidence,
    ExportFormat,
    IncludeInfo,
    TemplateInstantiation,
    SymbolInfo,
    CompilationUnit,
    BuildTrace,
    FileMetrics,
    IncludeMetrics,
    TemplateMetrics,
    SymbolMetrics,
    DependencyMetrics,
    AnalysisResult,
    Suggestion,
    ExportOptions,
)


def has_native_module() -> bool:
    """Check if the native C++ module is available."""
    return _HAS_NATIVE


class Analyzer:
    """
    High-level interface for build trace analysis.

    This class provides a convenient way to parse build traces,
    run analysis, and generate suggestions.

    Example:
        >>> analyzer = Analyzer()
        >>> analyzer.add_trace_file("build/trace.json")
        >>> results = analyzer.analyze()
        >>> for suggestion in results.suggestions:
        ...     print(f"{suggestion.priority}: {suggestion.title}")
    """

    def __init__(self):
        """Initialize the analyzer."""
        self._trace_files: List[str] = []
        self._trace_content: List[str] = []
        self._analysis_result: Optional[AnalysisResult] = None
        self._suggestions: List[Suggestion] = []
        self._native_traces: List[Any] = []  # Native BuildTrace objects
        self._native_analysis_result = None

    def add_trace_file(self, path: str) -> None:
        """
        Add a trace file to be analyzed.

        Args:
            path: Path to a compiler trace file (e.g., Clang -ftime-trace output)

        Raises:
            FileNotFoundError: If the trace file doesn't exist
        """
        if not os.path.exists(path):
            raise FileNotFoundError(f"Trace file not found: {path}")
        self._trace_files.append(path)

    def add_trace_content(self, content: str) -> None:
        """
        Add trace content directly as a string.

        Args:
            content: Raw trace content (e.g., JSON from -ftime-trace)
        """
        self._trace_content.append(content)

    def analyze(self) -> 'AnalysisResult':
        """
        Run analysis on all added traces.

        Returns:
            AnalysisResult containing metrics and analysis data

        Raises:
            RuntimeError: If no traces have been added
        """
        if not self._trace_files and not self._trace_content:
            raise RuntimeError("No trace files or content added")

        if _HAS_NATIVE and _native is not None:
            self._native_traces = []
            self._native_analysis_result = None

            for path in self._trace_files:
                try:
                    trace = _native.parse_trace_file(path)
                    self._native_traces.append(trace)
                except RuntimeError:
                    pass

            if self._native_traces:
                combined_trace = self._native_traces[0]
                options = _native.AnalysisOptions()
                self._native_analysis_result = _native.run_full_analysis(combined_trace, options)
                self._analysis_result = AnalysisResult.from_native(self._native_analysis_result)
            else:
                self._analysis_result = self._analyze_python()
        else:
            self._analysis_result = self._analyze_python()

        return self._analysis_result

    def get_suggestions(self) -> List[Suggestion]:
        """
        Get optimization suggestions based on analysis.

        Must call analyze() first.

        Returns:
            List of Suggestion objects ordered by priority
        """
        if self._analysis_result is None:
            raise RuntimeError("Must call analyze() first")

        if _HAS_NATIVE and _native is not None and self._native_traces and hasattr(self, '_native_analysis_result'):
            try:
                options = _native.SuggesterOptions()
                native_suggestions = _native.generate_suggestions(
                    self._native_traces[0],
                    self._native_analysis_result,
                    options
                )
                self._suggestions = [
                    Suggestion.from_native(s) for s in native_suggestions
                ]
            except Exception:
                self._suggestions = self._generate_suggestions_python()
        else:
            self._suggestions = self._generate_suggestions_python()

        return self._suggestions

    def export(
            self,
            export_format: ExportFormat = ExportFormat.JSON,
            path: Optional[str] = None,
            options: Optional[ExportOptions] = None
    ) -> str:
        """
        Export analysis results and suggestions.

        Args:
            export_format: Output format (JSON, HTML, CSV, SARIF, Markdown)
            path: Optional file path to write output
            options: Export configuration options

        Returns:
            Exported content as string
        """
        if self._analysis_result is None:
            raise RuntimeError("Must call analyze() first")

        if options is None:
            options = ExportOptions()

        if _HAS_NATIVE and _native is not None and hasattr(self, '_native_analysis_result') and self._native_analysis_result:
            try:
                format_map = {
                    ExportFormat.JSON: _native.ExportFormat.JSON,
                    ExportFormat.HTML: _native.ExportFormat.HTML,
                    ExportFormat.CSV: _native.ExportFormat.CSV,
                    ExportFormat.SARIF: _native.ExportFormat.SARIF,
                    ExportFormat.MARKDOWN: _native.ExportFormat.Markdown,
                }
                native_format = format_map.get(export_format, _native.ExportFormat.JSON)
                native_options = _native.ExportOptions()
                content = _native.export_to_string(
                    self._native_analysis_result,
                    self._suggestions if self._suggestions else [],
                    native_format,
                    native_options
                )
            except Exception:
                content = self._export_python(export_format, options)
        else:
            content = self._export_python(export_format, options)

        if path:
            with open(path, 'w') as f:
                f.write(content)

        return content

    def _analyze_python(self) -> 'AnalysisResult':
        """Pure Python analysis implementation (limited)."""
        import json

        units = []
        for path in self._trace_files:
            with open(path, 'r') as f:
                try:
                    data = json.load(f)
                    unit = CompilationUnit.from_json(data, path)
                    units.append(unit)
                except json.JSONDecodeError as json_decode_error:
                    raise RuntimeError(f"Failed to parse {path}: {json_decode_error}")

        for content in self._trace_content:
            try:
                data = json.loads(content)
                unit = CompilationUnit.from_json(data, "<content>")
                units.append(unit)
            except json.JSONDecodeError as json_decode_error:
                raise RuntimeError(f"Failed to parse content: {json_decode_error}")

        return AnalysisResult.from_compilation_units(units)

    def _generate_suggestions_python(self) -> List[Suggestion]:
        """Pure Python suggestion generation (limited)."""
        suggestions = []

        if self._analysis_result:
            if self._analysis_result.file_metrics:
                for fm in self._analysis_result.file_metrics:
                    if fm.compile_time_ms > 5000:
                        suggestions.append(Suggestion(
                            type=SuggestionType.PCH,
                            priority=Priority.HIGH,
                            title=f"Consider PCH for {fm.file_path}",
                            description=f"File takes {fm.compile_time_ms:.0f}ms to compile",
                            file_path=fm.file_path,
                            estimated_impact_ms=fm.compile_time_ms * 0.3,
                            confidence=Confidence.MEDIUM,
                        ))

        return suggestions

    def _export_python(self, export_format: ExportFormat, options: ExportOptions) -> str:
        """Pure Python export implementation."""
        import json

        if export_format == ExportFormat.JSON:
            return json.dumps(
                self._analysis_result.to_dict(),
                indent=2 if options.pretty_print else None
            )
        else:
            raise NotImplementedError(f"Format {format} not supported in pure Python mode")


def analyze_trace(path: str) -> AnalysisResult:
    """
    Convenience function to analyze a single trace file.

    Args:
        path: Path to trace file

    Returns:
        AnalysisResult with metrics and analysis
    """
    analyzer = Analyzer()
    analyzer.add_trace_file(path)
    return analyzer.analyze()


def analyze_traces(paths: List[str]) -> AnalysisResult:
    """
    Convenience function to analyze multiple trace files.

    Args:
        paths: List of paths to trace files

    Returns:
        Combined AnalysisResult
    """
    analyzer = Analyzer()
    for path in paths:
        analyzer.add_trace_file(path)
    return analyzer.analyze()


__all__ = [
    "__version__",

    # Main classes
    "Analyzer",

    # Types
    "Duration",
    "CompilerType",
    "SuggestionType",
    "Priority",
    "Confidence",
    "ExportFormat",
    "IncludeInfo",
    "TemplateInstantiation",
    "SymbolInfo",
    "CompilationUnit",
    "BuildTrace",
    "FileMetrics",
    "IncludeMetrics",
    "TemplateMetrics",
    "SymbolMetrics",
    "DependencyMetrics",
    "AnalysisResult",
    "Suggestion",
    "ExportOptions",

    # Functions
    "has_native_module",
    "analyze_trace",
    "analyze_traces",
]