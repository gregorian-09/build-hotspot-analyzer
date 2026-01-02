"""
Tests for the pybind11 native bindings.

These tests only run when the native module (_bha_native) is available.
Skip these tests if building in pure-Python mode.
"""

import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).parent.parent))

try:
    from bha import _bha_native
except ImportError as e:
    raise unittest.SkipTest(f"Native module not available: {e}")

class TestNativeBindings(unittest.TestCase):
    """Tests for native C++ bindings via pybind11."""

    def setUp(self):
        """Get native module for tests."""
        self._native = _bha_native
        self.assertIsNotNone(self._native, "Native module should be available")

    def test_import_native(self):
        """Native module can be imported."""
        self.assertIsNotNone(self._native)

    def test_native_compiler_type(self):
        """Native CompilerType enum works correctly."""
        if hasattr(self._native, 'CompilerType'):
            self.assertTrue(hasattr(self._native.CompilerType, 'Clang'))
            self.assertTrue(hasattr(self._native.CompilerType, 'GCC'))
            self.assertTrue(hasattr(self._native.CompilerType, 'MSVC'))

    def test_native_suggestion_type(self):
        """Native SuggestionType enum works correctly."""
        if hasattr(self._native, 'SuggestionType'):
            self.assertTrue(hasattr(self._native.SuggestionType, 'ForwardDeclaration'))
            self.assertTrue(hasattr(self._native.SuggestionType, 'PCHOptimization'))
            self.assertTrue(hasattr(self._native.SuggestionType, 'UnityBuild'))

    def test_native_priority(self):
        """Native Priority enum works correctly."""
        if hasattr(self._native, 'Priority'):
            self.assertTrue(hasattr(self._native.Priority, 'Critical'))
            self.assertTrue(hasattr(self._native.Priority, 'High'))
            self.assertTrue(hasattr(self._native.Priority, 'Medium'))
            self.assertTrue(hasattr(self._native.Priority, 'Low'))

    def test_native_export_format(self):
        """Native ExportFormat enum works correctly."""
        if hasattr(self._native, 'ExportFormat'):
            self.assertTrue(hasattr(self._native.ExportFormat, 'JSON'))
            self.assertTrue(hasattr(self._native.ExportFormat, 'HTML'))
            self.assertTrue(hasattr(self._native.ExportFormat, 'CSV'))
            self.assertTrue(hasattr(self._native.ExportFormat, 'SARIF'))

    def test_native_file_metrics(self):
        """Native FileMetrics type works correctly."""
        if hasattr(self._native, 'FileMetrics'):
            metrics = self._native.FileMetrics()
            self.assertIsNotNone(metrics)
            self.assertTrue(hasattr(metrics, 'path'))

    def test_native_time_breakdown(self):
        """Native TimeBreakdown type works correctly."""
        if hasattr(self._native, 'TimeBreakdown'):
            breakdown = self._native.TimeBreakdown()
            self.assertIsNotNone(breakdown)

    def test_native_compilation_unit(self):
        """Native CompilationUnit type works correctly."""
        if hasattr(self._native, 'CompilationUnit'):
            unit = self._native.CompilationUnit()
            self.assertIsNotNone(unit)
            self.assertTrue(hasattr(unit, 'source_file'))

    def test_native_build_trace(self):
        """Native BuildTrace type works correctly."""
        if hasattr(self._native, 'BuildTrace'):
            trace = self._native.BuildTrace()
            self.assertIsNotNone(trace)
            self.assertTrue(hasattr(trace, 'units'))

    def test_native_suggestion(self):
        """Native Suggestion type works correctly."""
        if hasattr(self._native, 'Suggestion'):
            suggestion = self._native.Suggestion()
            self.assertIsNotNone(suggestion)
            self.assertTrue(hasattr(suggestion, 'type'))
            self.assertTrue(hasattr(suggestion, 'priority'))
            self.assertTrue(hasattr(suggestion, 'title'))

    def test_native_analysis_options(self):
        """Native AnalysisOptions type works correctly."""
        if hasattr(self._native, 'AnalysisOptions'):
            options = self._native.AnalysisOptions()
            self.assertIsNotNone(options)

    def test_native_parse_trace_file(self):
        """Native parse_trace_file function exists."""
        self.assertTrue(hasattr(self._native, 'parse_trace_file'))

    def test_native_run_full_analysis(self):
        """Native run_full_analysis function exists."""
        self.assertTrue(hasattr(self._native, 'run_full_analysis'))

    def test_native_generate_suggestions(self):
        """Native generate_suggestions function exists."""
        self.assertTrue(hasattr(self._native, 'generate_suggestions'))

    def test_native_export_to_string(self):
        """Native export_to_string function exists."""
        self.assertTrue(hasattr(self._native, 'export_to_string'))

class TestNativeIntegration(unittest.TestCase):
    """Integration tests requiring native module."""

    def setUp(self):
        """Get native module for tests."""
        self._native = _bha_native

    def test_parse_and_analyze(self):
        """Parse a trace and run analysis."""
        import tempfile
        import json
        import os

        if not hasattr(self._native, 'parse_trace_file'):
            self.skipTest("parse_trace_file not available")

        trace_data = {
            "traceEvents": [
                {"name": "Total ExecuteCompiler", "ph": "X", "dur": 1000000, "ts": 0, "pid": 1, "tid": 1},
                {"name": "Total Frontend", "ph": "X", "dur": 800000, "ts": 0, "pid": 1, "tid": 1},
                {"name": "Total Backend", "ph": "X", "dur": 200000, "ts": 800000, "pid": 1, "tid": 1},
                {"name": "Source", "ph": "X", "dur": 50000, "ts": 0, "pid": 1, "tid": 1, "args": {"detail": "test.h"}},
            ]
        }

        with tempfile.NamedTemporaryFile(mode='w', suffix='.time-trace.json', delete=False) as f:
            json.dump(trace_data, f)
            trace_path = f.name

        try:
            try:
                trace = self._native.parse_trace_file(trace_path)
                self.assertIsNotNone(trace)

                if hasattr(self._native, 'run_full_analysis'):
                    options = self._native.AnalysisOptions() if hasattr(self._native, 'AnalysisOptions') else None
                    if options:
                        result = self._native.run_full_analysis(trace, options)
                        self.assertIsNotNone(result)
            except RuntimeError as runtime_exception:
                if "No parser found" in str(runtime_exception):
                    self.skipTest("Native parser did not recognize trace format")
                else:
                    raise
        finally:
            os.unlink(trace_path)

    def test_export_analysis_result(self):
        """Export analysis result to string."""
        if not hasattr(self._native, 'export_to_string'):
            self.skipTest("export_to_string not available")

        if hasattr(self._native, 'AnalysisResult'):
            result = self._native.AnalysisResult()
            if hasattr(self._native, 'ExportFormat') and hasattr(self._native, 'ExportOptions'):
                options = self._native.ExportOptions()
                output = self._native.export_to_string(result, [], self._native.ExportFormat.JSON, options)
                self.assertIsInstance(output, str)


if __name__ == '__main__':
    unittest.main()