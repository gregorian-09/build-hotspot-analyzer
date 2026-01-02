"""
Tests for the BHA Python package.

These tests verify the pure Python functionality works correctly,
independent of the native C++ bindings.
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

# Add parent to path
sys.path.insert(0, str(Path(__file__).parent.parent))

class TestDuration(unittest.TestCase):
    """Tests for Duration type."""

    def test_from_nanoseconds(self):
        from bha.types import Duration
        d = Duration.from_nanoseconds(1_000_000_000)
        self.assertEqual(d.nanoseconds, 1_000_000_000)
        self.assertEqual(d.seconds, 1.0)

    def test_from_milliseconds(self):
        from bha.types import Duration
        d = Duration.from_milliseconds(1500)
        self.assertEqual(d.milliseconds, 1500.0)
        self.assertEqual(d.seconds, 1.5)

    def test_from_seconds(self):
        from bha.types import Duration
        d = Duration.from_seconds(2.5)
        self.assertEqual(d.seconds, 2.5)
        self.assertEqual(d.milliseconds, 2500.0)

    def test_comparison(self):
        from bha.types import Duration
        d1 = Duration.from_milliseconds(100)
        d2 = Duration.from_milliseconds(200)
        d3 = Duration.from_milliseconds(100)

        self.assertLess(d1, d2)
        self.assertGreater(d2, d1)
        self.assertEqual(d1, d3)
        self.assertLessEqual(d1, d3)
        self.assertGreaterEqual(d1, d3)

    def test_arithmetic(self):
        from bha.types import Duration
        d1 = Duration.from_milliseconds(100)
        d2 = Duration.from_milliseconds(50)

        result = d1 + d2
        self.assertEqual(result.milliseconds, 150.0)

        result = d1 - d2
        self.assertEqual(result.milliseconds, 50.0)

    def test_repr(self):
        from bha.types import Duration
        d = Duration.from_seconds(1.5)
        self.assertIn("1.5", repr(d))
        self.assertIn("s", repr(d))


class TestEnums(unittest.TestCase):
    """Tests for enum types."""

    def test_compiler_type(self):
        from bha.types import CompilerType
        self.assertEqual(CompilerType.CLANG.value, 1)
        self.assertEqual(CompilerType.GCC.value, 2)
        self.assertEqual(CompilerType.MSVC.value, 3)

    def test_suggestion_type(self):
        from bha.types import SuggestionType
        self.assertEqual(SuggestionType.PCH.value, "pch")
        self.assertEqual(SuggestionType.FORWARD_DECLARATION.value, "forward_declaration")

    def test_priority(self):
        from bha.types import Priority
        self.assertGreater(Priority.CRITICAL.value, Priority.HIGH.value)
        self.assertGreater(Priority.HIGH.value, Priority.MEDIUM.value)
        self.assertGreater(Priority.MEDIUM.value, Priority.LOW.value)

    def test_export_format(self):
        from bha.types import ExportFormat
        self.assertEqual(ExportFormat.JSON.value, "json")
        self.assertEqual(ExportFormat.HTML.value, "html")
        self.assertEqual(ExportFormat.CSV.value, "csv")


class TestIncludeInfo(unittest.TestCase):
    """Tests for IncludeInfo dataclass."""

    def test_creation(self):
        from bha.types import IncludeInfo, Duration
        info = IncludeInfo(
            header_path="/usr/include/vector",
            include_time=Duration.from_milliseconds(50),
            line_number=10,
            is_system=True
        )
        self.assertEqual(info.header_path, "/usr/include/vector")
        self.assertEqual(info.include_time.milliseconds, 50.0)
        self.assertTrue(info.is_system)

    def test_from_json(self):
        from bha.types import IncludeInfo
        data = {
            "header_path": "myheader.h",
            "dur": 1000,  # microseconds
            "line": 5,
            "is_system": False
        }
        info = IncludeInfo.from_json(data)
        self.assertEqual(info.header_path, "myheader.h")
        self.assertEqual(info.include_time.microseconds, 1000.0)
        self.assertEqual(info.line_number, 5)


class TestTemplateInstantiation(unittest.TestCase):
    """Tests for TemplateInstantiation dataclass."""

    def test_creation(self):
        from bha.types import TemplateInstantiation, Duration
        tmpl = TemplateInstantiation(
            template_name="std::vector<int>",
            instantiation_time=Duration.from_milliseconds(100),
            instantiation_count=50
        )
        self.assertEqual(tmpl.template_name, "std::vector<int>")
        self.assertEqual(tmpl.instantiation_count, 50)

    def test_from_json(self):
        from bha.types import TemplateInstantiation
        data = {
            "name": "std::map<std::string, int>",
            "dur": 5000,
            "count": 25
        }
        tmpl = TemplateInstantiation.from_json(data)
        self.assertIn("std::map", tmpl.template_name)
        self.assertEqual(tmpl.instantiation_count, 25)


class TestFileMetrics(unittest.TestCase):
    """Tests for FileMetrics dataclass."""

    def test_creation(self):
        from bha.types import FileMetrics
        metrics = FileMetrics(
            file_path="/src/main.cpp",
            compile_time_ms=1500.0,
            include_count=25,
            template_instantiation_count=100
        )
        self.assertEqual(metrics.file_path, "/src/main.cpp")
        self.assertEqual(metrics.compile_time_ms, 1500.0)

    def test_to_dict(self):
        from bha.types import FileMetrics
        metrics = FileMetrics(
            file_path="test.cpp",
            compile_time_ms=500.0,
            include_count=10
        )
        d = metrics.to_dict()
        self.assertEqual(d["file_path"], "test.cpp")
        self.assertEqual(d["compile_time_ms"], 500.0)
        self.assertEqual(d["include_count"], 10)


class TestAnalysisResult(unittest.TestCase):
    """Tests for AnalysisResult dataclass."""

    def test_empty_result(self):
        from bha.types import AnalysisResult
        result = AnalysisResult()
        self.assertEqual(result.file_count, 0)
        self.assertEqual(len(result.file_metrics), 0)

    def test_to_dict(self):
        from bha.types import AnalysisResult, FileMetrics, Duration
        result = AnalysisResult(
            file_count=2,
            total_compile_time=Duration.from_seconds(10),
            file_metrics=[
                FileMetrics(file_path="a.cpp", compile_time_ms=5000),
                FileMetrics(file_path="b.cpp", compile_time_ms=5000),
            ]
        )
        d = result.to_dict()
        self.assertEqual(d["bha_version"], "1.0.0")
        self.assertEqual(d["summary"]["file_count"], 2)
        self.assertEqual(len(d["file_metrics"]), 2)


class TestSuggestion(unittest.TestCase):
    """Tests for Suggestion dataclass."""

    def test_creation(self):
        from bha.types import Suggestion, SuggestionType, Priority, Confidence
        suggestion = Suggestion(
            type=SuggestionType.PCH,
            priority=Priority.HIGH,
            title="Use precompiled header",
            description="This header is included in 50 files",
            file_path="common.h",
            estimated_impact_ms=500.0,
            confidence=Confidence.HIGH
        )
        self.assertEqual(suggestion.type, SuggestionType.PCH)
        self.assertEqual(suggestion.priority, Priority.HIGH)
        self.assertEqual(suggestion.estimated_impact_ms, 500.0)

    def test_to_dict(self):
        from bha.types import Suggestion, SuggestionType, Priority, Confidence
        suggestion = Suggestion(
            type=SuggestionType.FORWARD_DECLARATION,
            priority=Priority.MEDIUM,
            title="Add forward declaration",
            description="Can avoid including heavy.h",
            confidence=Confidence.MEDIUM
        )
        d = suggestion.to_dict()
        self.assertEqual(d["type"], "forward_declaration")
        self.assertEqual(d["priority"], "MEDIUM")


class TestExportOptions(unittest.TestCase):
    """Tests for ExportOptions dataclass."""

    def test_defaults(self):
        from bha.types import ExportOptions, Priority
        opts = ExportOptions()
        self.assertTrue(opts.pretty_print)
        self.assertTrue(opts.include_metadata)
        self.assertEqual(opts.min_priority, Priority.LOW)

    def test_to_native(self):
        from bha.types import ExportOptions, Priority
        opts = ExportOptions(
            pretty_print=False,
            max_entries=100,
            min_priority=Priority.HIGH
        )
        native = opts.to_native()
        self.assertFalse(native["pretty_print"])
        self.assertEqual(native["max_entries"], 100)


class TestCompilationUnit(unittest.TestCase):
    """Tests for CompilationUnit dataclass."""

    def test_from_clang_trace_json(self):
        from bha.types import CompilationUnit
        trace_data = {
            "traceEvents": [
                {"name": "Total ExecuteCompiler", "dur": 5000000},
                {"name": "Total Frontend", "dur": 3000000},
                {"name": "Total Backend", "dur": 2000000},
                {"name": "Source", "dur": 100000, "args": {"detail": "vector"}},
                {"name": "Source", "dur": 50000, "args": {"detail": "string"}},
                {"name": "InstantiateClass", "dur": 200000, "args": {"detail": "std::vector<int>"}},
            ]
        }
        unit = CompilationUnit.from_json(trace_data, "test.cpp")
        self.assertEqual(unit.source_file, "test.cpp")
        self.assertEqual(unit.total_time.milliseconds, 5000.0)
        self.assertEqual(unit.frontend_time.milliseconds, 3000.0)
        self.assertEqual(len(unit.includes), 2)
        self.assertEqual(len(unit.templates), 1)


class TestAnalyzer(unittest.TestCase):
    """Tests for the Analyzer class."""

    def test_no_traces_error(self):
        from bha import Analyzer
        analyzer = Analyzer()
        with self.assertRaises(RuntimeError):
            analyzer.analyze()

    def test_add_nonexistent_file(self):
        from bha import Analyzer
        analyzer = Analyzer()
        with self.assertRaises(FileNotFoundError):
            analyzer.add_trace_file("/nonexistent/path/trace.json")

    def test_analyze_requires_first(self):
        from bha import Analyzer
        analyzer = Analyzer()
        with self.assertRaises(RuntimeError):
            analyzer.get_suggestions()


class TestModuleImports(unittest.TestCase):
    """Tests for module-level imports and exports."""

    def test_version(self):
        import bha
        self.assertEqual(bha.__version__, "1.0.0")

    def test_has_native_module(self):
        import bha
        result = bha.has_native_module()
        self.assertIsInstance(result, bool)

    def test_all_exports(self):
        import bha
        self.assertTrue(hasattr(bha, "Analyzer"))
        self.assertTrue(hasattr(bha, "Duration"))
        self.assertTrue(hasattr(bha, "Priority"))
        self.assertTrue(hasattr(bha, "Suggestion"))
        self.assertTrue(hasattr(bha, "ExportFormat"))
        self.assertTrue(hasattr(bha, "analyze_trace"))


class TestVisualization(unittest.TestCase):
    """Tests for visualization module."""

    def test_has_visualization_support(self):
        from bha.visualization import has_visualization_support
        result = has_visualization_support()
        self.assertIsInstance(result, bool)

    def test_visualizer_requires_matplotlib(self):
        from bha.visualization import has_visualization_support
        if not has_visualization_support():
            from bha.types import AnalysisResult
            from bha.visualization import BuildVisualizer
            result = AnalysisResult()
            with self.assertRaises(ImportError):
                BuildVisualizer(result)


class TestIntegration(unittest.TestCase):
    """Integration tests with sample data."""

    def test_analyze_sample_trace(self):
        """Test analyzing a sample trace file."""
        from bha import Analyzer
        from bha.types import ExportFormat

        trace_data = {
            "traceEvents": [
                {"name": "Total ExecuteCompiler", "dur": 2000000},
                {"name": "Total Frontend", "dur": 1500000},
                {"name": "Total Backend", "dur": 500000},
                {"name": "Source", "dur": 100000, "args": {"detail": "iostream"}},
            ]
        }

        with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
            json.dump(trace_data, f)
            trace_path = f.name

        try:
            analyzer = Analyzer()
            analyzer.add_trace_file(trace_path)
            result = analyzer.analyze()

            self.assertIsNotNone(result)
            self.assertEqual(result.total_compile_time.milliseconds, 2000.0)
        finally:
            import os
            os.unlink(trace_path)

    def test_export_json(self):
        """Test JSON export."""
        from bha.types import AnalysisResult, FileMetrics, Duration

        result = AnalysisResult(
            file_count=1,
            total_compile_time=Duration.from_seconds(5),
            file_metrics=[
                FileMetrics(file_path="main.cpp", compile_time_ms=5000)
            ]
        )

        output = json.dumps(result.to_dict(), indent=2)
        parsed = json.loads(output)

        self.assertEqual(parsed["bha_version"], "1.0.0")
        self.assertEqual(parsed["summary"]["file_count"], 1)


if __name__ == '__main__':
    unittest.main()