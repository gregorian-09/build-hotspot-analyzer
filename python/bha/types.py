"""
BHA Type Definitions

This module defines all the data types used by BHA in pure Python.
These mirror the C++ types and can be populated from either the native
module or from JSON parsing.
"""

from dataclasses import dataclass, field
from enum import Enum
from typing import List, Optional, Dict, Any

class Duration:
    """
    Duration type representing time in nanoseconds.

    Provides convenient conversions to various time units.
    """

    def __init__(self, nanoseconds: int = 0):
        self._ns = nanoseconds

    @classmethod
    def from_nanoseconds(cls, ns: int) -> 'Duration':
        return cls(ns)

    @classmethod
    def from_microseconds(cls, us: float) -> 'Duration':
        return cls(int(us * 1000))

    @classmethod
    def from_milliseconds(cls, ms: float) -> 'Duration':
        return cls(int(ms * 1_000_000))

    @classmethod
    def from_seconds(cls, s: float) -> 'Duration':
        return cls(int(s * 1_000_000_000))

    @property
    def nanoseconds(self) -> int:
        return self._ns

    @property
    def microseconds(self) -> float:
        return self._ns / 1000.0

    @property
    def milliseconds(self) -> float:
        return self._ns / 1_000_000.0

    @property
    def seconds(self) -> float:
        return self._ns / 1_000_000_000.0

    def __repr__(self) -> str:
        if self._ns >= 1_000_000_000:
            return f"Duration({self.seconds:.3f}s)"
        elif self._ns >= 1_000_000:
            return f"Duration({self.milliseconds:.3f}ms)"
        elif self._ns >= 1000:
            return f"Duration({self.microseconds:.3f}us)"
        else:
            return f"Duration({self._ns}ns)"

    def __eq__(self, other: object) -> bool:
        if isinstance(other, Duration):
            return self._ns == other._ns
        return False

    def __lt__(self, other: 'Duration') -> bool:
        return self._ns < other._ns

    def __le__(self, other: 'Duration') -> bool:
        return self._ns <= other._ns

    def __gt__(self, other: 'Duration') -> bool:
        return self._ns > other._ns

    def __ge__(self, other: 'Duration') -> bool:
        return self._ns >= other._ns

    def __add__(self, other: 'Duration') -> 'Duration':
        return Duration(self._ns + other._ns)

    def __sub__(self, other: 'Duration') -> 'Duration':
        return Duration(self._ns - other._ns)


class CompilerType(Enum):
    """Supported compiler types."""
    UNKNOWN = 0
    CLANG = 1
    GCC = 2
    MSVC = 3
    INTEL = 4
    NVCC = 5


class SuggestionType(Enum):
    """Types of optimization suggestions."""
    PCH = "pch"
    FORWARD_DECLARATION = "forward_declaration"
    INCLUDE_OPTIMIZATION = "include_optimization"
    TEMPLATE_OPTIMIZATION = "template_optimization"
    HEADER_SPLIT = "header_split"
    PIMPL = "pimpl"
    UNITY_BUILD = "unity_build"
    MODULE_MIGRATION = "module_migration"
    CUSTOM = "custom"


class Priority(Enum):
    """Suggestion priority levels."""
    LOW = 1
    MEDIUM = 2
    HIGH = 3
    CRITICAL = 4


class Confidence(Enum):
    """Confidence level for suggestions."""
    LOW = 1
    MEDIUM = 2
    HIGH = 3


class ExportFormat(Enum):
    """Supported export formats."""
    JSON = "json"
    HTML = "html"
    CSV = "csv"
    SARIF = "sarif"
    MARKDOWN = "markdown"


@dataclass
class IncludeInfo:
    """Information about a single include directive."""
    header_path: str
    include_time: Duration = field(default_factory=Duration)
    line_number: int = 0
    is_system: bool = False
    is_direct: bool = True
    included_by: Optional[str] = None

    @classmethod
    def from_json(cls, data: Dict[str, Any]) -> 'IncludeInfo':
        return cls(
            header_path=data.get('header_path', data.get('path', '')),
            include_time=Duration.from_microseconds(data.get('dur', 0)),
            line_number=data.get('line', 0),
            is_system=data.get('is_system', False),
            is_direct=data.get('is_direct', True),
            included_by=data.get('included_by'),
        )


