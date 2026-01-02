"""
BHA Visualization Module

Provides matplotlib-based visualization for build analysis results.
Supports various chart types for understanding build performance.
"""

import os
from typing import List, Optional, Tuple

try:
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    from matplotlib.figure import Figure
    from matplotlib.axes import Axes
    _HAS_MATPLOTLIB = True
except ImportError:
    _HAS_MATPLOTLIB = False
    plt = None
    mpatches = None
    Figure = None
    Axes = None


try:
    import numpy as np
    _HAS_NUMPY = True
except ImportError:
    _HAS_NUMPY = False
    np = None

from .types import (
    AnalysisResult,
    Suggestion,
    Priority,
)

def has_visualization_support() -> bool:
    """Check if visualization dependencies are available."""
    return _HAS_MATPLOTLIB


def _check_matplotlib():
    """Raise error if matplotlib is not available."""
    if not _HAS_MATPLOTLIB:
        raise ImportError(
            "Visualization requires matplotlib. Install with: pip install matplotlib"
        )


class BuildVisualizer:
    """
    Visualizer for build analysis results.

    Creates various charts and diagrams to help understand
    build performance characteristics.

    Example:
        >>> from bha import Analyzer
        >>> from bha.visualization import BuildVisualizer
        >>> analyzer = Analyzer()
        >>> analyzer.add_trace_file("trace.json")
        >>> result = analyzer.analyze()
        >>> viz = BuildVisualizer(result)
        >>> viz.plot_compile_times()
        >>> viz.save("compile_times.png")
    """

    # Color scheme for consistent styling
    COLORS = {
        'primary': '#2196F3',
        'secondary': '#4CAF50',
        'warning': '#FF9800',
        'danger': '#F44336',
        'info': '#00BCD4',
        'light': '#E0E0E0',
        'dark': '#424242',
    }

    PRIORITY_COLORS = {
        Priority.LOW: '#4CAF50',
        Priority.MEDIUM: '#FF9800',
        Priority.HIGH: '#F44336',
        Priority.CRITICAL: '#9C27B0',
    }

    def __init__(
            self,
            result: AnalysisResult,
            suggestions: Optional[List[Suggestion]] = None,
            figsize: Tuple[int, int] = (12, 8),
            style: str = 'seaborn-v0_8-whitegrid'
    ):
        """
        Initialize the visualizer.

        Args:
            result: Analysis result to visualize
            suggestions: Optional list of suggestions to include
            figsize: Default figure size (width, height) in inches
            style: Matplotlib style to use
        """
        _check_matplotlib()
        self.result = result
        self.suggestions = suggestions or []
        self.figsize = figsize
        self.style = style
        self._current_fig: Optional[Figure] = None

    def _setup_style(self):
        """Apply consistent styling."""
        try:
            plt.style.use(self.style)
        except OSError:
            plt.style.use('ggplot')

    def plot_compile_times(
            self,
            top_n: int = 20,
            horizontal: bool = True,
            show_threshold: bool = True,
            threshold_ms: float = 1000.0,
    ) -> Figure:
        """
        Plot compilation times per file.

        Args:
            top_n: Number of top files to show
            horizontal: Use horizontal bar chart
            show_threshold: Show a threshold line
            threshold_ms: Threshold value in milliseconds

        Returns:
            matplotlib Figure object
        """
        self._setup_style()

        # Sort files by compile time
        sorted_metrics = sorted(
            self.result.file_metrics,
            key=lambda x: x.compile_time_ms,
            reverse=True
        )[:top_n]

        if not sorted_metrics:
            fig, ax = plt.subplots(figsize=self.figsize)
            ax.text(0.5, 0.5, 'No data available', ha='center', va='center')
            self._current_fig = fig
            return fig

        files = [os.path.basename(m.file_path) for m in sorted_metrics]
        times = [m.compile_time_ms for m in sorted_metrics]

        fig, ax = plt.subplots(figsize=self.figsize)

        # Color bars based on compile time
        colors = []
        for t in times:
            if t >= threshold_ms * 2:
                colors.append(self.COLORS['danger'])
            elif t >= threshold_ms:
                colors.append(self.COLORS['warning'])
            else:
                colors.append(self.COLORS['primary'])

        if horizontal:
            y_pos = range(len(files))
            bars = ax.barh(y_pos, times, color=colors)
            ax.set_yticks(y_pos)
            ax.set_yticklabels(files)
            ax.set_xlabel('Compile Time (ms)')
            ax.set_ylabel('Source File')
            ax.invert_yaxis()  # Largest at top

            if show_threshold:
                ax.axvline(x=threshold_ms, color=self.COLORS['danger'],
                           linestyle='--', label=f'Threshold ({threshold_ms}ms)')
                ax.legend()
        else:
            x_pos = range(len(files))
            ax.bar(x_pos, times, color=colors)
            ax.set_xticks(x_pos)
            ax.set_xticklabels(files, rotation=45, ha='right')
            ax.set_ylabel('Compile Time (ms)')
            ax.set_xlabel('Source File')

            if show_threshold:
                ax.axhline(y=threshold_ms, color=self.COLORS['danger'],
                           linestyle='--', label=f'Threshold ({threshold_ms}ms)')
                ax.legend()

        ax.set_title(f'Top {len(files)} Files by Compile Time')
        plt.tight_layout()

        self._current_fig = fig
        return fig

    def plot_include_analysis(self, top_n: int = 15) -> Figure:
        """
        Plot include file analysis.

        Shows most frequently included files and their impact.

        Args:
            top_n: Number of top includes to show

        Returns:
            matplotlib Figure object
        """
        self._setup_style()

        fig, axes = plt.subplots(1, 2, figsize=(self.figsize[0], self.figsize[1]))

        # Most included files
        ax1 = axes[0]
        most_included = self.result.include_metrics.most_included[:top_n]
        if most_included:
            files = [os.path.basename(p) for p, _ in most_included]
            counts = [c for _, c in most_included]

            y_pos = range(len(files))
            ax1.barh(y_pos, counts, color=self.COLORS['primary'])
            ax1.set_yticks(y_pos)
            ax1.set_yticklabels(files)
            ax1.set_xlabel('Include Count')
            ax1.set_title('Most Frequently Included')
            ax1.invert_yaxis()
        else:
            ax1.text(0.5, 0.5, 'No data', ha='center', va='center')
            ax1.set_title('Most Frequently Included')

        # Slowest includes
        ax2 = axes[1]
        slowest = self.result.include_metrics.slowest_includes[:top_n]
        if slowest:
            files = [os.path.basename(p) for p, _ in slowest]
            times = [t for _, t in slowest]

            y_pos = range(len(files))
            ax2.barh(y_pos, times, color=self.COLORS['warning'])
            ax2.set_yticks(y_pos)
            ax2.set_yticklabels(files)
            ax2.set_xlabel('Total Include Time (ms)')
            ax2.set_title('Slowest Includes')
            ax2.invert_yaxis()
        else:
            ax2.text(0.5, 0.5, 'No data', ha='center', va='center')
            ax2.set_title('Slowest Includes')

        plt.tight_layout()
        self._current_fig = fig
        return fig

    def plot_template_analysis(self, top_n: int = 15) -> Figure:
        """
        Plot template instantiation analysis.

        Shows most instantiated templates and their costs.

        Args:
            top_n: Number of top templates to show

        Returns:
            matplotlib Figure object
        """
        self._setup_style()

        fig, axes = plt.subplots(1, 2, figsize=(self.figsize[0], self.figsize[1]))

        # Most instantiated templates
        ax1 = axes[0]
        most_instantiated = self.result.template_metrics.most_instantiated[:top_n]
        if most_instantiated:
            # Truncate long template names
            names = [n[:40] + '...' if len(n) > 40 else n for n, _ in most_instantiated]
            counts = [c for _, c in most_instantiated]

            y_pos = range(len(names))
            ax1.barh(y_pos, counts, color=self.COLORS['secondary'])
            ax1.set_yticks(y_pos)
            ax1.set_yticklabels(names, fontsize=8)
            ax1.set_xlabel('Instantiation Count')
            ax1.set_title('Most Instantiated Templates')
            ax1.invert_yaxis()
        else:
            ax1.text(0.5, 0.5, 'No data', ha='center', va='center')
            ax1.set_title('Most Instantiated Templates')

        # Slowest templates
        ax2 = axes[1]
        slowest = self.result.template_metrics.slowest_templates[:top_n]
        if slowest:
            names = [n[:40] + '...' if len(n) > 40 else n for n, _ in slowest]
            times = [t for _, t in slowest]

            y_pos = range(len(names))
            ax2.barh(y_pos, times, color=self.COLORS['danger'])
            ax2.set_yticks(y_pos)
            ax2.set_yticklabels(names, fontsize=8)
            ax2.set_xlabel('Total Instantiation Time (ms)')
            ax2.set_title('Slowest Templates')
            ax2.invert_yaxis()
        else:
            ax2.text(0.5, 0.5, 'No data', ha='center', va='center')
            ax2.set_title('Slowest Templates')

        plt.tight_layout()
        self._current_fig = fig
        return fig

    def plot_time_breakdown(self) -> Figure:
        """
        Plot overall time breakdown as pie chart.

        Shows distribution of time across different build phases.

        Returns:
            matplotlib Figure object
        """
        self._setup_style()

        fig, ax = plt.subplots(figsize=(10, 8))

        # Calculate time breakdown
        total_include_time = self.result.include_metrics.total_include_time.milliseconds
        total_template_time = self.result.template_metrics.total_template_time.milliseconds
        total_compile_time = self.result.total_compile_time.milliseconds

        # Estimate other time (parsing, codegen, etc.)
        other_time = max(0, total_compile_time - total_include_time - total_template_time)

        labels = ['Include Processing', 'Template Instantiation', 'Other (Parse/Codegen)']
        sizes = [total_include_time, total_template_time, other_time]
        colors = [self.COLORS['primary'], self.COLORS['secondary'], self.COLORS['info']]
        explode = (0.05, 0.05, 0)

        # Filter out zero values
        non_zero = [(l, s, c, e) for l, s, c, e in zip(labels, sizes, colors, explode) if s > 0]
        if non_zero:
            labels, sizes, colors, explode = zip(*non_zero)
            ax.pie(sizes, explode=explode, labels=labels, colors=colors,
                   autopct='%1.1f%%', shadow=True, startangle=90)
        else:
            ax.text(0.5, 0.5, 'No timing data available', ha='center', va='center')

        ax.set_title('Build Time Breakdown')
        ax.axis('equal')

        self._current_fig = fig
        return fig

    def plot_suggestions_by_priority(self) -> Figure:
        """
        Plot suggestions grouped by priority.

        Shows count of suggestions at each priority level.

        Returns:
            matplotlib Figure object
        """
        self._setup_style()

        fig, ax = plt.subplots(figsize=(10, 6))

        if not self.suggestions:
            ax.text(0.5, 0.5, 'No suggestions available', ha='center', va='center')
            ax.set_title('Suggestions by Priority')
            self._current_fig = fig
            return fig

        # Count by priority
        priority_counts = {}
        for s in self.suggestions:
            priority_counts[s.priority] = priority_counts.get(s.priority, 0) + 1

        priorities = [Priority.CRITICAL, Priority.HIGH, Priority.MEDIUM, Priority.LOW]
        counts = [priority_counts.get(p, 0) for p in priorities]
        colors = [self.PRIORITY_COLORS[p] for p in priorities]
        labels = [p.name for p in priorities]

        bars = ax.bar(labels, counts, color=colors)

        # Add count labels on bars
        for bar, count in zip(bars, counts):
            if count > 0:
                ax.annotate(str(count),
                            xy=(bar.get_x() + bar.get_width() / 2, bar.get_height()),
                            ha='center', va='bottom')

        ax.set_xlabel('Priority')
        ax.set_ylabel('Number of Suggestions')
        ax.set_title('Optimization Suggestions by Priority')

        self._current_fig = fig
        return fig

    def plot_suggestions_impact(self, top_n: int = 15) -> Figure:
        """
        Plot suggestions by estimated impact.

        Shows potential time savings for each suggestion.

        Args:
            top_n: Number of top suggestions to show

        Returns:
            matplotlib Figure object
        """
        self._setup_style()

        fig, ax = plt.subplots(figsize=self.figsize)

        if not self.suggestions:
            ax.text(0.5, 0.5, 'No suggestions available', ha='center', va='center')
            ax.set_title('Suggestions by Impact')
            self._current_fig = fig
            return fig

        # Sort by estimated impact
        sorted_suggestions = sorted(
            self.suggestions,
            key=lambda x: x.estimated_impact_ms,
            reverse=True
        )[:top_n]

        titles = [s.title[:50] + '...' if len(s.title) > 50 else s.title
                  for s in sorted_suggestions]
        impacts = [s.estimated_impact_ms for s in sorted_suggestions]
        colors = [self.PRIORITY_COLORS[s.priority] for s in sorted_suggestions]

        y_pos = range(len(titles))
        ax.barh(y_pos, impacts, color=colors)
        ax.set_yticks(y_pos)
        ax.set_yticklabels(titles, fontsize=9)
        ax.set_xlabel('Estimated Time Savings (ms)')
        ax.set_title(f'Top {len(titles)} Suggestions by Impact')
        ax.invert_yaxis()

        # Add legend for priorities
        legend_patches = [
            mpatches.Patch(color=color, label=priority.name)
            for priority, color in self.PRIORITY_COLORS.items()
        ]
        ax.legend(handles=legend_patches, loc='lower right')

        plt.tight_layout()
        self._current_fig = fig
        return fig

    def plot_file_heatmap(self) -> Figure:
        """
        Plot a heatmap of file metrics.

        Shows compile time, include count, and template count
        for each file as a heatmap.

        Returns:
            matplotlib Figure object
        """
        _check_matplotlib()
        if not _HAS_NUMPY:
            raise ImportError(
                "Heatmap requires numpy. Install with: pip install numpy"
            )

        self._setup_style()

        if not self.result.file_metrics:
            fig, ax = plt.subplots(figsize=self.figsize)
            ax.text(0.5, 0.5, 'No file metrics available', ha='center', va='center')
            self._current_fig = fig
            return fig

        # Take top 20 files by compile time
        sorted_metrics = sorted(
            self.result.file_metrics,
            key=lambda x: x.compile_time_ms,
            reverse=True
        )[:20]

        files = [os.path.basename(m.file_path) for m in sorted_metrics]
        metrics = ['Compile Time', 'Include Count', 'Template Count']

        # Normalize data to 0-1 range for each metric
        data = np.array([
            [m.compile_time_ms for m in sorted_metrics],
            [m.include_count for m in sorted_metrics],
            [m.template_instantiation_count for m in sorted_metrics],
        ]).T

        # Normalize each column
        for i in range(data.shape[1]):
            col_max = data[:, i].max()
            if col_max > 0:
                data[:, i] = data[:, i] / col_max

        fig, ax = plt.subplots(figsize=(10, 12))
        im = ax.imshow(data, cmap='YlOrRd', aspect='auto')

        ax.set_xticks(range(len(metrics)))
        ax.set_xticklabels(metrics)
        ax.set_yticks(range(len(files)))
        ax.set_yticklabels(files)

        plt.colorbar(im, label='Normalized Value')
        ax.set_title('File Metrics Heatmap')

        plt.tight_layout()
        self._current_fig = fig
        return fig

    def create_dashboard(self) -> Figure:
        """
        Create a comprehensive dashboard with multiple plots.

        Returns:
            matplotlib Figure with multiple subplots
        """
        self._setup_style()

        fig = plt.figure(figsize=(16, 12))

        # Compile times (top left)
        ax1 = fig.add_subplot(2, 2, 1)
        sorted_metrics = sorted(
            self.result.file_metrics,
            key=lambda x: x.compile_time_ms,
            reverse=True
        )[:10]
        if sorted_metrics:
            files = [os.path.basename(m.file_path)[:20] for m in sorted_metrics]
            times = [m.compile_time_ms for m in sorted_metrics]
            ax1.barh(range(len(files)), times, color=self.COLORS['primary'])
            ax1.set_yticks(range(len(files)))
            ax1.set_yticklabels(files)
            ax1.set_xlabel('ms')
            ax1.invert_yaxis()
        ax1.set_title('Top 10 Compile Times')

        # Time breakdown (top right)
        ax2 = fig.add_subplot(2, 2, 2)
        total_inc = self.result.include_metrics.total_include_time.milliseconds
        total_tmpl = self.result.template_metrics.total_template_time.milliseconds
        total = self.result.total_compile_time.milliseconds
        other = max(0, total - total_inc - total_tmpl)
        sizes = [total_inc, total_tmpl, other]
        if sum(sizes) > 0:
            ax2.pie(sizes, labels=['Includes', 'Templates', 'Other'],
                    colors=[self.COLORS['primary'], self.COLORS['secondary'], self.COLORS['info']],
                    autopct='%1.1f%%')
        ax2.set_title('Time Breakdown')

        # Include analysis (bottom left)
        ax3 = fig.add_subplot(2, 2, 3)
        most_included = self.result.include_metrics.most_included[:10]
        if most_included:
            files = [os.path.basename(p)[:20] for p, _ in most_included]
            counts = [c for _, c in most_included]
            ax3.barh(range(len(files)), counts, color=self.COLORS['warning'])
            ax3.set_yticks(range(len(files)))
            ax3.set_yticklabels(files)
            ax3.set_xlabel('Count')
            ax3.invert_yaxis()
        ax3.set_title('Most Included Files')

        # Suggestions by priority (bottom right)
        ax4 = fig.add_subplot(2, 2, 4)
        if self.suggestions:
            priority_counts = {}
            for s in self.suggestions:
                priority_counts[s.priority] = priority_counts.get(s.priority, 0) + 1
            priorities = [Priority.CRITICAL, Priority.HIGH, Priority.MEDIUM, Priority.LOW]
            counts = [priority_counts.get(p, 0) for p in priorities]
            colors = [self.PRIORITY_COLORS[p] for p in priorities]
            ax4.bar([p.name for p in priorities], counts, color=colors)
            ax4.set_ylabel('Count')
        else:
            ax4.text(0.5, 0.5, 'No suggestions', ha='center', va='center')
        ax4.set_title('Suggestions by Priority')

        plt.suptitle('BHA Build Analysis Dashboard', fontsize=14, fontweight='bold')
        plt.tight_layout()

        self._current_fig = fig
        return fig

    def save(
            self,
            path: str,
            dpi: int = 150,
            bbox_inches: str = 'tight',
            transparent: bool = False,
    ) -> None:
        """
        Save the current figure to a file.

        Args:
            path: Output file path (supports .png, .pdf, .svg, etc.)
            dpi: Resolution in dots per inch
            bbox_inches: Bounding box setting
            transparent: Whether to use transparent background
        """
        if self._current_fig is None:
            raise RuntimeError("No figure to save. Call a plot method first.")

        self._current_fig.savefig(
            path,
            dpi=dpi,
            bbox_inches=bbox_inches,
            transparent=transparent,
        )

    def show(self) -> None:
        """Display the current figure interactively."""
        _check_matplotlib()
        plt.show()


def quick_plot(result: AnalysisResult, output_path: Optional[str] = None) -> Figure:
    """
    Convenience function to create a quick dashboard.

    Args:
        result: Analysis result to visualize
        output_path: Optional path to save the figure

    Returns:
        matplotlib Figure object
    """
    viz = BuildVisualizer(result)
    fig = viz.create_dashboard()

    if output_path:
        viz.save(output_path)

    return fig


def plot_compile_times(
        result: AnalysisResult,
        top_n: int = 20,
        output_path: Optional[str] = None
) -> Figure:
    """
    Convenience function to plot compile times.

    Args:
        result: Analysis result
        top_n: Number of top files to show
        output_path: Optional path to save the figure

    Returns:
        matplotlib Figure object
    """
    viz = BuildVisualizer(result)
    fig = viz.plot_compile_times(top_n=top_n)

    if output_path:
        viz.save(output_path)

    return fig