"""
Tests for bha.types module (pure Python types).
"""

import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).parent.parent))

from bha.types import MemoryMetrics, Duration

class TestMemoryMetrics(unittest.TestCase):
    """Tests for MemoryMetrics dataclass."""

    def test_memory_metrics_default_values(self):
        """MemoryMetrics initializes with zero values."""
        metrics = MemoryMetrics()
        self.assertEqual(metrics.peak_memory_bytes, 0)
        self.assertEqual(metrics.frontend_peak_bytes, 0)
        self.assertEqual(metrics.backend_peak_bytes, 0)
        self.assertEqual(metrics.max_stack_bytes, 0)
        self.assertEqual(metrics.parsing_bytes, 0)
        self.assertEqual(metrics.semantic_bytes, 0)
        self.assertEqual(metrics.codegen_bytes, 0)
        self.assertEqual(metrics.ggc_memory, 0)

    def test_memory_metrics_has_data_false(self):
        """has_data returns False for empty metrics."""
        metrics = MemoryMetrics()
        self.assertFalse(metrics.has_data())

    def test_memory_metrics_has_data_peak_memory(self):
        """has_data returns True when peak_memory_bytes is set."""
        metrics = MemoryMetrics(peak_memory_bytes=1024)
        self.assertTrue(metrics.has_data())

    def test_memory_metrics_has_data_frontend_peak(self):
        """has_data returns True when frontend_peak_bytes is set."""
        metrics = MemoryMetrics(frontend_peak_bytes=512)
        self.assertTrue(metrics.has_data())

    def test_memory_metrics_has_data_backend_peak(self):
        """has_data returns True when backend_peak_bytes is set."""
        metrics = MemoryMetrics(backend_peak_bytes=256)
        self.assertTrue(metrics.has_data())

    def test_memory_metrics_has_data_max_stack(self):
        """has_data returns True when max_stack_bytes is set."""
        metrics = MemoryMetrics(max_stack_bytes=128)
        self.assertTrue(metrics.has_data())

    def test_memory_metrics_all_fields(self):
        """All fields can be set and accessed."""
        metrics = MemoryMetrics(
            peak_memory_bytes=10240,
            frontend_peak_bytes=5120,
            backend_peak_bytes=2560,
            max_stack_bytes=1024,
            parsing_bytes=512,
            semantic_bytes=256,
            codegen_bytes=128,
            ggc_memory=64
        )
        self.assertEqual(metrics.peak_memory_bytes, 10240)
        self.assertEqual(metrics.frontend_peak_bytes, 5120)
        self.assertEqual(metrics.backend_peak_bytes, 2560)
        self.assertEqual(metrics.max_stack_bytes, 1024)
        self.assertEqual(metrics.parsing_bytes, 512)
        self.assertEqual(metrics.semantic_bytes, 256)
        self.assertEqual(metrics.codegen_bytes, 128)
        self.assertEqual(metrics.ggc_memory, 64)
        self.assertTrue(metrics.has_data())

class TestDuration(unittest.TestCase):
    """Tests for Duration class (ensuring it still works with memory features)."""

    def test_duration_milliseconds(self):
        """Duration conversions work correctly."""
        d = Duration.from_milliseconds(100)
        self.assertEqual(d.milliseconds, 100.0)

    def test_duration_addition(self):
        """Duration addition works."""
        d1 = Duration.from_milliseconds(100)
        d2 = Duration.from_milliseconds(50)
        d3 = d1 + d2
        self.assertEqual(d3.milliseconds, 150.0)

if __name__ == '__main__':
    unittest.main()