@dataclass
class TemplateInstantiation:
    """Information about a template instantiation."""
    template_name: str
    instantiation_time: Duration = field(default_factory=Duration)
    specialization: str = ""
    location_file: str = ""
    location_line: int = 0
    instantiation_count: int = 1

    @classmethod
    def from_json(cls, data: Dict[str, Any]) -> 'TemplateInstantiation':
        return cls(
            template_name=data.get('name', ''),
            instantiation_time=Duration.from_microseconds(data.get('dur', 0)),
            specialization=data.get('args', ''),
            location_file=data.get('file', ''),
            location_line=data.get('line', 0),
            instantiation_count=data.get('count', 1),
        )


@dataclass
class SymbolInfo:
    """Information about a symbol (function, class, etc.)."""
    name: str
    mangled_name: str = ""
    symbol_type: str = ""
    size_bytes: int = 0
    is_inline: bool = False
    is_template: bool = False
    definition_file: str = ""
    definition_line: int = 0


@dataclass
class CompilationUnit:
    """Represents a single compiled file with its trace data."""
    source_file: str
    compiler: CompilerType = CompilerType.UNKNOWN
    total_time: Duration = field(default_factory=Duration)
    frontend_time: Duration = field(default_factory=Duration)
    backend_time: Duration = field(default_factory=Duration)
    includes: List[IncludeInfo] = field(default_factory=list)
    templates: List[TemplateInstantiation] = field(default_factory=list)
    symbols: List[SymbolInfo] = field(default_factory=list)

    @classmethod
    def from_json(cls, data: Dict[str, Any], source_hint: str = "") -> 'CompilationUnit':
        """Parse a Clang -ftime-trace JSON file."""
        unit = cls(source_file=source_hint)

        trace_events = data.get('traceEvents', [])

        for event in trace_events:
            name = event.get('name', '')
            dur = event.get('dur', 0)  # microseconds

            if name == 'Total ExecuteCompiler':
                unit.total_time = Duration.from_microseconds(dur)
            elif name == 'Total Frontend':
                unit.frontend_time = Duration.from_microseconds(dur)
            elif name == 'Total Backend':
                unit.backend_time = Duration.from_microseconds(dur)
            elif name == 'Source':
                args = event.get('args', {})
                if 'detail' in args:
                    unit.includes.append(IncludeInfo(
                        header_path=args['detail'],
                        include_time=Duration.from_microseconds(dur),
                    ))
            elif name == 'InstantiateClass' or name == 'InstantiateFunction':
                args = event.get('args', {})
                unit.templates.append(TemplateInstantiation(
                    template_name=args.get('detail', ''),
                    instantiation_time=Duration.from_microseconds(dur),
                ))

        return unit


@dataclass
class BuildTrace:
    """Collection of compilation units forming a complete build."""
    units: List[CompilationUnit] = field(default_factory=list)
    total_build_time: Duration = field(default_factory=Duration)
    parallel_jobs: int = 1

    def add_unit(self, unit: CompilationUnit) -> None:
        self.units.append(unit)
        self.total_build_time = self.total_build_time + unit.total_time


@dataclass
class FileMetrics:
    """Metrics for a single file."""
    file_path: str
    compile_time_ms: float = 0.0
    include_count: int = 0
    template_instantiation_count: int = 0
    lines_of_code: int = 0
    include_depth: int = 0
    is_header: bool = False
    includers: List[str] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return {
            'file_path': self.file_path,
            'compile_time_ms': self.compile_time_ms,
            'include_count': self.include_count,
            'template_instantiation_count': self.template_instantiation_count,
            'lines_of_code': self.lines_of_code,
            'include_depth': self.include_depth,
            'is_header': self.is_header,
            'includers': self.includers,
        }


@dataclass
class IncludeMetrics:
    """Aggregate include metrics."""
    total_includes: int = 0
    unique_includes: int = 0
    max_depth: int = 0
    total_include_time: Duration = field(default_factory=Duration)
    most_included: List[tuple] = field(default_factory=list)  # (path, count)
    slowest_includes: List[tuple] = field(default_factory=list)  # (path, time_ms)

    def to_dict(self) -> Dict[str, Any]:
        return {
            'total_includes': self.total_includes,
            'unique_includes': self.unique_includes,
            'max_depth': self.max_depth,
            'total_include_time_ms': self.total_include_time.milliseconds,
            'most_included': [{'path': p, 'count': c} for p, c in self.most_included],
            'slowest_includes': [{'path': p, 'time_ms': t} for p, t in self.slowest_includes],
        }


@dataclass
class TemplateMetrics:
    """Aggregate template instantiation metrics."""
    total_instantiations: int = 0
    unique_templates: int = 0
    total_template_time: Duration = field(default_factory=Duration)
    most_instantiated: List[tuple] = field(default_factory=list)  # (name, count)
    slowest_templates: List[tuple] = field(default_factory=list)  # (name, time_ms)

    def to_dict(self) -> Dict[str, Any]:
        return {
            'total_instantiations': self.total_instantiations,
            'unique_templates': self.unique_templates,
            'total_template_time_ms': self.total_template_time.milliseconds,
            'most_instantiated': [{'name': n, 'count': c} for n, c in self.most_instantiated],
            'slowest_templates': [{'name': n, 'time_ms': t} for n, t in self.slowest_templates],
        }


@dataclass
class SymbolMetrics:
    """Aggregate symbol metrics."""
    total_symbols: int = 0
    inline_symbols: int = 0
    template_symbols: int = 0
    total_symbol_size: int = 0

    def to_dict(self) -> Dict[str, Any]:
        return {
            'total_symbols': self.total_symbols,
            'inline_symbols': self.inline_symbols,
            'template_symbols': self.template_symbols,
            'total_symbol_size': self.total_symbol_size,
        }


@dataclass
class DependencyMetrics:
    """Aggregate dependency metrics."""
    total_dependencies: int = 0
    circular_dependencies: int = 0
    max_dependency_depth: int = 0
    strongly_connected_components: int = 0

    def to_dict(self) -> Dict[str, Any]:
        return {
            'total_dependencies': self.total_dependencies,
            'circular_dependencies': self.circular_dependencies,
            'max_dependency_depth': self.max_dependency_depth,
            'strongly_connected_components': self.strongly_connected_components,
        }


@dataclass
class AnalysisResult:
    """Complete analysis result containing all metrics."""
    file_metrics: List[FileMetrics] = field(default_factory=list)
    include_metrics: IncludeMetrics = field(default_factory=IncludeMetrics)
    template_metrics: TemplateMetrics = field(default_factory=TemplateMetrics)
    symbol_metrics: SymbolMetrics = field(default_factory=SymbolMetrics)
    dependency_metrics: DependencyMetrics = field(default_factory=DependencyMetrics)
    total_compile_time: Duration = field(default_factory=Duration)
    file_count: int = 0

    @classmethod
    def from_compilation_units(cls, units: List[CompilationUnit]) -> 'AnalysisResult':
        """Create analysis result from compilation units."""
        result = cls()
        result.file_count = len(units)

        include_counts: Dict[str, int] = {}
        include_times: Dict[str, float] = {}
        template_counts: Dict[str, int] = {}
        template_times: Dict[str, float] = {}

        for unit in units:
            result.total_compile_time = result.total_compile_time + unit.total_time

            # File metrics
            result.file_metrics.append(FileMetrics(
                file_path=unit.source_file,
                compile_time_ms=unit.total_time.milliseconds,
                include_count=len(unit.includes),
                template_instantiation_count=len(unit.templates),
            ))

            # Aggregate includes
            for inc in unit.includes:
                path = inc.header_path
                include_counts[path] = include_counts.get(path, 0) + 1
                include_times[path] = include_times.get(path, 0) + inc.include_time.milliseconds

            # Aggregate templates
            for tmpl in unit.templates:
                name = tmpl.template_name
                template_counts[name] = template_counts.get(name, 0) + 1
                template_times[name] = template_times.get(name, 0) + tmpl.instantiation_time.milliseconds

        # Include metrics
        result.include_metrics.total_includes = sum(include_counts.values())
        result.include_metrics.unique_includes = len(include_counts)
        result.include_metrics.most_included = sorted(
            include_counts.items(), key=lambda x: x[1], reverse=True
        )[:10]
        result.include_metrics.slowest_includes = sorted(
            include_times.items(), key=lambda x: x[1], reverse=True
        )[:10]

        # Template metrics
        result.template_metrics.total_instantiations = sum(template_counts.values())
        result.template_metrics.unique_templates = len(template_counts)
        result.template_metrics.most_instantiated = sorted(
            template_counts.items(), key=lambda x: x[1], reverse=True
        )[:10]
        result.template_metrics.slowest_templates = sorted(
            template_times.items(), key=lambda x: x[1], reverse=True
        )[:10]

        return result

    @classmethod
    def from_native(cls, native_result: Any) -> 'AnalysisResult':
        """Create from native C++ result object."""
        result = cls()
        # Map native object fields to Python types
        if hasattr(native_result, 'file_metrics'):
            result.file_metrics = [
                FileMetrics(
                    file_path=fm.file_path,
                    compile_time_ms=fm.compile_time_ms,
                    include_count=fm.include_count,
                    template_instantiation_count=getattr(fm, 'template_instantiation_count', 0),
                )
                for fm in native_result.file_metrics
            ]
        return result

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON export."""
        return {
            'bha_version': '1.0.0',
            'summary': {
                'total_compile_time_ms': self.total_compile_time.milliseconds,
                'file_count': self.file_count,
            },
            'file_metrics': [fm.to_dict() for fm in self.file_metrics],
            'include_metrics': self.include_metrics.to_dict(),
            'template_metrics': self.template_metrics.to_dict(),
            'symbol_metrics': self.symbol_metrics.to_dict(),
            'dependency_metrics': self.dependency_metrics.to_dict(),
        }


@dataclass
class Suggestion:
    """An optimization suggestion."""
    type: SuggestionType
    priority: Priority
    title: str
    description: str
    file_path: str = ""
    line_number: int = 0
    estimated_impact_ms: float = 0.0
    confidence: Confidence = Confidence.MEDIUM
    affected_files: List[str] = field(default_factory=list)
    code_changes: List[Dict[str, str]] = field(default_factory=list)

    @classmethod
    def from_native(cls, native_suggestion: Any) -> 'Suggestion':
        """Create from native C++ suggestion object."""
        return cls(
            type=SuggestionType(native_suggestion.type) if hasattr(native_suggestion, 'type') else SuggestionType.CUSTOM,
            priority=Priority(native_suggestion.priority) if hasattr(native_suggestion, 'priority') else Priority.MEDIUM,
            title=getattr(native_suggestion, 'title', ''),
            description=getattr(native_suggestion, 'description', ''),
            file_path=getattr(native_suggestion, 'file_path', ''),
            line_number=getattr(native_suggestion, 'line_number', 0),
            estimated_impact_ms=getattr(native_suggestion, 'estimated_impact_ms', 0.0),
            confidence=Confidence(native_suggestion.confidence) if hasattr(native_suggestion, 'confidence') else Confidence.MEDIUM,
        )

    def to_dict(self) -> Dict[str, Any]:
        return {
            'type': self.type.value,
            'priority': self.priority.name,
            'title': self.title,
            'description': self.description,
            'file_path': self.file_path,
            'line_number': self.line_number,
            'estimated_impact_ms': self.estimated_impact_ms,
            'confidence': self.confidence.name,
            'affected_files': self.affected_files,
            'code_changes': self.code_changes,
        }


@dataclass
class ExportOptions:
    """Configuration options for export."""
    pretty_print: bool = True
    include_metadata: bool = True
    include_suggestions: bool = True
    include_raw_data: bool = False
    min_priority: Priority = Priority.LOW
    max_entries: int = 0  # 0 = unlimited

    def to_native(self) -> Dict[str, Any]:
        """Convert to dict for native module."""
        return {
            'pretty_print': self.pretty_print,
            'include_metadata': self.include_metadata,
            'include_suggestions': self.include_suggestions,
            'include_raw_data': self.include_raw_data,
            'min_priority': self.min_priority.value,
            'max_entries': self.max_entries,
        }