import * as vscode from 'vscode';
import * as crypto from 'node:crypto';
import * as fs from 'node:fs';
import * as path from 'node:path';
import {
    LanguageClient,
    LanguageClientOptions,
    RevealOutputChannelOn,
    ServerOptions,
    Trace,
    TransportKind
} from 'vscode-languageclient/node';

// ============================================================================
// Configuration Constants
// ============================================================================

const CONFIG = {
    /** Delay before auto-analyze runs after activation (ms) */
    AUTO_ANALYZE_DELAY_MS: 2000,
    /** Default server executable name */
    DEFAULT_SERVER_PATH: 'bha-lsp',
    /** Minimum confidence threshold for auto-applicable suggestions (0-1) */
    MIN_AUTO_APPLY_CONFIDENCE: 0.8,
    /** Maximum number of suggestions to display in quick pick */
    MAX_QUICK_PICK_ITEMS: 100,
    /** Timeout for LSP requests (ms) */
    REQUEST_TIMEOUT_MS: 30000,
    /** Webview panel column */
    WEBVIEW_COLUMN: vscode.ViewColumn.Two,
} as const;

const PRIORITY_LABELS = ['High', 'Medium', 'Low'] as const;
const PRIORITY_CLASSES = ['high', 'medium', 'low'] as const;

// ============================================================================
// Interfaces
// ============================================================================

interface Range {
    start: { line: number; character: number };
    end: { line: number; character: number };
}

interface Suggestion {
    id: string;
    type: number;
    title: string;
    description: string;
    priority: number;
    confidence: number;
    autoApplicable: boolean;
    applicationMode?: string;
    applicationSummary?: string;
    applicationGuidance?: string;
    autoApplyBlockedReason?: string;
    refactorClassName?: string;
    compileCommandsPath?: string;
    targetUri?: string;
    range?: Range;
    estimatedImpact: {
        timeSavedMs: number;
        percentage: number;
        filesAffected: number;
    };
    estimatedSavingsEvidence?: string;
}

interface AnalysisResult {
    analysisId?: string;
    suggestions: Suggestion[];
    baselineMetrics?: {
        totalDurationMs: number;
        filesCompiled: number;
    };
    buildTiming?: {
        totalBuildTimeMs?: number;
        source?: string;
    };
    filesAnalyzed?: number;
}

interface SuggestionTextEdit {
    file: string;
    startLine: number;
    startCol: number;
    endLine: number;
    endCol: number;
    newText: string;
}

interface SuggestionDetails extends Suggestion {
    rationale?: string;
    filesToCreate?: string[];
    filesToModify?: string[];
    dependencies?: string[];
    textEdits?: SuggestionTextEdit[];
}

interface RecordBuildResult {
    success: boolean;
    buildSystem?: string;
    buildType?: string;
    compiler?: string;
    cCompiler?: string;
    cxxCompiler?: string;
    parallelJobs?: number;
    buildDir?: string | null;
    traceOutputDir?: string | null;
    traceFiles?: string[];
    traceFileCount?: number;
    memoryFiles?: string[];
    memoryFileCount?: number;
    buildTimeMs?: number;
    output?: string;
}

interface ApplyResult {
    success: boolean;
    changedFiles: string[];
    errors: Array<{ message: string }>;
    backupId?: string;
    buildValidation?: {
        requested?: boolean;
        ran?: boolean;
        success?: boolean;
        errorCount?: number;
    };
    rollback?: {
        attempted?: boolean;
        success?: boolean;
        reason?: string;
        restoredFiles?: string[];
        errors?: Array<{ message: string }>;
    };
    trustLoop?: {
        available?: boolean;
        reason?: string;
        predictedSavingsMs?: number;
        actualSavingsMs?: number;
        predictionDeltaMs?: number;
        predictionErrorPercent?: number;
        baselineBuildMs?: number;
        baselineSource?: string;
        rebuildBuildMs?: number;
        actualSavingsPercent?: number;
        status?: string;
    };
}

interface ApplyAllResult {
    success: boolean;
    appliedCount: number;
    skippedCount: number;
    failedCount: number;
    appliedSuggestionIds?: string[];
    backupId?: string;
    errors: Array<{ suggestionId: string; message: string }>;
    buildValidation?: {
        requested?: boolean;
        ran?: boolean;
        success?: boolean;
        errorCount?: number;
    };
    rollback?: {
        attempted?: boolean;
        success?: boolean;
        reason?: string;
        restoredFiles?: string[];
        errors?: Array<{ message: string }>;
    };
    trustLoop?: {
        available?: boolean;
        reason?: string;
        predictedSavingsMs?: number;
        actualSavingsMs?: number;
        predictionDeltaMs?: number;
        predictionErrorPercent?: number;
        baselineBuildMs?: number;
        baselineSource?: string;
        rebuildBuildMs?: number;
        actualSavingsPercent?: number;
        status?: string;
    };
}

interface RevertResult {
    success: boolean;
    restoredFiles: string[];
    errors: Array<{ message: string }>;
}

interface BackupSummary {
    id: string;
    timestamp: number;
    fileCount: number;
    onDisk: boolean;
}

interface ListBackupsResult {
    backups: BackupSummary[];
}

interface TrustLoopSummary {
    message: string;
    logSuffix: string;
    improved: boolean;
    regressedOrFlat: boolean;
}

interface QuickPickItemWithId extends vscode.QuickPickItem {
    suggestionId: string;
    applicationMode?: string;
    autoApplyBlockedReason?: string;
    applicationGuidance?: string;
}

interface RecordBuildOptions {
    buildDir?: string;
    cleanFirst: boolean;
    verbose: boolean;
    buildSystem?: string;
    buildType?: string;
    compiler?: string;
    cCompiler?: string;
    cxxCompiler?: string;
    parallelJobs?: number;
    traceOutputDir?: string;
    extraArgs: string[];
}

interface PersistedBuildProfile {
    projectRoot: string;
    buildSystem?: string;
    buildDir?: string;
    buildType?: string;
    compiler?: string;
    cCompiler?: string;
    cxxCompiler?: string;
    parallelJobs?: number;
    traceOutputDir?: string;
    extraArgs: string[];
    recordedAt: string;
    buildTimeMs?: number;
}

interface PersistedAnalysisRun {
    analysisId?: string;
    recordedAt: string;
    suggestionCount: number;
    totalBuildTimeMs: number;
    buildTimeSource: string;
    filesAnalyzed: number;
}

interface AsyncCommandAccepted {
    accepted: boolean;
    async: boolean;
    jobId: string;
}

// ============================================================================
// UUID Generation
// ============================================================================

function generateUUID(): string {
    // RFC 4122 version 4 UUID
    return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, (c) => {
        const r = Math.random() * 16 | 0;
        const v = c === 'x' ? r : (r & 0x3 | 0x8);
        return v.toString(16);
    });
}

function generateOperationId(prefix: string): string {
    return `${prefix}-${generateUUID()}`;
}

// ============================================================================
// Bounds Checking Utilities
// ============================================================================

function isValidSuggestion(s: unknown): s is Suggestion {
    if (!s || typeof s !== 'object') return false;
    const obj = s as Record<string, unknown>;
    return (
        typeof obj.id === 'string' && obj.id.length > 0 &&
        typeof obj.title === 'string' &&
        typeof obj.description === 'string' &&
        typeof obj.priority === 'number' && obj.priority >= 0 && obj.priority <= 2 &&
        typeof obj.confidence === 'number' && obj.confidence >= 0 && obj.confidence <= 1
    );
}

function isValidAnalysisResult(result: unknown): result is AnalysisResult {
    if (!result || typeof result !== 'object') return false;
    const obj = result as Record<string, unknown>;
    return Array.isArray(obj.suggestions);
}

function isValidApplyResult(result: unknown): result is ApplyResult {
    if (!result || typeof result !== 'object') return false;
    const obj = result as Record<string, unknown>;
    return typeof obj.success === 'boolean';
}

function isValidApplyAllResult(result: unknown): result is ApplyAllResult {
    if (!result || typeof result !== 'object') return false;
    const obj = result as Record<string, unknown>;
    return (
        typeof obj.success === 'boolean' &&
        typeof obj.appliedCount === 'number' &&
        typeof obj.skippedCount === 'number'
    );
}

function isValidRevertResult(result: unknown): result is RevertResult {
    if (!result || typeof result !== 'object') return false;
    const obj = result as Record<string, unknown>;
    return typeof obj.success === 'boolean';
}

function isValidListBackupsResult(result: unknown): result is ListBackupsResult {
    if (!result || typeof result !== 'object') return false;
    const obj = result as Record<string, unknown>;
    return Array.isArray(obj.backups);
}

function isValidRecordBuildResult(result: unknown): result is RecordBuildResult {
    if (!result || typeof result !== 'object') return false;
    const obj = result as Record<string, unknown>;
    return typeof obj.success === 'boolean';
}

function isValidAsyncCommandAccepted(result: unknown): result is AsyncCommandAccepted {
    if (!result || typeof result !== 'object') {
        return false;
    }
    const obj = result as Record<string, unknown>;
    return obj.accepted === true && obj.async === true && typeof obj.jobId === 'string';
}

function isValidSuggestionDetails(result: unknown): result is SuggestionDetails {
    if (!result || typeof result !== 'object') {
        return false;
    }
    const obj = result as Record<string, unknown>;
    return typeof obj.id === 'string' && typeof obj.description === 'string';
}

function safeGetPriority(priority: number): number {
    if (typeof priority !== 'number' || priority < 0 || priority > 2) {
        return 2; // Default to Low
    }
    return Math.floor(priority);
}

function safeGetConfidence(confidence: number): number {
    if (typeof confidence !== 'number' || isNaN(confidence)) {
        return 0;
    }
    return Math.max(0, Math.min(1, confidence));
}

function safeGetNumber(value: unknown, defaultValue: number): number {
    if (typeof value === 'number' && !isNaN(value)) {
        return value;
    }
    return defaultValue;
}

function safeGetString(value: unknown, defaultValue: string): string {
    if (typeof value === 'string') {
        return value;
    }
    return defaultValue;
}

function formatApplicationMode(mode?: string): string {
    switch (mode) {
        case 'direct-edits':
            return 'Direct Edits';
        case 'external-refactor':
            return 'Refactor Tool';
        case 'advisory':
        default:
            return 'Manual Review';
    }
}

function hasBulkApplyPath(suggestion: Suggestion): boolean {
    return suggestion.autoApplicable;
}

function formatDurationMs(ms: number): string {
    return `${(ms / 1000).toFixed(2)}s`;
}

function buildTrustLoopSummary(trustLoop?: ApplyResult['trustLoop'] | ApplyAllResult['trustLoop']): TrustLoopSummary | undefined {
    if (!trustLoop || trustLoop.available !== true) {
        return undefined;
    }

    const actualSavingsMs = safeGetNumber(trustLoop.actualSavingsMs, 0);
    const baselineBuildMs = safeGetNumber(trustLoop.baselineBuildMs, 0);
    const baselineSource = safeGetString(trustLoop.baselineSource, '');
    const rebuildBuildMs = safeGetNumber(trustLoop.rebuildBuildMs, 0);
    const actualSavingsPercent = safeGetNumber(trustLoop.actualSavingsPercent, 0);
    const predictedSavingsMs = safeGetNumber(trustLoop.predictedSavingsMs, 0);
    const predictionDeltaMs = safeGetNumber(trustLoop.predictionDeltaMs, 0);

    let headline = '';
    if (actualSavingsMs > 0) {
        headline = `Measured rebuild improvement: ${formatDurationMs(actualSavingsMs)} faster (${actualSavingsPercent.toFixed(1)}%).`;
    } else if (actualSavingsMs < 0) {
        headline = `Measured rebuild regression: ${formatDurationMs(Math.abs(actualSavingsMs))} slower (${Math.abs(actualSavingsPercent).toFixed(1)}%).`;
    } else {
        headline = 'Measured rebuild delta: no change.';
    }

    let baselineSegment = '';
    if (baselineBuildMs > 0 || rebuildBuildMs > 0) {
        const baselineLabel = baselineSource === 'recorded-build'
            ? 'Recorded baseline'
            : baselineSource === 'trace-aggregate'
                ? 'Trace aggregate baseline'
                : 'Baseline';
        baselineSegment = ` ${baselineLabel} ${formatDurationMs(baselineBuildMs)} -> measured rebuild ${formatDurationMs(rebuildBuildMs)}.`;
    }

    let predictionSegment = '';
    if (predictedSavingsMs !== 0 || predictionDeltaMs !== 0) {
        const deltaPrefix = predictionDeltaMs >= 0 ? '+' : '-';
        predictionSegment =
            ` Estimated ${formatDurationMs(predictedSavingsMs)}; measured-vs-estimate ${deltaPrefix}${formatDurationMs(Math.abs(predictionDeltaMs))}.`;
    }

    return {
        message: `${headline}${baselineSegment}${predictionSegment} Measured rebuild is authoritative; estimate is heuristic.`,
        logSuffix:
            ` trustLoop(actualSavingsMs=${actualSavingsMs}, actualSavingsPercent=${actualSavingsPercent.toFixed(1)}, baselineBuildMs=${baselineBuildMs}, baselineSource=${baselineSource || 'unknown'}, rebuildBuildMs=${rebuildBuildMs}, predictedSavingsMs=${predictedSavingsMs}, predictionDeltaMs=${predictionDeltaMs})`,
        improved: actualSavingsMs > 0,
        regressedOrFlat: actualSavingsMs <= 0
    };
}

function resolveAnalysisBuildTiming(result: AnalysisResult): { totalBuildTimeMs: number; source: string } {
    const metrics = result.baselineMetrics || { totalDurationMs: 0, filesCompiled: 0 };
    const fallbackBuildTimeMs = safeGetNumber(metrics.totalDurationMs, 0);
    const totalBuildTimeMs = safeGetNumber(result.buildTiming?.totalBuildTimeMs, fallbackBuildTimeMs);
    const source = safeGetString(
        result.buildTiming?.source,
        totalBuildTimeMs === fallbackBuildTimeMs ? 'trace-aggregate' : ''
    );
    return { totalBuildTimeMs, source };
}

// ============================================================================
// Client State
// ============================================================================

let client: LanguageClient;
let lastBackupId: string | undefined;
let outputChannel: vscode.OutputChannel;
let traceOutputChannel: vscode.OutputChannel;
let extensionContext: vscode.ExtensionContext;
let bhaViewProvider: BhaTreeDataProvider | undefined;
let bhaPreviewProvider: BhaPreviewContentProvider | undefined;
const lastBuildDirByWorkspace = new Map<string, string>();
const lastTraceDirByWorkspace = new Map<string, string>();
const lastBuildProfileByWorkspace = new Map<string, PersistedBuildProfile>();
const lastBackupIdByWorkspace = new Map<string, string>();
const analysisHistoryByWorkspace = new Map<string, PersistedAnalysisRun[]>();
const BUILD_PROFILE_STATE_PREFIX = 'bha.lastBuildProfile:';
const BACKUP_ID_STATE_PREFIX = 'bha.lastBackupId:';
const ANALYSIS_HISTORY_STATE_PREFIX = 'bha.analysisHistory:';

class BhaPreviewContentProvider implements vscode.TextDocumentContentProvider {
    private readonly changeEmitter = new vscode.EventEmitter<vscode.Uri>();
    private readonly contents = new Map<string, string>();
    readonly onDidChange = this.changeEmitter.event;

    provideTextDocumentContent(uri: vscode.Uri): string {
        return this.contents.get(uri.toString()) ?? '';
    }

    setContent(uri: vscode.Uri, content: string): void {
        this.contents.set(uri.toString(), content);
        this.changeEmitter.fire(uri);
    }

    dispose(): void {
        this.contents.clear();
        this.changeEmitter.dispose();
    }
}

type BhaViewState =
    | 'starting'
    | 'no-data'
    | 'analyzing'
    | 'applying'
    | 'validating'
    | 'ready'
    | 'rolled-back'
    | 'failed';

class BhaTreeItem extends vscode.TreeItem {
    readonly children: BhaTreeItem[];
    readonly suggestionId?: string;

    constructor(
        label: string,
        collapsibleState: vscode.TreeItemCollapsibleState,
        options: {
            description?: string;
            tooltip?: string;
            contextValue?: string;
            icon?: string;
            command?: vscode.Command;
            children?: BhaTreeItem[];
            suggestionId?: string;
        } = {}
    ) {
        super(label, collapsibleState);
        this.description = options.description;
        this.tooltip = options.tooltip ?? label;
        this.contextValue = options.contextValue;
        this.iconPath = options.icon ? new vscode.ThemeIcon(options.icon) : undefined;
        this.command = options.command;
        this.children = options.children ?? [];
        this.suggestionId = options.suggestionId;
    }
}

class BhaTreeDataProvider implements vscode.TreeDataProvider<BhaTreeItem> {
    private readonly changeEmitter = new vscode.EventEmitter<BhaTreeItem | undefined | void>();
    readonly onDidChangeTreeData = this.changeEmitter.event;
    private result: AnalysisResult | undefined;
    private state: BhaViewState = 'starting';
    private stateDetail = 'Starting the language server...';
    private operationStatus: { label: string; detail: string; details: string[] } | undefined;
    private refreshing = false;

    dispose(): void {
        this.changeEmitter.dispose();
    }

    getTreeItem(element: BhaTreeItem): vscode.TreeItem {
        return element;
    }

    getChildren(element?: BhaTreeItem): BhaTreeItem[] {
        return element ? element.children : this.buildRootItems();
    }

    setState(state: BhaViewState, detail: string): void {
        this.state = state;
        this.stateDetail = detail;
        this.changeEmitter.fire();
    }

    setOperationStatus(label: string, detail: string, details: string[] = []): void {
        this.operationStatus = { label, detail, details };
        this.changeEmitter.fire();
    }

    setAnalysisResult(result: AnalysisResult): void {
        this.result = {
            ...result,
            suggestions: result.suggestions.filter(isValidSuggestion)
        };
        if (hasAnalysisMetrics(this.result)) {
            rememberAnalysisRun(this.result);
        }
        this.state = 'ready';
        this.stateDetail = this.result.suggestions.length === 0
            ? 'Analysis completed without actionable suggestions.'
            : 'Analysis completed; review the evidence before applying changes.';
        this.changeEmitter.fire();
    }

    async refresh(): Promise<void> {
        if (this.refreshing || !client) {
            this.changeEmitter.fire();
            return;
        }

        this.refreshing = true;
        this.changeEmitter.fire();
        try {
            const response = await client.sendRequest<unknown>('workspace/executeCommand', {
                command: 'bha.showMetrics',
                arguments: []
            });
            if (isValidAnalysisResult(response)) {
                // showMetrics intentionally contains suggestions only. Preserve the
                // full analysis payload when a background refresh returns that cache.
                if (this.result && !hasAnalysisMetrics(response)) {
                    this.result = {
                        ...this.result,
                        suggestions: response.suggestions.filter(isValidSuggestion)
                    };
                    this.state = 'ready';
                    this.stateDetail = this.result.suggestions.length === 0
                        ? 'Analysis completed without actionable suggestions.'
                        : 'Analysis completed; review the evidence before applying changes.';
                    this.changeEmitter.fire();
                } else if (!hasAnalysisMetrics(response) && response.suggestions.length === 0) {
                    this.result = undefined;
                    this.state = 'no-data';
                    this.stateDetail = getAnalysisHistory(getWorkspaceRootPath()).length > 0
                        ? 'A previous run is saved in history; analyze again to load current suggestions.'
                        : 'Record traces and analyze the workspace to populate BHA.';
                    this.changeEmitter.fire();
                } else {
                    this.setAnalysisResult(response);
                }
            } else {
                this.result = undefined;
                this.state = 'no-data';
                this.stateDetail = 'Record traces and analyze the workspace to populate BHA.';
                this.changeEmitter.fire();
            }
        } catch (error) {
            this.state = 'failed';
            this.stateDetail = error instanceof Error ? error.message : String(error);
            this.changeEmitter.fire();
        } finally {
            this.refreshing = false;
        }
    }

    private buildRootItems(): BhaTreeItem[] {
        const result = this.result;
        const metrics = result?.baselineMetrics;
        const timing = result ? resolveAnalysisBuildTiming(result) : undefined;
        const suggestions = result?.suggestions ?? [];
        const history = getAnalysisHistory(getWorkspaceRootPath());
        const statusIcon = this.state === 'failed'
            ? 'error'
            : this.state === 'ready'
                ? 'pass'
                : this.state === 'no-data'
                    ? 'circle-slash'
                    : 'loading~spin';

        const metricChildren = timing && metrics
            ? [
                new BhaTreeItem(
                    `${formatDurationMs(timing.totalBuildTimeMs)} build time`,
                    vscode.TreeItemCollapsibleState.None,
                    { description: timing.source || 'source unavailable', icon: 'clock' }
                ),
                new BhaTreeItem(
                    `${safeGetNumber(result?.filesAnalyzed ?? metrics.filesCompiled, 0)} compilation units`,
                    vscode.TreeItemCollapsibleState.None,
                    { icon: 'symbol-file' }
                )
            ]
            : history.length > 0
                ? [
                    new BhaTreeItem(
                        `${formatDurationMs(history[0].totalBuildTimeMs)} last recorded build`,
                        vscode.TreeItemCollapsibleState.None,
                        { description: `${history[0].buildTimeSource} · ${formatHistoryDate(history[0].recordedAt)}`, icon: 'clock' }
                    ),
                    new BhaTreeItem(
                        `${history[0].filesAnalyzed} compilation units last analyzed`,
                        vscode.TreeItemCollapsibleState.None,
                        { icon: 'symbol-file' }
                    )
                ]
                : [new BhaTreeItem('No metrics available', vscode.TreeItemCollapsibleState.None, { icon: 'info' })];

        const suggestionItems = suggestions.map((suggestion) => {
            const priority = safeGetPriority(suggestion.priority);
            const mode = formatApplicationMode(suggestion.applicationMode);
            const confidence = `${(safeGetConfidence(suggestion.confidence) * 100).toFixed(0)}% confidence`;
            const description = `${PRIORITY_LABELS[priority]} · ${mode} · ${confidence}`;
            const isAdvisory = suggestion.applicationMode === 'advisory';
            return new BhaTreeItem(
                suggestion.title,
                vscode.TreeItemCollapsibleState.None,
                {
                    description,
                    tooltip: `${suggestion.title}\n${extractSuggestionSummary(suggestion.description)}`,
                    contextValue: 'bhaSuggestion',
                    icon: isAdvisory ? 'warning' : priority === 0 ? 'flame' : 'lightbulb',
                    command: {
                        command: 'buildHotspotAnalyzer.showSuggestions',
                        title: 'Show Suggestion Details'
                    },
                    suggestionId: suggestion.id
                }
            );
        });

        const suggestionGroup = new BhaTreeItem(
            `Suggestions (${suggestions.length})`,
            suggestions.length > 0
                ? vscode.TreeItemCollapsibleState.Expanded
                : vscode.TreeItemCollapsibleState.None,
            {
                description: suggestions.length > 0 ? 'Review before applying' : 'None available',
                icon: suggestions.length > 0 ? 'lightbulb' : 'check',
                children: suggestionItems
            }
        );

        const actionChildren = [
            new BhaTreeItem('Record Build Traces', vscode.TreeItemCollapsibleState.None, {
                icon: 'record',
                command: {
                    command: 'buildHotspotAnalyzer.recordBuildTraces',
                    title: 'Record Build Traces'
                }
            }),
            new BhaTreeItem('Analyze Build Performance', vscode.TreeItemCollapsibleState.None, {
                icon: 'pulse',
                command: {
                    command: 'buildHotspotAnalyzer.analyzeProject',
                    title: 'Analyze Build Performance'
                }
            }),
            new BhaTreeItem('Show Full Suggestions', vscode.TreeItemCollapsibleState.None, {
                icon: 'open-preview',
                command: {
                    command: 'buildHotspotAnalyzer.showSuggestions',
                    title: 'Show Full Suggestions'
                }
            }),
            new BhaTreeItem('Revert Changes', vscode.TreeItemCollapsibleState.None, {
                icon: 'discard',
                command: {
                    command: 'buildHotspotAnalyzer.revertChanges',
                    title: 'Revert Changes'
                }
            })
        ];

        const historyItems = history.map((run) => new BhaTreeItem(
            formatHistoryDate(run.recordedAt),
            vscode.TreeItemCollapsibleState.None,
            {
                description: `${run.suggestionCount} suggestions · ${formatDurationMs(run.totalBuildTimeMs)}`,
                tooltip: `${run.analysisId ?? 'Analysis run'}\n${run.buildTimeSource}\n${run.filesAnalyzed} compilation units`,
                icon: 'history'
            }
        ));
        const historyGroup = new BhaTreeItem(
            `Run History (${history.length})`,
            history.length > 0 ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None,
            {
                description: history.length > 0 ? 'Persisted workspace summaries' : 'No completed runs',
                icon: 'history',
                children: historyItems
            }
        );

        const roots = [
            new BhaTreeItem(
                `BHA: ${this.state.replace('-', ' ')}`,
                vscode.TreeItemCollapsibleState.None,
                {
                    description: this.stateDetail,
                    tooltip: this.stateDetail,
                    icon: statusIcon
                }
            ),
            new BhaTreeItem('Build Metrics', vscode.TreeItemCollapsibleState.Expanded, {
                icon: 'graph',
                children: metricChildren
            }),
            suggestionGroup,
            historyGroup,
            new BhaTreeItem('Actions', vscode.TreeItemCollapsibleState.Expanded, {
                icon: 'tools',
                children: actionChildren
            })
        ];
        if (this.operationStatus) {
            const operationFailed = /failed|rolled back/i.test(this.operationStatus.label);
            const operationDetails = this.operationStatus.details.map((detail) => new BhaTreeItem(
                detail,
                vscode.TreeItemCollapsibleState.None,
                { icon: operationFailed ? 'warning' : 'info' }
            ));
            roots.splice(1, 0, new BhaTreeItem(
                this.operationStatus.label,
                operationDetails.length > 0
                    ? vscode.TreeItemCollapsibleState.Expanded
                    : vscode.TreeItemCollapsibleState.None,
                {
                    description: this.operationStatus.detail,
                    tooltip: this.operationStatus.detail,
                    icon: operationFailed ? 'warning' : 'pass',
                    children: operationDetails
                }
            ));
        }
        return roots;
    }
}

function getWorkspaceRootPath(): string | undefined {
    return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}

function buildProfileStateKey(workspaceRoot: string): string {
    return `${BUILD_PROFILE_STATE_PREFIX}${workspaceRoot}`;
}

function backupIdStateKey(workspaceRoot: string): string {
    return `${BACKUP_ID_STATE_PREFIX}${workspaceRoot}`;
}

function analysisHistoryStateKey(workspaceRoot: string): string {
    return `${ANALYSIS_HISTORY_STATE_PREFIX}${workspaceRoot}`;
}

function hasAnalysisMetrics(result: AnalysisResult): boolean {
    return result.baselineMetrics !== undefined ||
        result.buildTiming !== undefined ||
        result.filesAnalyzed !== undefined;
}

function getAnalysisHistory(workspaceRoot: string | undefined): PersistedAnalysisRun[] {
    return workspaceRoot ? analysisHistoryByWorkspace.get(workspaceRoot) ?? [] : [];
}

function formatHistoryDate(value: string): string {
    const parsed = new Date(value);
    return Number.isNaN(parsed.getTime()) ? 'Unknown analysis time' : parsed.toLocaleString();
}

function rememberAnalysisRun(result: AnalysisResult): void {
    const workspaceRoot = getWorkspaceRootPath();
    if (!workspaceRoot || !hasAnalysisMetrics(result)) {
        return;
    }

    const timing = resolveAnalysisBuildTiming(result);
    const summary: PersistedAnalysisRun = {
        analysisId: result.analysisId,
        recordedAt: new Date().toISOString(),
        suggestionCount: result.suggestions.length,
        totalBuildTimeMs: timing.totalBuildTimeMs,
        buildTimeSource: timing.source || 'unknown',
        filesAnalyzed: safeGetNumber(
            result.filesAnalyzed ?? result.baselineMetrics?.filesCompiled,
            0
        )
    };
    const previous = getAnalysisHistory(workspaceRoot);
    const next = [
        summary,
        ...previous.filter((run) =>
            !summary.analysisId || run.analysisId !== summary.analysisId
        )
    ].slice(0, 10);
    analysisHistoryByWorkspace.set(workspaceRoot, next);
    void extensionContext.workspaceState.update(analysisHistoryStateKey(workspaceRoot), next);
}

function timestamp(): string {
    return new Date().toLocaleTimeString();
}

function logLine(message: string): void {
    outputChannel.appendLine(`[${timestamp()}] ${message}`);
}

function logBlock(title: string, content?: string): void {
    if (!content || content.trim().length === 0) {
        return;
    }
    outputChannel.appendLine(`[${timestamp()}] ${title}`);
    for (const line of content.replace(/\r\n/g, '\n').split('\n')) {
        outputChannel.appendLine(line);
    }
}

function showOutput(preserveFocus = true): void {
    outputChannel.show(preserveFocus);
}

function traceSettingToProtocol(value: string): Trace {
    switch (value) {
        case 'messages':
            return Trace.Messages;
        case 'verbose':
            return Trace.Verbose;
        case 'off':
        default:
            return Trace.Off;
    }
}

function normalizeWorkspaceRelativePath(workspaceRoot: string, candidate?: string): string | undefined {
    const trimmed = candidate?.trim();
    if (!trimmed) {
        return undefined;
    }
    return path.isAbsolute(trimmed) ? path.normalize(trimmed) : path.normalize(path.join(workspaceRoot, trimmed));
}

function normalizeCompilerOverride(candidate?: string): string | undefined {
    const trimmed = candidate?.trim();
    return trimmed ? trimmed : undefined;
}

function isValidCompilerOverride(candidate: string | undefined): boolean {
    if (!candidate) {
        return true;
    }
    if (!candidate.includes(path.sep) && !candidate.startsWith('.')) {
        return true;
    }
    return fs.existsSync(path.normalize(candidate));
}

function formatCompilerOverrides(options: {
    cCompiler?: string;
    cxxCompiler?: string;
    compiler?: string;
}): string {
    if (options.cCompiler || options.cxxCompiler) {
        return `C=${options.cCompiler ?? 'auto'}, CXX=${options.cxxCompiler ?? 'auto'}`;
    }
    return options.compiler ?? 'auto';
}

function validatePersistedBuildProfile(profile: PersistedBuildProfile | undefined, workspaceRoot: string): PersistedBuildProfile | undefined {
    if (!profile || profile.projectRoot !== workspaceRoot) {
        return undefined;
    }

    const resolvedBuildDir = normalizeWorkspaceRelativePath(workspaceRoot, profile.buildDir);
    if (resolvedBuildDir && !fs.existsSync(resolvedBuildDir)) {
        return undefined;
    }

    const resolvedTraceDir = normalizeWorkspaceRelativePath(workspaceRoot, profile.traceOutputDir);
    if (resolvedTraceDir && !fs.existsSync(resolvedTraceDir)) {
        return undefined;
    }

    const compiler = normalizeCompilerOverride(profile.compiler);
    const cCompiler = normalizeCompilerOverride(profile.cCompiler);
    const cxxCompiler = normalizeCompilerOverride(profile.cxxCompiler);
    if (!isValidCompilerOverride(compiler) ||
        !isValidCompilerOverride(cCompiler) ||
        !isValidCompilerOverride(cxxCompiler)) {
        return undefined;
    }

    return {
        ...profile,
        buildDir: profile.buildDir?.trim() || undefined,
        traceOutputDir: profile.traceOutputDir?.trim() || undefined,
        compiler: compiler || undefined,
        cCompiler: cCompiler || undefined,
        cxxCompiler: cxxCompiler || undefined,
        extraArgs: Array.isArray(profile.extraArgs) ? profile.extraArgs : [],
        buildTimeMs: typeof profile.buildTimeMs === 'number' && Number.isFinite(profile.buildTimeMs) && profile.buildTimeMs >= 0
            ? profile.buildTimeMs
            : undefined
    };
}

async function persistBuildProfile(workspaceRoot: string, profile: PersistedBuildProfile): Promise<void> {
    lastBuildProfileByWorkspace.set(workspaceRoot, profile);
    await extensionContext.workspaceState.update(buildProfileStateKey(workspaceRoot), profile);
}

async function clearPersistedBuildProfile(workspaceRoot: string): Promise<void> {
    lastBuildProfileByWorkspace.delete(workspaceRoot);
    await extensionContext.workspaceState.update(buildProfileStateKey(workspaceRoot), undefined);
}

async function persistLastBackupId(workspaceRoot: string, backupId: string): Promise<void> {
    lastBackupIdByWorkspace.set(workspaceRoot, backupId);
    lastBackupId = backupId;
    await extensionContext.workspaceState.update(backupIdStateKey(workspaceRoot), backupId);
}

async function clearPersistedLastBackupId(workspaceRoot: string): Promise<void> {
    lastBackupIdByWorkspace.delete(workspaceRoot);
    lastBackupId = undefined;
    await extensionContext.workspaceState.update(backupIdStateKey(workspaceRoot), undefined);
}

function getReusableBackupId(workspaceRoot: string): string | undefined {
    const cached = lastBackupIdByWorkspace.get(workspaceRoot)?.trim();
    if (cached) {
        return cached;
    }
    const persisted = extensionContext.workspaceState.get<string>(backupIdStateKey(workspaceRoot))?.trim();
    if (persisted) {
        lastBackupIdByWorkspace.set(workspaceRoot, persisted);
        return persisted;
    }
    return undefined;
}

function getReusableBuildProfile(workspaceRoot: string): PersistedBuildProfile | undefined {
    const cached = validatePersistedBuildProfile(lastBuildProfileByWorkspace.get(workspaceRoot), workspaceRoot);
    if (cached) {
        return cached;
    }
    void clearPersistedBuildProfile(workspaceRoot);
    return undefined;
}

// ============================================================================
// Activation
// ============================================================================

export function activate(context: vscode.ExtensionContext) {
    extensionContext = context;
    const config = vscode.workspace.getConfiguration('buildHotspotAnalyzer');
    const serverPath = config.get<string>('serverPath', CONFIG.DEFAULT_SERVER_PATH);

    outputChannel = vscode.window.createOutputChannel('Build Hotspot Analyzer');
    traceOutputChannel = vscode.window.createOutputChannel('Build Hotspot Analyzer Trace');
    context.subscriptions.push(outputChannel, traceOutputChannel);

    bhaViewProvider = new BhaTreeDataProvider();
    bhaPreviewProvider = new BhaPreviewContentProvider();
    context.subscriptions.push(
        bhaViewProvider,
        bhaPreviewProvider,
        vscode.workspace.registerTextDocumentContentProvider('bha-preview', bhaPreviewProvider),
        vscode.window.registerTreeDataProvider('buildHotspotAnalyzer.analysis', bhaViewProvider)
    );

    for (const folder of vscode.workspace.workspaceFolders ?? []) {
        const workspaceRoot = folder.uri.fsPath;
        const cachedHistory = context.workspaceState.get<unknown>(analysisHistoryStateKey(workspaceRoot));
        if (Array.isArray(cachedHistory)) {
            const validHistory = cachedHistory.filter((run): run is PersistedAnalysisRun => {
                if (!run || typeof run !== 'object') {
                    return false;
                }
                const item = run as Record<string, unknown>;
                return typeof item.recordedAt === 'string' &&
                    typeof item.suggestionCount === 'number' &&
                    typeof item.totalBuildTimeMs === 'number' &&
                    typeof item.buildTimeSource === 'string' &&
                    typeof item.filesAnalyzed === 'number';
            }).slice(0, 10);
            analysisHistoryByWorkspace.set(workspaceRoot, validHistory);
        } else {
            void context.workspaceState.update(analysisHistoryStateKey(workspaceRoot), undefined);
        }

        const cachedProfile = validatePersistedBuildProfile(
            context.workspaceState.get<PersistedBuildProfile>(buildProfileStateKey(workspaceRoot)),
            workspaceRoot
        );
        if (cachedProfile) {
            lastBuildProfileByWorkspace.set(workspaceRoot, cachedProfile);
            if (cachedProfile.buildDir) {
                rememberWorkspaceBuildDir(workspaceRoot, cachedProfile.buildDir);
            }
            if (cachedProfile.traceOutputDir) {
                lastTraceDirByWorkspace.set(workspaceRoot, cachedProfile.traceOutputDir);
            }
        } else {
            void context.workspaceState.update(buildProfileStateKey(workspaceRoot), undefined);
        }

        const cachedBackupId = context.workspaceState.get<string>(backupIdStateKey(workspaceRoot))?.trim();
        if (cachedBackupId) {
            lastBackupIdByWorkspace.set(workspaceRoot, cachedBackupId);
            lastBackupId = cachedBackupId;
        } else {
            void context.workspaceState.update(backupIdStateKey(workspaceRoot), undefined);
        }
    }

    if (!serverPath || serverPath.trim().length === 0) {
        logLine('Server path is not configured');
        vscode.window.showErrorMessage('BHA: Server path is not configured');
        return;
    }

    logLine(`Activating extension with server: ${serverPath}`);

    const serverOptions: ServerOptions = {
        command: serverPath,
        args: [],
        transport: TransportKind.stdio
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [
            { scheme: 'file', language: 'cpp' },
            { scheme: 'file', language: 'c' }
        ],
        outputChannel,
        traceOutputChannel,
        revealOutputChannelOn: RevealOutputChannelOn.Error,
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.{cpp,cc,cxx,c,h,hpp,hxx}')
        }
    };

    client = new LanguageClient(
        'buildHotspotAnalyzer',
        'Build Hotspot Analyzer',
        serverOptions,
        clientOptions
    );

    client.onNotification('bha/jobLog', (params: unknown) => {
        if (!params || typeof params !== 'object') {
            return;
        }
        const payload = params as Record<string, unknown>;
        const jobId = safeGetString(payload.jobId, '');
        const category = safeGetString(payload.category, 'job');
        const message = safeGetString(payload.message, '');
        if (!message) {
            return;
        }
        logLine(`[${category}${jobId ? ` ${jobId}` : ''}] ${message}`);
        if (category === 'apply') {
            if (message.includes('Re-analyzing project') || message.includes('Rerank analysis')) {
                bhaViewProvider?.setState('validating', 'Fresh traces are being collected to re-rank remaining suggestions.');
                bhaViewProvider?.setOperationStatus('Re-ranking remaining suggestions', message);
            } else if (message.includes('Fault isolation')) {
                bhaViewProvider?.setState('validating', 'Validation failed; isolating edits that can be retained.');
                bhaViewProvider?.setOperationStatus('Isolating bulk-apply failures', message);
            }
        }
    });
    client.onNotification('bha/jobStarted', (params: unknown) => {
        const payload = (params && typeof params === 'object') ? params as Record<string, unknown> : {};
        logLine(`Background job started: ${safeGetString(payload.command, 'unknown')} (${safeGetString(payload.jobId, '')})`);
    });
    client.onNotification('bha/jobCompleted', (params: unknown) => {
        const payload = (params && typeof params === 'object') ? params as Record<string, unknown> : {};
        logLine(`Background job completed: ${safeGetString(payload.command, 'unknown')} (${safeGetString(payload.jobId, '')}) status=${safeGetString(payload.status, 'unknown')}`);
        void bhaViewProvider?.refresh();
    });

    // Register commands
    context.subscriptions.push(
        vscode.commands.registerCommand('buildHotspotAnalyzer.recordBuildTraces', cmdRecordBuildTraces),
        vscode.commands.registerCommand('buildHotspotAnalyzer.recordBuildTracesAdvanced', cmdRecordBuildTracesAdvanced),
        vscode.commands.registerCommand('buildHotspotAnalyzer.analyzeProject', cmdAnalyzeProject),
        vscode.commands.registerCommand('buildHotspotAnalyzer.showSuggestions', cmdShowSuggestions),
        vscode.commands.registerCommand('buildHotspotAnalyzer.showActivityLog', cmdShowActivityLog),
        vscode.commands.registerCommand('buildHotspotAnalyzer.applySuggestion', cmdApplySuggestion),
        vscode.commands.registerCommand('buildHotspotAnalyzer.applyAllSuggestions', cmdApplyAllSuggestions),
        vscode.commands.registerCommand('buildHotspotAnalyzer.revertChanges', cmdRevertChanges),
        vscode.commands.registerCommand('buildHotspotAnalyzer.restartServer', cmdRestartServer),
        vscode.commands.registerCommand('buildHotspotAnalyzer.refreshView', () => bhaViewProvider?.refresh()),
        vscode.commands.registerCommand('buildHotspotAnalyzer.previewSuggestion', cmdPreviewSuggestion)
    );

    void client.start().then(async () => {
        const traceSetting = config.get<string>('trace.server', 'off');
        await client.setTrace(traceSettingToProtocol(traceSetting));
        logLine(`Language client ready (trace=${traceSetting})`);
        await bhaViewProvider?.refresh();
    }).catch((error: unknown) => {
        const errorMessage = error instanceof Error ? error.message : String(error);
        logLine(`Language client failed to initialize: ${errorMessage}`);
    });

    if (config.get<boolean>('autoAnalyze', false)) {
        setTimeout(() => {
            vscode.commands.executeCommand('buildHotspotAnalyzer.analyzeProject');
        }, CONFIG.AUTO_ANALYZE_DELAY_MS);
    }
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    logLine('Deactivating extension');
    return client.stop();
}

// ============================================================================
// Command Handlers
// ============================================================================

function rememberWorkspaceBuildDir(workspaceRoot: string, buildDir?: string): void {
    const normalized = buildDir?.trim();
    if (!normalized) {
        return;
    }
    lastBuildDirByWorkspace.set(workspaceRoot, normalized);
}

function getWorkspaceBuildDir(workspaceRoot: string): string | undefined {
    return lastBuildDirByWorkspace.get(workspaceRoot) ?? 'build';
}

async function promptForBuildDir(defaultValue?: string): Promise<string | undefined> {
    return vscode.window.showInputBox({
        prompt: 'Build directory (optional, leave empty for auto-detect)',
        placeHolder: 'build',
        value: defaultValue
    });
}

async function withBhaProgress<T>(
    title: string,
    cancellable: boolean,
    task: (
        progress: vscode.Progress<{ message?: string; increment?: number }>,
        token: vscode.CancellationToken
    ) => Promise<T>
): Promise<T> {
    logLine(`${title} started`);
    showOutput(true);
    return vscode.window.withProgress(
        {
            location: vscode.ProgressLocation.Notification,
            title,
            cancellable
        },
        (progress, token) => task(progress, token)
    );
}

async function delay(ms: number): Promise<void> {
    await new Promise((resolve) => setTimeout(resolve, ms));
}

async function runAsyncLspCommand<T>(
    title: string,
    command: string,
    argumentsPayload: Record<string, unknown>,
    startedMessage: string
): Promise<T> {
    const accepted = await client.sendRequest<unknown>('workspace/executeCommand', {
        command,
        arguments: [{ ...argumentsPayload, async: true }]
    });
    if (!isValidAsyncCommandAccepted(accepted)) {
        throw new Error('Server did not accept async command');
    }

    const jobId = accepted.jobId;
    let cancelRequested = false;

    return withBhaProgress(title, true, async (progress, token) => {
        progress.report({ message: startedMessage });
        return new Promise<T>(async (resolve, reject) => {
            let finished = false;
            const onCancel = async () => {
                if (cancelRequested || finished) {
                    return;
                }
                cancelRequested = true;
                logLine(`Cancellation requested for job ${jobId}`);
                progress.report({ message: 'Cancellation requested...' });
                try {
                    await client.sendRequest('workspace/executeCommand', {
                        command: 'bha.cancelJob',
                        arguments: [{ jobId }]
                    });
                } catch (error) {
                    const errorMessage = error instanceof Error ? error.message : String(error);
                    logLine(`Failed to request cancellation for ${jobId}: ${errorMessage}`);
                }
            };

            const subscription = token.onCancellationRequested(onCancel);

            try {
                while (!finished) {
                    const statusResult = await client.sendRequest<unknown>('workspace/executeCommand', {
                        command: 'bha.getJobStatus',
                        arguments: [{ jobId }]
                    });
                    const statusPayload = (statusResult && typeof statusResult === 'object')
                        ? statusResult as Record<string, unknown>
                        : {};
                    const status = safeGetString(statusPayload.status, 'unknown');
                    const errorMessage = safeGetString(statusPayload.error, '');
                    const result = statusPayload.result;

                    if (status === 'queued') {
                        progress.report({ message: 'Queued...' });
                    } else if (status === 'running') {
                        progress.report({ message: cancelRequested ? 'Cancelling...' : 'Running...' });
                    } else if (status === 'completed') {
                        finished = true;
                        resolve(result as T);
                        return;
                    } else if (status === 'cancelled') {
                        finished = true;
                        reject(new Error('Operation cancelled'));
                        return;
                    } else if (status === 'failed') {
                        finished = true;
                        reject(new Error(errorMessage || 'Operation failed'));
                        return;
                    }

                    await delay(250);
                }
            } catch (error) {
                reject(error);
            } finally {
                subscription.dispose();
            }
        });
    });
}

async function fetchAvailableBackups(): Promise<BackupSummary[]> {
    const result = await client.sendRequest<unknown>('workspace/executeCommand', {
        command: 'bha.listBackups',
        arguments: [{}]
    });

    if (!isValidListBackupsResult(result)) {
        return [];
    }

    return result.backups
        .filter((backup) => backup && typeof backup.id === 'string' && backup.id.length > 0)
        .sort((lhs, rhs) => safeGetNumber(rhs.timestamp, 0) - safeGetNumber(lhs.timestamp, 0));
}

async function resolveBackupIdForRevert(workspaceRoot: string | undefined): Promise<string | undefined> {
    const preferredBackupId = workspaceRoot ? getReusableBackupId(workspaceRoot) : lastBackupId;
    const backups = await fetchAvailableBackups();

    if (backups.length === 0) {
        return preferredBackupId;
    }

    if (preferredBackupId && backups.some((backup) => backup.id === preferredBackupId)) {
        return preferredBackupId;
    }

    if (backups.length === 1) {
        return backups[0].id;
    }

    const selected = await vscode.window.showQuickPick(
        backups.map((backup) => {
            const timestamp = safeGetNumber(backup.timestamp, 0);
            const when = timestamp > 0
                ? new Date(timestamp * 1000).toLocaleString()
                : 'unknown time';
            return {
                label: backup.id,
                description: `${backup.fileCount} file(s)`,
                detail: `${when}${backup.onDisk ? ' • disk backup' : ''}`,
                backupId: backup.id
            };
        }),
        {
            placeHolder: 'Select a backup to revert'
        }
    );

    return selected?.backupId;
}

async function fetchSuggestionDetails(suggestionId: string): Promise<SuggestionDetails | undefined> {
    try {
        const result = await client.sendRequest<unknown>('workspace/executeCommand', {
            command: 'bha.getSuggestionDetails',
            arguments: [{ suggestionId }]
        });
        if (isValidSuggestionDetails(result)) {
            return result;
        }
    } catch {
        return undefined;
    }
    return undefined;
}

async function cmdShowActivityLog(): Promise<void> {
    showOutput(false);
}

function splitShellArgs(input: string): string[] {
    const args: string[] = [];
    let current = '';
    let quote: '"' | '\'' | null = null;
    let escape = false;

    for (const ch of input) {
        if (escape) {
            current += ch;
            escape = false;
            continue;
        }
        if (ch === '\\') {
            escape = true;
            continue;
        }
        if (quote) {
            if (ch === quote) {
                quote = null;
            } else {
                current += ch;
            }
            continue;
        }
        if (ch === '"' || ch === '\'') {
            quote = ch;
            continue;
        }
        if (/\s/.test(ch)) {
            if (current.length > 0) {
                args.push(current);
                current = '';
            }
            continue;
        }
        current += ch;
    }

    if (current.length > 0) {
        args.push(current);
    }

    return args;
}

async function promptForRecordBuildOptions(advanced: boolean): Promise<RecordBuildOptions | undefined> {
    const workspaceRoot = getWorkspaceRootPath();
    const buildDir = await promptForBuildDir(workspaceRoot ? getWorkspaceBuildDir(workspaceRoot) : 'build');
    if (buildDir === undefined) {
        return undefined;
    }

    const options: RecordBuildOptions = {
        buildDir: buildDir || undefined,
        cleanFirst: false,
        verbose: false,
        extraArgs: []
    };

    if (!advanced) {
        return options;
    }

    const buildSystem = await vscode.window.showQuickPick([
        { label: 'Auto-detect', value: '' },
        { label: 'CMake', value: 'CMake' },
        { label: 'Ninja', value: 'Ninja' },
        { label: 'Make', value: 'Make' },
        { label: 'Meson', value: 'Meson' },
        { label: 'Bazel', value: 'Bazel' },
        { label: 'Buck2', value: 'Buck2' },
        { label: 'SCons', value: 'SCons' },
        { label: 'Unreal', value: 'Unreal' },
        { label: 'XCode', value: 'XCode' },
        { label: 'MSBuild', value: 'MSBuild' }
    ], {
        title: 'Build System',
        placeHolder: 'Choose a build system override or keep auto-detect'
    });
    if (buildSystem === undefined) {
        return undefined;
    }
    options.buildSystem = buildSystem.value || undefined;

    const buildType = await vscode.window.showQuickPick([
        { label: 'Release', value: 'Release' },
        { label: 'Debug', value: 'Debug' },
        { label: 'RelWithDebInfo', value: 'RelWithDebInfo' },
        { label: 'MinSizeRel', value: 'MinSizeRel' },
        { label: 'Development', value: 'Development' }
    ], {
        title: 'Build Type',
        placeHolder: 'Choose a build type'
    });
    if (buildType === undefined) {
        return undefined;
    }
    options.buildType = buildType.value;

    const cCompiler = await vscode.window.showInputBox({
        title: 'C Compiler Override',
        prompt: 'C compiler executable or absolute path (optional)',
        placeHolder: 'clang, gcc, icx, /usr/bin/clang'
    });
    if (cCompiler === undefined) {
        return undefined;
    }
    options.cCompiler = normalizeCompilerOverride(cCompiler);

    const cxxCompiler = await vscode.window.showInputBox({
        title: 'C++ Compiler Override',
        prompt: 'C++ compiler executable or absolute path (optional)',
        placeHolder: 'clang++, g++, icpx, /usr/bin/clang++'
    });
    if (cxxCompiler === undefined) {
        return undefined;
    }
    options.cxxCompiler = normalizeCompilerOverride(cxxCompiler);

    const parallelJobs = await vscode.window.showInputBox({
        title: 'Parallel Jobs',
        prompt: 'Number of parallel jobs (optional, leave empty for auto-detect)',
        placeHolder: '8',
        validateInput: (value) => {
            if (value.trim().length === 0) {
                return undefined;
            }
            const parsed = Number(value);
            return Number.isInteger(parsed) && parsed > 0 ? undefined : 'Enter a positive integer';
        }
    });
    if (parallelJobs === undefined) {
        return undefined;
    }
    options.parallelJobs = parallelJobs.trim().length > 0 ? Number(parallelJobs) : undefined;

    const traceOutputDir = await vscode.window.showInputBox({
        title: 'Trace Output Directory',
        prompt: 'Directory for trace files (optional, leave empty to use the adapter default)',
        placeHolder: 'traces'
    });
    if (traceOutputDir === undefined) {
        return undefined;
    }
    options.traceOutputDir = traceOutputDir.trim() || undefined;

    const extraArgs = await vscode.window.showInputBox({
        title: 'Extra Build Arguments',
        prompt: 'Additional build-system arguments (optional)',
        placeHolder: '--config Debug -DENABLE_SOMETHING=ON'
    });
    if (extraArgs === undefined) {
        return undefined;
    }
    options.extraArgs = splitShellArgs(extraArgs);

    const cleanFirst = await vscode.window.showQuickPick([
        { label: 'No', value: false },
        { label: 'Yes', value: true }
    ], {
        title: 'Clean Before Build',
        placeHolder: 'Run a clean build before recording traces?'
    });
    if (cleanFirst === undefined) {
        return undefined;
    }
    options.cleanFirst = cleanFirst.value;

    const verbose = await vscode.window.showQuickPick([
        { label: 'No', value: false },
        { label: 'Yes', value: true }
    ], {
        title: 'Verbose Build Output',
        placeHolder: 'Enable verbose build output while recording traces?'
    });
    if (verbose === undefined) {
        return undefined;
    }
    options.verbose = verbose.value;

    return options;
}

async function runAnalysis(
    buildDir: string | undefined,
    rebuild: boolean,
    traceDir?: string
): Promise<AnalysisResult | undefined> {
    const workspaceRoot = getWorkspaceRootPath();
    if (!workspaceRoot) {
        vscode.window.showErrorMessage('No workspace folder open');
        return undefined;
    }
    rememberWorkspaceBuildDir(workspaceRoot, buildDir);
    logLine(`Analyze request: projectRoot=${workspaceRoot}, buildDir=${buildDir ?? '<auto>'}, traceDir=${traceDir ?? '<auto>'}, rebuild=${rebuild}`);

    const operationId = generateOperationId(rebuild ? 'build-and-analyze' : 'analyze');
    bhaViewProvider?.setState('analyzing', rebuild
        ? 'Rebuilding and analyzing build performance...'
        : 'Analyzing traces and generating suggestions...');

    try {
        const result = await runAsyncLspCommand<unknown>(
            rebuild ? 'BHA: Rebuilding and analyzing build performance' : 'BHA: Analyzing build performance',
            'bha.analyze',
            {
                projectRoot: workspaceRoot,
                buildDir: buildDir || undefined,
                traceDir: traceDir || undefined,
                rebuild,
                operationId
            },
            'Analyzing traces and generating suggestions...'
        );

        if (!isValidAnalysisResult(result)) {
            logLine('Analyze request returned an invalid result');
            vscode.window.showErrorMessage('Analysis returned invalid result');
            return;
        }

        // Filter out invalid suggestions
        const validSuggestions = result.suggestions.filter(isValidSuggestion);
        result.suggestions = validSuggestions;
        bhaViewProvider?.setAnalysisResult(result);
        bhaViewProvider?.setOperationStatus(
            'Analysis completed',
            `${validSuggestions.length} suggestion(s) found; evidence is ready for review.`
        );

        vscode.window.showInformationMessage(
            `Analysis complete: ${validSuggestions.length} suggestions found`
        );
        const metrics = result.baselineMetrics;
        const buildTiming = resolveAnalysisBuildTiming(result);
        logLine(
            `Analysis complete: suggestions=${validSuggestions.length}, totalBuildTimeMs=${buildTiming.totalBuildTimeMs}, totalBuildTimeSource=${buildTiming.source || 'unknown'}, filesAnalyzed=${safeGetNumber(result.filesAnalyzed, safeGetNumber(metrics?.filesCompiled, 0))}`
        );

        if (validSuggestions.length > 0) {
            await showSuggestionsPanel(result);
        }
        return result;
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        logLine(`Analysis failed: ${errorMessage}`);
        bhaViewProvider?.setState('failed', errorMessage);
        vscode.window.showErrorMessage(`Analysis failed: ${errorMessage}`);
        return undefined;
    }
}

async function recordBuildTraces(advanced: boolean): Promise<void> {
    const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
    if (!workspaceFolder) {
        vscode.window.showErrorMessage('No workspace folder open');
        return;
    }

    const options = await promptForRecordBuildOptions(advanced);
    if (!options) {
        return;
    }

    try {
        logLine(
            `Record traces request: projectRoot=${workspaceFolder.uri.fsPath}, buildDir=${options.buildDir ?? '<auto>'}, buildSystem=${options.buildSystem ?? 'auto'}, compilers=${formatCompilerOverrides(options)}, buildType=${options.buildType ?? 'default'}, clean=${options.cleanFirst}, verbose=${options.verbose}`
        );
        const result = await runAsyncLspCommand<unknown>(
            advanced ? 'BHA: Recording build traces (advanced)' : 'BHA: Recording build traces',
            'bha.recordBuildTraces',
            {
                projectRoot: workspaceFolder.uri.fsPath,
                buildDir: options.buildDir,
                cleanFirst: options.cleanFirst,
                verbose: options.verbose,
                buildSystem: options.buildSystem,
                buildType: options.buildType,
                compiler: options.compiler,
                cCompiler: options.cCompiler,
                cxxCompiler: options.cxxCompiler,
                parallelJobs: options.parallelJobs,
                traceOutputDir: options.traceOutputDir,
                extraArgs: options.extraArgs,
                operationId: generateOperationId(advanced ? 'record-build-traces-advanced' : 'record-build-traces')
            },
            'Running traced build...'
        );

        if (!isValidRecordBuildResult(result)) {
            logLine('Record traces request returned an invalid result');
            vscode.window.showErrorMessage('Build trace recording returned invalid result');
            return;
        }

        const traceFileCount = safeGetNumber(result.traceFileCount, 0);
        const buildSystem = safeGetString(result.buildSystem, 'unknown build system');
        const buildTimeMs = safeGetNumber(result.buildTimeMs, 0);
        bhaViewProvider?.setOperationStatus(
            'Build traces recorded',
            `${traceFileCount} trace file(s) captured in ${formatDurationMs(buildTimeMs)}.`
        );
        logLine(
            `Recorded traces: buildSystem=${safeGetString(result.buildSystem, 'unknown')}, traceFileCount=${traceFileCount}, traceOutputDir=${safeGetString(result.traceOutputDir, '<auto>')}, buildTimeMs=${buildTimeMs}`
        );
        logBlock('Build output', safeGetString(result.output, ''));
        const workspaceRoot = getWorkspaceRootPath();
        if (workspaceRoot) {
            rememberWorkspaceBuildDir(workspaceRoot, safeGetString(result.buildDir, '').trim() || options.buildDir);
            const chosenTraceDir = safeGetString(result.traceOutputDir, '').trim();
            if (chosenTraceDir.length > 0) {
                lastTraceDirByWorkspace.set(workspaceRoot, chosenTraceDir);
            } else {
                lastTraceDirByWorkspace.delete(workspaceRoot);
            }
            await persistBuildProfile(workspaceRoot, {
                projectRoot: workspaceRoot,
                buildSystem: safeGetString(result.buildSystem, '').trim() || options.buildSystem,
                buildDir: safeGetString(result.buildDir, '').trim() || options.buildDir,
                buildType: safeGetString(result.buildType, '').trim() || options.buildType,
                compiler: safeGetString(result.compiler, '').trim() || options.compiler,
                cCompiler: safeGetString(result.cCompiler, '').trim() || options.cCompiler,
                cxxCompiler: safeGetString(result.cxxCompiler, '').trim() || options.cxxCompiler,
                parallelJobs: safeGetNumber(result.parallelJobs, 0) || options.parallelJobs,
                traceOutputDir: chosenTraceDir || options.traceOutputDir,
                extraArgs: options.extraArgs,
                recordedAt: new Date().toISOString(),
                buildTimeMs
            });
        }
        const message = `Build traces recorded: ${traceFileCount} trace files via ${buildSystem} in ${(buildTimeMs / 1000).toFixed(2)}s`;
        const analyzeNow = 'Analyze Now';
        const choice = await vscode.window.showInformationMessage(message, analyzeNow);
        if (choice === analyzeNow) {
            await runAnalysis(options.buildDir, false, options.traceOutputDir);
        }
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        logLine(`Build trace recording failed: ${errorMessage}`);
        vscode.window.showErrorMessage(`Build trace recording failed: ${errorMessage}`);
    }
}

async function cmdRecordBuildTraces(): Promise<void> {
    await recordBuildTraces(false);
}

async function cmdRecordBuildTracesAdvanced(): Promise<void> {
    await recordBuildTraces(true);
}

async function cmdAnalyzeProject(): Promise<void> {
    const workspaceRoot = getWorkspaceRootPath();
    const buildDir = await promptForBuildDir(workspaceRoot ? getWorkspaceBuildDir(workspaceRoot) : 'build');
    if (buildDir === undefined) {
        return;
    }
    const traceDir = workspaceRoot ? lastTraceDirByWorkspace.get(workspaceRoot) : undefined;
    await runAnalysis(buildDir || undefined, false, traceDir);
}

async function cmdShowSuggestions(): Promise<void> {
    try {
        logLine('Fetching suggestions for current analysis');
        const result = await client.sendRequest<unknown>('workspace/executeCommand', {
            command: 'bha.showMetrics',
            arguments: []
        });

        if (!isValidAnalysisResult(result)) {
            logLine('No valid analysis result available for suggestions');
            vscode.window.showInformationMessage('No valid suggestions available. Run analysis first.');
            return;
        }

        const validSuggestions = result.suggestions.filter(isValidSuggestion);
        result.suggestions = validSuggestions;

        if (validSuggestions.length > 0) {
            logLine(`Showing suggestions panel with ${validSuggestions.length} suggestions`);
            await showSuggestionsPanel(result);
        } else {
            logLine('No suggestions available to show');
            vscode.window.showInformationMessage('No suggestions available. Run analysis first.');
        }
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        logLine(`Failed to get suggestions: ${errorMessage}`);
        vscode.window.showErrorMessage(`Failed to get suggestions: ${errorMessage}`);
    }
}

async function cmdApplySuggestion(suggestionIdOrItem?: string | BhaTreeItem): Promise<void> {
    const operationId = generateOperationId('apply');
    let suggestionId = typeof suggestionIdOrItem === 'string'
        ? suggestionIdOrItem
        : suggestionIdOrItem?.suggestionId;

    if (!suggestionId) {
        const result = await client.sendRequest<unknown>('workspace/executeCommand', {
            command: 'bha.showMetrics',
            arguments: []
        });

        if (!isValidAnalysisResult(result) || result.suggestions.length === 0) {
            vscode.window.showInformationMessage('No suggestions available');
            return;
        }

        const validSuggestions = result.suggestions.filter(isValidSuggestion);
        if (validSuggestions.length === 0) {
            vscode.window.showInformationMessage('No valid suggestions available');
            return;
        }

        const items: QuickPickItemWithId[] = validSuggestions
            .slice(0, CONFIG.MAX_QUICK_PICK_ITEMS)
            .map((s: Suggestion) => ({
                label: safeGetString(s.title, 'Untitled'),
                description: `Priority: ${PRIORITY_LABELS[safeGetPriority(s.priority)]}, Confidence: ${(safeGetConfidence(s.confidence) * 100).toFixed(0)}%, Apply: ${formatApplicationMode(s.applicationMode)}`,
                detail: s.autoApplyBlockedReason
                    ? `Manual-only: ${safeGetString(s.autoApplyBlockedReason, '')}`
                    : safeGetString(s.applicationGuidance, safeGetString(s.description, '')),
                suggestionId: s.id,
                applicationMode: s.applicationMode,
                autoApplyBlockedReason: s.autoApplyBlockedReason,
                applicationGuidance: s.applicationGuidance
            }));

        const selected = await vscode.window.showQuickPick(items, {
            placeHolder: 'Select a suggestion to apply'
        });

        if (!selected) return;
        if (selected.applicationMode === 'advisory') {
            const reason = safeGetString(
                selected.autoApplyBlockedReason,
                'No safe automatic apply path is available for this suggestion.'
            );
            const guidance = safeGetString(selected.applicationGuidance, '');
            const message = guidance
                ? `${reason} ${guidance}`
                : reason;
            vscode.window.showWarningMessage(`Manual review required: ${message}`);
            return;
        }
        suggestionId = selected.suggestionId;
    }

    // Validate suggestion ID
    if (!suggestionId || suggestionId.trim().length === 0) {
        logLine('Apply suggestion aborted: invalid suggestion ID');
        vscode.window.showErrorMessage('Invalid suggestion ID');
        return;
    }

    const confirm = await vscode.window.showWarningMessage(
        'Apply this suggestion? This will modify your code.',
        { modal: true },
        'Apply'
    );

    if (confirm !== 'Apply') return;

    try {
        logLine(`Applying suggestion: id=${suggestionId}`);
        bhaViewProvider?.setState('applying', 'Applying the selected suggestion...');
        const workspaceRoot = getWorkspaceRootPath();
        const buildProfile = workspaceRoot ? getReusableBuildProfile(workspaceRoot) : undefined;
        const applyResult = await runAsyncLspCommand<unknown>(
            'BHA: Applying suggestion',
            'bha.applySuggestion',
            { suggestionId, operationId, buildProfile },
            'Applying edits and validating result...'
        );

        if (!isValidApplyResult(applyResult)) {
            logLine('Apply suggestion returned an invalid result');
            vscode.window.showErrorMessage('Apply returned invalid result');
            return;
        }

        if (applyResult.backupId && workspaceRoot) {
            await persistLastBackupId(workspaceRoot, applyResult.backupId);
        } else if (applyResult.backupId) {
            lastBackupId = applyResult.backupId;
        }

        if (applyResult.success) {
            bhaViewProvider?.setState('validating', 'Suggestion applied; validation completed. Refreshing results...');
            const numFiles = Array.isArray(applyResult.changedFiles) ? applyResult.changedFiles.length : 0;
            const trustLoopSummary = buildTrustLoopSummary(applyResult.trustLoop);
            bhaViewProvider?.setOperationStatus(
                'Suggestion applied',
                trustLoopSummary?.message ?? `Modified ${numFiles} file(s); validation succeeded.`
            );
            logLine(
                `Suggestion applied successfully: id=${suggestionId}, changedFiles=${numFiles}, backupId=${applyResult.backupId ?? '<none>'}${trustLoopSummary ? trustLoopSummary.logSuffix : ''}`
            );
            const message = trustLoopSummary
                ? `Suggestion applied successfully. Modified ${numFiles} files. ${trustLoopSummary.message}`
                : `Suggestion applied successfully. Modified ${numFiles} files.`;
            const action = await (trustLoopSummary?.regressedOrFlat
                ? vscode.window.showWarningMessage(message, 'OK', 'Revert')
                : vscode.window.showInformationMessage(message, 'OK', 'Revert'));
            if (action === 'Revert' && lastBackupId) {
                await cmdRevertChanges();
            }
        } else {
            bhaViewProvider?.setState(
                applyResult.rollback?.attempted && applyResult.rollback.success ? 'rolled-back' : 'failed',
                applyResult.rollback?.attempted && applyResult.rollback.success
                    ? 'Validation failed; the suggestion was rolled back.'
                    : 'Suggestion application failed.'
            );
            const errors = Array.isArray(applyResult.errors) ? applyResult.errors : [];
            const errorMsgs = errors.map((e) => safeGetString(e?.message, 'Unknown error'));
            const rollback = applyResult.rollback;
            bhaViewProvider?.setOperationStatus(
                rollback?.attempted && rollback.success ? 'Suggestion rolled back' : 'Suggestion failed',
                rollback?.attempted && rollback.success
                    ? 'Validation failed; the workspace was restored.'
                    : errorMsgs.join('; ') || 'No validation result was available.'
            );
            const rollbackSuffix = rollback?.attempted
                ? ` Rollback ${rollback.success ? 'succeeded' : 'failed'} (${safeGetString(rollback.reason, 'unknown')}).`
                : '';
            logLine(`Apply suggestion failed: id=${suggestionId}, errors=${errorMsgs.join('; ') || 'unknown'}, rollback=${rollback?.attempted ? safeGetString(rollback.reason, 'unknown') : 'not-attempted'}`);
            vscode.window.showErrorMessage(
                `Failed to apply suggestion: ${errorMsgs.join(', ') || 'Unknown error'}.${rollbackSuffix}`
            );
        }
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        logLine(`Failed to apply suggestion: ${errorMessage}`);
        bhaViewProvider?.setState('failed', errorMessage);
        vscode.window.showErrorMessage(`Failed to apply suggestion: ${errorMessage}`);
    }
    void bhaViewProvider?.refresh();
}

async function cmdApplyAllSuggestions(): Promise<void> {
    const operationId = generateOperationId('apply-all');

    // Get current suggestions
    const result = await client.sendRequest<unknown>('workspace/executeCommand', {
        command: 'bha.showMetrics',
        arguments: []
    });

    if (!isValidAnalysisResult(result) || result.suggestions.length === 0) {
        vscode.window.showInformationMessage('No suggestions available to apply');
        return;
    }

    const validSuggestions = result.suggestions.filter(isValidSuggestion);
    if (validSuggestions.length === 0) {
        vscode.window.showInformationMessage('No valid suggestions available');
        return;
    }

    // Filter options
    const filterChoice = await vscode.window.showQuickPick([
        { label: 'All suggestions', value: 'all' },
        { label: 'High priority only', value: 'high' },
        { label: 'High and Medium priority', value: 'high-medium' },
        { label: 'Auto-applicable only (safe)', value: 'safe' }
    ], {
        placeHolder: 'Select which suggestions to apply'
    });

    if (!filterChoice) return;

    let minPriority = 2; // Low
    let safeOnly = false;

    switch (filterChoice.value) {
        case 'high':
            minPriority = 0;
            break;
        case 'high-medium':
            minPriority = 1;
            break;
        case 'safe':
            safeOnly = true;
            break;
    }

    // Count affected suggestions
    const affectedCount = validSuggestions.filter(s => {
        if (!hasBulkApplyPath(s)) return false;
        if (safeOnly && !s.autoApplicable) return false;
        return s.priority <= minPriority;
    }).length;

    if (affectedCount === 0) {
        logLine('Apply all aborted: no suggestions matched selected criteria');
        vscode.window.showInformationMessage('No suggestions match the selected criteria');
        return;
    }

    const modeChoice = await vscode.window.showWarningMessage(
        `Apply ${affectedCount} suggestions? This will modify your code. A backup will be created for rollback.`,
        {
            modal: true,
            detail: 'Keep successful edits is recommended for bulk apply. Atomic apply rolls back everything if rebuild validation fails.'
        },
        'Keep Successful Edits',
        'Atomic Apply'
    );

    if (!modeChoice) return;
    const atomic = modeChoice === 'Atomic Apply';

    try {
        logLine(
            `Applying suggestions in bulk: affectedCount=${affectedCount}, safeOnly=${safeOnly}, minPriority=${minPriority}, atomic=${atomic}`
        );
        bhaViewProvider?.setState('applying', 'Applying selected suggestions and validating the result...');
        bhaViewProvider?.setOperationStatus(
            'Bulk apply planned',
            `${affectedCount} suggestion(s) selected by the current filter; ${atomic ? 'atomic' : 'fault-isolating'} validation requested.`,
            [
                `Selection filter: ${filterChoice.label}`,
                `Safe-only: ${safeOnly ? 'yes' : 'no'}`,
                `Validation mode: ${atomic ? 'atomic rollback' : 'keep valid edits with fault isolation'}`
            ]
        );
        const workspaceRoot = getWorkspaceRootPath();
        const buildProfile = workspaceRoot ? getReusableBuildProfile(workspaceRoot) : undefined;
        const applyResult = await runAsyncLspCommand<unknown>(
            'BHA: Applying suggestions',
            'bha.applyAllSuggestions',
            {
                minPriority,
                safeOnly,
                atomic,
                operationId,
                buildProfile
            },
            atomic
                ? 'Applying edits and validating atomically...'
                : 'Applying edits, isolating failures, and validating survivors...'
        );

        if (!isValidApplyAllResult(applyResult)) {
            logLine('Apply all returned an invalid result');
            vscode.window.showErrorMessage('Apply all returned invalid result');
            return;
        }

        if (applyResult.backupId && workspaceRoot) {
            await persistLastBackupId(workspaceRoot, applyResult.backupId);
        } else if (applyResult.backupId) {
            lastBackupId = applyResult.backupId;
        }

        if (applyResult.success) {
            bhaViewProvider?.setState('validating', 'Bulk application completed; refreshing results...');
            const errors = Array.isArray(applyResult.errors) ? applyResult.errors : [];
            const hasWarnings = errors.length > 0;
            const trustLoopSummary = buildTrustLoopSummary(applyResult.trustLoop);
            const appliedIds = Array.isArray(applyResult.appliedSuggestionIds)
                ? applyResult.appliedSuggestionIds
                : [];
            const validation = applyResult.buildValidation;
            const finalDetails = [
                `Selection: ${affectedCount} suggestion(s) from ${filterChoice.label}.`,
                `Applied: ${applyResult.appliedCount}; skipped: ${applyResult.skippedCount}; failed: ${applyResult.failedCount}.`,
                `Validation: ${validation?.ran ? (validation.success ? 'passed' : 'failed') : 'not run'}.`,
                `Rollback: ${applyResult.rollback?.attempted ? (applyResult.rollback.success ? 'succeeded' : 'failed') : 'not required'}`,
                ...(appliedIds.length > 0 ? [`Applied IDs: ${appliedIds.join(', ')}`] : []),
                ...errors.slice(0, 6).map((error) => safeGetString(error?.message, 'Unknown apply warning'))
            ];
            bhaViewProvider?.setOperationStatus(
                'Bulk apply completed',
                trustLoopSummary?.message ?? `Applied ${applyResult.appliedCount} suggestion(s); skipped ${applyResult.skippedCount}.`,
                finalDetails
            );
            logLine(
                `Apply all succeeded: applied=${applyResult.appliedCount}, skipped=${applyResult.skippedCount}, warnings=${errors.length}, backupId=${applyResult.backupId ?? '<none>'}${trustLoopSummary ? trustLoopSummary.logSuffix : ''}`
            );
            let message = `Applied ${applyResult.appliedCount} suggestions successfully.`;
            if (applyResult.skippedCount > 0) {
                message += ` Skipped ${applyResult.skippedCount}.`;
            }
            if (hasWarnings) {
                message += ` ${errors.length} suggestion(s) were not kept after validation.`;
            }
            if (trustLoopSummary) {
                message += ` ${trustLoopSummary.message}`;
            }

            const action = await (hasWarnings
                ? vscode.window.showWarningMessage(message, 'OK', 'Revert All')
                : trustLoopSummary?.regressedOrFlat
                    ? vscode.window.showWarningMessage(message, 'OK', 'Revert All')
                : vscode.window.showInformationMessage(message, 'OK', 'Revert All'));

            if (action === 'Revert All' && lastBackupId) {
                await cmdRevertChanges();
            }
        } else {
            bhaViewProvider?.setState(
                applyResult.rollback?.attempted && applyResult.rollback.success ? 'rolled-back' : 'failed',
                applyResult.rollback?.attempted && applyResult.rollback.success
                    ? 'Bulk validation failed; changes were rolled back.'
                    : 'Bulk application failed.'
            );
            // Transaction failed - should have rolled back automatically
            const failedCount = safeGetNumber(applyResult.failedCount, 0);
            const errors = Array.isArray(applyResult.errors) ? applyResult.errors : [];

            let errorDetails = '';
            if (errors.length > 0) {
                errorDetails = errors.slice(0, 3)
                    .map(e => safeGetString(e?.message, 'Unknown'))
                    .join('; ');
                if (errors.length > 3) {
                    errorDetails += ` (+${errors.length - 3} more)`;
                }
            }
            const rollback = applyResult.rollback;
            const validation = applyResult.buildValidation;
            bhaViewProvider?.setOperationStatus(
                rollback?.attempted && rollback.success ? 'Bulk apply rolled back' : 'Bulk apply failed',
                rollback?.attempted && rollback.success
                    ? 'Validation failed; the workspace was restored.'
                    : errorDetails || 'No validation result was available.',
                [
                    `Selection: ${affectedCount} suggestion(s) from ${filterChoice.label}.`,
                    `Applied: ${applyResult.appliedCount}; skipped: ${applyResult.skippedCount}; failed: ${failedCount}.`,
                    `Validation: ${validation?.ran ? (validation.success ? 'passed' : 'failed') : 'not run'}.`,
                    `Rollback: ${rollback?.attempted ? (rollback.success ? 'succeeded' : 'failed') : 'not required'}`,
                    ...errors.slice(0, 8).map((error) => safeGetString(error?.message, 'Unknown apply error'))
                ]
            );
            const rollbackDetails = rollback?.attempted
                ? ` Rollback ${rollback.success ? 'succeeded' : 'failed'} (${safeGetString(rollback.reason, 'unknown')}).`
                : '';
            logLine(`Apply all failed: failed=${failedCount}, errors=${errorDetails || 'unknown'}, rollback=${rollback?.attempted ? safeGetString(rollback.reason, 'unknown') : 'not-attempted'}`);

            vscode.window.showErrorMessage(
                `Apply all failed: ${failedCount} errors. ${errorDetails}.${rollbackDetails}`
            );
        }
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        logLine(`Failed to apply suggestions: ${errorMessage}`);
        bhaViewProvider?.setState('failed', errorMessage);
        vscode.window.showErrorMessage(`Failed to apply suggestions: ${errorMessage}`);
    }
    void bhaViewProvider?.refresh();
}

async function cmdRevertChanges(): Promise<void> {
    const operationId = generateOperationId('revert');
    const workspaceRoot = getWorkspaceRootPath();
    const backupId = await resolveBackupIdForRevert(workspaceRoot);

    if (!backupId) {
        logLine('Revert skipped: no backup available');
        vscode.window.showInformationMessage('No backup available to revert');
        return;
    }

    const confirm = await vscode.window.showWarningMessage(
        `Revert changes from backup ${backupId}?`,
        { modal: true },
        'Revert'
    );

    if (confirm !== 'Revert') return;

    try {
        logLine(`Reverting changes from backup: ${backupId}`);
        bhaViewProvider?.setState('applying', 'Restoring files from the selected backup...');
        const executeRevert = async (
            progress: vscode.Progress<{ message?: string; increment?: number }>
        ): Promise<unknown> => {
            progress.report({ message: 'Restoring files from backup...' });
            const result = await client.sendRequest('workspace/executeCommand', {
                command: 'bha.revertChanges',
                arguments: [{ backupId, operationId }]
            });
            return result as unknown;
        };
        const revertResult = await withBhaProgress(
            'BHA: Reverting changes',
            false,
            async (progress) => executeRevert(progress)
        );

        if (!isValidRevertResult(revertResult)) {
            logLine('Revert returned an invalid result');
            vscode.window.showErrorMessage('Revert returned invalid result');
            return;
        }

        if (revertResult.success) {
            bhaViewProvider?.setState('ready', 'Changes reverted; refresh the analysis to inspect the restored state.');
            const numFiles = Array.isArray(revertResult.restoredFiles) ? revertResult.restoredFiles.length : 0;
            bhaViewProvider?.setOperationStatus('Changes reverted', `Restored ${numFiles} file(s) from backup.`);
            logLine(`Revert succeeded: restoredFiles=${numFiles}`);
            vscode.window.showInformationMessage(
                `Reverted successfully. Restored ${numFiles} files.`
            );
            if (workspaceRoot && getReusableBackupId(workspaceRoot) === backupId) {
                await clearPersistedLastBackupId(workspaceRoot);
            } else if (lastBackupId === backupId) {
                lastBackupId = undefined;
            }
        } else {
            bhaViewProvider?.setState('failed', 'Revert failed; inspect the activity log for details.');
            const errors = Array.isArray(revertResult.errors) ? revertResult.errors : [];
            const errorMsgs = errors.map((e) => safeGetString(e?.message, 'Unknown error'));
            logLine(`Revert failed: ${errorMsgs.join('; ') || 'unknown'}`);
            vscode.window.showErrorMessage(
                `Failed to revert: ${errorMsgs.join(', ') || 'Unknown error'}`
            );
        }
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        logLine(`Failed to revert changes: ${errorMessage}`);
        bhaViewProvider?.setState('failed', errorMessage);
        vscode.window.showErrorMessage(`Failed to revert changes: ${errorMessage}`);
    }
    void bhaViewProvider?.refresh();
}

async function cmdRestartServer(): Promise<void> {
    if (client) {
        try {
            logLine('Restarting language server');
            await client.stop();
            await client.start();
            const traceSetting = vscode.workspace.getConfiguration('buildHotspotAnalyzer').get<string>('trace.server', 'off');
            await client.setTrace(traceSettingToProtocol(traceSetting));
            logLine(`Language server restarted (trace=${traceSetting})`);
            bhaViewProvider?.setState('starting', 'Language server restarted; refreshing analysis state...');
            await bhaViewProvider?.refresh();
            vscode.window.showInformationMessage('BHA language server restarted');
        } catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            logLine(`Failed to restart server: ${errorMessage}`);
            vscode.window.showErrorMessage(`Failed to restart server: ${errorMessage}`);
        }
    }
}

function formatEvidence(value?: string): string {
    switch (value?.toLowerCase()) {
        case 'observed':
            return 'Observed';
        case 'derived':
            return 'Derived';
        default:
            return 'Unavailable';
    }
}

function resolveSuggestionFilePath(workspaceRoot: string, candidate: string): string | undefined {
    const trimmed = candidate.trim();
    if (!trimmed) {
        return undefined;
    }
    if (trimmed.startsWith('file://')) {
        try {
            return vscode.Uri.parse(trimmed).fsPath;
        } catch {
            return undefined;
        }
    }
    return normalizeWorkspaceRelativePath(workspaceRoot, trimmed);
}

function lineColumnToOffset(content: string, line: number, column: number): number | undefined {
    if (!Number.isInteger(line) || !Number.isInteger(column) || line < 0 || column < 0) {
        return undefined;
    }

    let lineStart = 0;
    for (let currentLine = 0; currentLine < line; currentLine += 1) {
        const newline = content.indexOf('\n', lineStart);
        if (newline < 0) {
            return undefined;
        }
        lineStart = newline + 1;
    }

    const newline = content.indexOf('\n', lineStart);
    const lineEnd = newline < 0 ? content.length : newline;
    const contentLineEnd = lineEnd > lineStart && content[lineEnd - 1] === '\r'
        ? lineEnd - 1
        : lineEnd;
    if (lineStart + column > contentLineEnd) {
        return undefined;
    }
    return lineStart + column;
}

function applyPreviewEdits(content: string, edits: SuggestionTextEdit[]): string | undefined {
    const ranges = edits.map((edit, index) => {
        const start = lineColumnToOffset(content, edit.startLine, edit.startCol);
        const end = lineColumnToOffset(content, edit.endLine, edit.endCol);
        return start === undefined || end === undefined || end < start
            ? undefined
            : { start, end, newText: edit.newText, index };
    });
    if (ranges.some((range) => range === undefined)) {
        return undefined;
    }

    const ordered = ranges as Array<{ start: number; end: number; newText: string; index: number }>;
    ordered.sort((left, right) => left.start - right.start || left.end - right.end || left.index - right.index);
    for (let index = 1; index < ordered.length; index += 1) {
        if (ordered[index - 1].end > ordered[index].start) {
            return undefined;
        }
    }

    let preview = content;
    for (let index = ordered.length - 1; index >= 0; index -= 1) {
        const edit = ordered[index];
        preview = `${preview.slice(0, edit.start)}${edit.newText}${preview.slice(edit.end)}`;
    }
    return preview;
}

async function cmdPreviewSuggestion(suggestionIdOrItem?: string | BhaTreeItem): Promise<void> {
    const suggestionId = typeof suggestionIdOrItem === 'string'
        ? suggestionIdOrItem
        : suggestionIdOrItem?.suggestionId;
    if (!suggestionId || suggestionId.trim().length === 0) {
        vscode.window.showInformationMessage('Select a suggestion before previewing its diff.');
        return;
    }

    const workspaceRoot = getWorkspaceRootPath();
    if (!workspaceRoot || !bhaPreviewProvider) {
        vscode.window.showErrorMessage('BHA: A workspace is required for diff preview.');
        return;
    }

    const details = await fetchSuggestionDetails(suggestionId);
    const edits = details?.textEdits ?? [];
    if (edits.length === 0) {
        vscode.window.showInformationMessage('This suggestion has no concrete text edits to preview.');
        return;
    }

    const editsByFile = new Map<string, SuggestionTextEdit[]>();
    for (const edit of edits) {
        const filePath = resolveSuggestionFilePath(workspaceRoot, edit.file);
        if (!filePath) {
            vscode.window.showErrorMessage('BHA: The suggestion contains an invalid edit path.');
            return;
        }
        const fileEdits = editsByFile.get(filePath) ?? [];
        fileEdits.push(edit);
        editsByFile.set(filePath, fileEdits);
    }

    let opened = 0;
    for (const [filePath, fileEdits] of editsByFile) {
        const sourceUri = vscode.Uri.file(filePath);
        const original = fs.existsSync(filePath) ? fs.readFileSync(filePath, 'utf8') : '';
        const preview = applyPreviewEdits(original, fileEdits);
        if (preview === undefined) {
            vscode.window.showErrorMessage(`BHA: Could not map edits to ${path.basename(filePath)} for preview.`);
            continue;
        }

        const previewUri = sourceUri.with({ scheme: 'bha-preview' });
        bhaPreviewProvider.setContent(previewUri, preview);
        await vscode.commands.executeCommand(
            'vscode.diff',
            sourceUri,
            previewUri,
            `BHA Preview: ${path.basename(filePath)}`
        );
        opened += 1;
    }

    if (opened === 0) {
        vscode.window.showInformationMessage('BHA: No previewable files were found for this suggestion.');
    }
}

// ============================================================================
// UI Components
// ============================================================================

async function showSuggestionsPanel(result: AnalysisResult): Promise<void> {
    const panel = vscode.window.createWebviewPanel(
        'bhaSuggestions',
        'Build Optimization Suggestions',
        CONFIG.WEBVIEW_COLUMN,
        { enableScripts: true }
    );

    const suggestionDetails = await Promise.all(
        (result.suggestions || []).map(async (suggestion) => {
            const details = await fetchSuggestionDetails(suggestion.id);
            return { ...suggestion, ...details } as SuggestionDetails;
        })
    );
    const webviewNonce = crypto.randomBytes(16).toString('base64');
    const suggestions = suggestionDetails;
    const metrics = result.baselineMetrics || { totalDurationMs: 0, filesCompiled: 0 };
    const buildTiming = resolveAnalysisBuildTiming(result);
    const totalDuration = buildTiming.totalBuildTimeMs;
    const filesCompiled = safeGetNumber(result.filesAnalyzed ?? metrics.filesCompiled, 0);
    const totalBuildTimeLabel = buildTiming.source === 'recorded-build'
        ? 'Recorded Build Time'
        : buildTiming.source === 'trace-aggregate'
            ? 'Trace Aggregate Time'
            : 'Total Build Time';

    panel.webview.html = `
        <!DOCTYPE html>
        <html lang="en">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${webviewNonce}';">
            <style>
                body {
                    font-family: var(--vscode-font-family), system-ui, sans-serif;
                    color: var(--vscode-foreground);
                    background-color: var(--vscode-editor-background);
                    padding: 20px;
                    line-height: 1.5;
                }
                h1 { font-size: 24px; margin-bottom: 10px; }
                h2 { font-size: 18px; margin-top: 20px; margin-bottom: 10px; }
                .metrics {
                    background: var(--vscode-editorWidget-background);
                    padding: 15px;
                    border-radius: 8px;
                    margin-bottom: 20px;
                    border: 1px solid var(--vscode-widget-border);
                }
                .actions {
                    margin-bottom: 20px;
                    display: flex;
                    gap: 10px;
                    flex-wrap: wrap;
                }
                .suggestion {
                    background: var(--vscode-editorWidget-background);
                    border-left: 4px solid var(--vscode-activityBarBadge-background);
                    padding: 16px;
                    margin-bottom: 15px;
                    border-radius: 8px;
                    border: 1px solid var(--vscode-widget-border);
                }
                .suggestion.high { border-left-color: #f14c4c; }
                .suggestion.medium { border-left-color: #cca700; }
                .suggestion.low { border-left-color: #89d185; }
                .suggestion-header {
                    display: flex;
                    justify-content: space-between;
                    align-items: center;
                    margin-bottom: 10px;
                    flex-wrap: wrap;
                    gap: 8px;
                }
                .suggestion-title {
                    font-size: 16px;
                    font-weight: bold;
                }
                .summary {
                    margin: 10px 0 0 0;
                    color: var(--vscode-foreground);
                }
                .badge {
                    display: inline-block;
                    padding: 2px 8px;
                    border-radius: 999px;
                    font-size: 12px;
                    margin-left: 5px;
                }
                .badge.priority { background: var(--vscode-badge-background); color: var(--vscode-badge-foreground); }
                .badge.confidence { background: var(--vscode-button-secondaryBackground); }
                .badge.auto { background: #89d185; color: #000; }
                .badge.mode { background: var(--vscode-editorInfo-foreground); color: var(--vscode-editor-background); }
                .badge.manual { background: var(--vscode-button-secondaryBackground); color: var(--vscode-button-secondaryForeground); }
                .impact {
                    color: var(--vscode-descriptionForeground);
                    margin: 14px 0 0 0;
                    padding: 12px;
                    border-radius: 6px;
                    background: var(--vscode-sideBar-background);
                    display: grid;
                    grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
                    gap: 10px;
                }
                .impact-item { display: flex; flex-direction: column; gap: 3px; }
                .impact-label {
                    font-size: 11px;
                    text-transform: uppercase;
                    letter-spacing: 0.04em;
                    color: var(--vscode-descriptionForeground);
                }
                .impact-value { font-size: 14px; font-weight: 600; color: var(--vscode-foreground); }
                .meta-list {
                    display: grid;
                    gap: 6px;
                    margin-top: 12px;
                }
                .meta-item {
                    font-size: 12px;
                    color: var(--vscode-descriptionForeground);
                }
                .details {
                    margin-top: 14px;
                    border-top: 1px solid var(--vscode-widget-border);
                    padding-top: 10px;
                }
                .details summary {
                    cursor: pointer;
                    font-weight: 600;
                    color: var(--vscode-foreground);
                    list-style: none;
                }
                .details summary::-webkit-details-marker {
                    display: none;
                }
                .details summary::before {
                    content: '▸';
                    display: inline-block;
                    margin-right: 8px;
                }
                .details[open] summary::before {
                    content: '▾';
                }
                .section {
                    margin-top: 10px;
                    padding: 10px 12px;
                    border-radius: 6px;
                    background: var(--vscode-sideBar-background);
                }
                .section-title {
                    font-size: 12px;
                    font-weight: 700;
                    margin-bottom: 6px;
                    color: var(--vscode-foreground);
                }
                .section-body {
                    font-size: 12px;
                    line-height: 1.55;
                    color: var(--vscode-descriptionForeground);
                    white-space: pre-wrap;
                    word-break: break-word;
                }
                .section-body.code {
                    font-family: var(--vscode-editor-font-family), monospace;
                    color: var(--vscode-editor-foreground);
                }
                .card-actions {
                    margin-top: 14px;
                    display: flex;
                    gap: 10px;
                    flex-wrap: wrap;
                }
                button {
                    background: var(--vscode-button-background);
                    color: var(--vscode-button-foreground);
                    border: none;
                    padding: 6px 14px;
                    cursor: pointer;
                    border-radius: 2px;
                    font-size: 13px;
                }
                button:hover {
                    background: var(--vscode-button-hoverBackground);
                }
                button:disabled {
                    cursor: not-allowed;
                    opacity: 0.6;
                }
                button.secondary {
                    background: var(--vscode-button-secondaryBackground);
                    color: var(--vscode-button-secondaryForeground);
                }
                button.secondary:hover {
                    background: var(--vscode-button-secondaryHoverBackground);
                }
                .empty-state {
                    padding: 18px;
                    border-radius: 8px;
                    background: var(--vscode-editorWidget-background);
                    border: 1px solid var(--vscode-widget-border);
                    color: var(--vscode-descriptionForeground);
                }
            </style>
        </head>
        <body>
            <h1>Build Hotspot Analysis</h1>

            <div class="metrics">
                <h2>Build Metrics</h2>
                <p><strong>${totalBuildTimeLabel}:</strong> ${(totalDuration / 1000).toFixed(2)}s</p>
                <p><strong>Compilation Units Analyzed:</strong> ${filesCompiled}</p>
            </div>

            <div class="actions">
                <button onclick="applyAll('all')">Apply All</button>
                <button class="secondary" onclick="applyAll('safe')">Apply Safe Only</button>
                <button class="secondary" onclick="revertAll()">Revert Changes</button>
            </div>

            <h2>Optimization Suggestions (${suggestions.length})</h2>
            ${suggestions.length === 0 ? '<div class="empty-state">No suggestions are available for the current analysis.</div>' : ''}
            ${suggestions.map((s: SuggestionDetails) => {
                const priority = safeGetPriority(s.priority);
                const confidence = safeGetConfidence(s.confidence);
                const impact = s.estimatedImpact || { timeSavedMs: 0, percentage: 0, filesAffected: 0 };
                const timeSaved = safeGetNumber(impact.timeSavedMs, 0);
                const percentage = safeGetNumber(impact.percentage, 0);
                const filesAffected = safeGetNumber(impact.filesAffected, 0);
                const savingsEvidence = formatEvidence(s.estimatedSavingsEvidence);
                const hasSavingsEvidence = savingsEvidence !== 'Unavailable';
                const mode = formatApplicationMode(s.applicationMode);
                const isAdvisory = s.applicationMode === 'advisory';
                const buttonLabel = s.applicationMode === 'external-refactor'
                    ? 'Apply via Refactor Tool'
                    : 'Apply Suggestion';
                const guidance = safeGetString(s.applicationGuidance, '');
                const blockedReason = safeGetString(s.autoApplyBlockedReason, '');
                const summary = extractSuggestionSummary(s.description);
                const detailsHtml = renderSuggestionSections(s.description);
                const rationale = safeGetString(s.rationale, '');
                const textEditsHtml = renderTextEdits(s.textEdits);
                const filesToCreate = Array.isArray(s.filesToCreate) ? s.filesToCreate : [];
                const filesToModify = Array.isArray(s.filesToModify) ? s.filesToModify : [];

                return `
                <div class="suggestion ${PRIORITY_CLASSES[priority] || 'low'}">
                    <div class="suggestion-header">
                        <div>
                            <span class="suggestion-title">${escapeHtml(s.title)}</span>
                            <span class="badge priority">${PRIORITY_LABELS[priority] || 'Unknown'}</span>
                            <span class="badge confidence">${(confidence * 100).toFixed(0)}%</span>
                            <span class="badge ${isAdvisory ? 'manual' : 'mode'}">${escapeHtml(mode)}</span>
                            ${s.autoApplicable ? '<span class="badge auto">Auto</span>' : ''}
                        </div>
                    </div>
                    ${summary ? `<p class="summary">${escapeHtml(summary)}</p>` : ''}
                    <div class="impact">
                        <div class="impact-item">
                            <span class="impact-label">Savings Evidence</span>
                            <span class="impact-value">${savingsEvidence}</span>
                        </div>
                        <div class="impact-item">
                            <span class="impact-label">Estimated Savings</span>
                            <span class="impact-value">${hasSavingsEvidence && timeSaved > 0 ? (timeSaved / 1000).toFixed(2) + 's' : 'Unavailable'}</span>
                        </div>
                        <div class="impact-item">
                            <span class="impact-label">Build Reduction</span>
                            <span class="impact-value">${hasSavingsEvidence ? percentage.toFixed(1) + '%' : 'Unavailable'}</span>
                        </div>
                    </div>
                    <div class="meta-list">
                        ${s.refactorClassName ? `<div class="meta-item"><strong>Class:</strong> ${escapeHtml(s.refactorClassName)}</div>` : ''}
                        ${s.targetUri ? `<div class="meta-item"><strong>Target:</strong> ${escapeHtml(s.targetUri.replace('file://', ''))}</div>` : ''}
                        ${guidance ? `<div class="meta-item"><strong>Guidance:</strong> ${escapeHtml(guidance)}</div>` : ''}
                        ${rationale ? `<div class="meta-item"><strong>Rationale:</strong> ${escapeHtml(rationale)}</div>` : ''}
                        ${filesToCreate.length > 0 ? `<div class="meta-item"><strong>Files to create:</strong> ${escapeHtml(filesToCreate.join(', '))}</div>` : ''}
                        ${filesToModify.length > 0 ? `<div class="meta-item"><strong>Files to modify:</strong> ${escapeHtml(filesToModify.join(', '))}</div>` : ''}
                        <div class="meta-item"><strong>Files affected:</strong> ${filesAffected}</div>
                        <div class="meta-item"><strong>Savings status:</strong> ${savingsEvidence}${hasSavingsEvidence ? '' : ' — no measured or derived value is available'}</div>
                        ${isAdvisory ? `<div class="meta-item"><strong>Apply Mode:</strong> Manual review required${blockedReason ? ` — ${escapeHtml(blockedReason)}` : ''}</div>` : ''}
                    </div>
                    ${(detailsHtml || textEditsHtml)
                        ? `<details class="details"><summary>Suggestion Details</summary>${detailsHtml}${textEditsHtml ? `<div class="section-title" style="margin-top:12px;">Text Edits</div>${textEditsHtml}` : ''}</details>`
                        : ''}
                    <div class="card-actions">
                        ${s.textEdits && s.textEdits.length > 0
                            ? `<button class="secondary" onclick="previewSuggestion('${escapeHtml(s.id)}')">Preview Diff</button>`
                            : ''}
                        ${isAdvisory
                            ? '<button class="secondary" disabled>Manual Review Required</button>'
                            : `<button onclick="applySuggestion('${escapeHtml(s.id)}')">${escapeHtml(buttonLabel)}</button>`}
                    </div>
                </div>
            `}).join('')}

            <script nonce="${webviewNonce}">
                const vscode = acquireVsCodeApi();

                function applySuggestion(id) {
                    if (!id || id.trim() === '') {
                        console.error('Invalid suggestion ID');
                        return;
                    }
                    vscode.postMessage({
                        command: 'applySuggestion',
                        suggestionId: id
                    });
                }

                function previewSuggestion(id) {
                    if (!id || id.trim() === '') {
                        console.error('Invalid suggestion ID');
                        return;
                    }
                    vscode.postMessage({
                        command: 'previewSuggestion',
                        suggestionId: id
                    });
                }

                function applyAll(mode) {
                    vscode.postMessage({
                        command: 'applyAll',
                        mode: mode
                    });
                }

                function revertAll() {
                    vscode.postMessage({
                        command: 'revert'
                    });
                }
            </script>
        </body>
        </html>
    `;

    panel.webview.onDidReceiveMessage(
        async message => {
            if (!message || typeof message.command !== 'string') return;

            switch (message.command) {
                case 'applySuggestion':
                    if (typeof message.suggestionId === 'string' && message.suggestionId.trim()) {
                        vscode.commands.executeCommand(
                            'buildHotspotAnalyzer.applySuggestion',
                            message.suggestionId
                        );
                    }
                    break;
                case 'previewSuggestion':
                    if (typeof message.suggestionId === 'string' && message.suggestionId.trim()) {
                        vscode.commands.executeCommand(
                            'buildHotspotAnalyzer.previewSuggestion',
                            message.suggestionId
                        );
                    }
                    break;
                case 'applyAll':
                    vscode.commands.executeCommand('buildHotspotAnalyzer.applyAllSuggestions');
                    break;
                case 'revert':
                    vscode.commands.executeCommand('buildHotspotAnalyzer.revertChanges');
                    break;
            }
        }
    );
}

function escapeHtml(text: string): string {
    if (typeof text !== 'string') {
        return '';
    }
    return text
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#039;');
}

function stripMarkdownMarkers(text: string): string {
    return text
        .replace(/\*\*(.*?)\*\*/g, '$1')
        .replace(/`([^`]+)`/g, '$1');
}

function collapseWhitespace(text: string): string {
    return text.replace(/\s+/g, ' ').trim();
}

function truncateText(text: string, maxLength: number): string {
    if (text.length <= maxLength) {
        return text;
    }
    return `${text.slice(0, Math.max(0, maxLength - 1)).trimEnd()}...`;
}

function extractSuggestionSummary(description: string): string {
    const firstHeading = description.indexOf('**');
    const prefix = firstHeading >= 0 ? description.slice(0, firstHeading) : description;
    const normalizedPrefix = collapseWhitespace(stripMarkdownMarkers(prefix));
    if (normalizedPrefix.length > 0) {
        return truncateText(normalizedPrefix, 220);
    }

    const normalizedDescription = collapseWhitespace(stripMarkdownMarkers(description));
    return truncateText(normalizedDescription, 220);
}

function parseSuggestionSections(description: string): Array<{ title: string; body: string }> {
    const matches = [...description.matchAll(/\*\*(.*?)\*\*/g)];
    if (matches.length === 0) {
        const body = description.trim();
        return body.length > 0 ? [{ title: 'Details', body }] : [];
    }

    const sections: Array<{ title: string; body: string }> = [];
    for (let index = 0; index < matches.length; index += 1) {
        const title = (matches[index][1] || '').trim();
        const bodyStart = matches[index].index! + matches[index][0].length;
        const bodyEnd = index + 1 < matches.length ? matches[index + 1].index! : description.length;
        const body = description.slice(bodyStart, bodyEnd).trim();
        if (title.length === 0 && body.length === 0) {
            continue;
        }
        sections.push({ title: title || 'Details', body });
    }
    return sections;
}

function isCodeLikeSection(title: string, body: string): boolean {
    const lowerTitle = title.toLowerCase();
    return lowerTitle.includes('pattern') ||
        lowerTitle.includes('edit') ||
        lowerTitle.includes('patch') ||
        body.includes('#include') ||
        body.includes('target_') ||
        body.includes('class ') ||
        body.includes('struct ') ||
        body.includes('//');
}

function renderSuggestionSections(description: string): string {
    const sections = parseSuggestionSections(description);
    if (sections.length === 0) {
        return '';
    }

    return sections.map((section) => {
        const body = section.body.trim();
        const normalizedBody = body.length > 0 ? body : 'No additional details.';
        const escapedTitle = escapeHtml(stripMarkdownMarkers(section.title));
        const escapedBody = escapeHtml(stripMarkdownMarkers(normalizedBody));
        const bodyClass = isCodeLikeSection(section.title, normalizedBody) ? 'section-body code' : 'section-body';
        return `
            <div class="section">
                <div class="section-title">${escapedTitle}</div>
                <div class="${bodyClass}">${escapedBody}</div>
            </div>
        `;
    }).join('');
}

function renderTextEdits(edits?: SuggestionTextEdit[]): string {
    if (!Array.isArray(edits) || edits.length === 0) {
        return '';
    }

    return edits.map((edit) => {
        const location = `${escapeHtml(edit.file)}:${edit.startLine + 1}:${edit.startCol + 1}`;
        const snippet = edit.newText.trim().length > 0 ? edit.newText : '[delete]';
        return `
            <div class="section">
                <div class="section-title">${location}</div>
                <div class="section-body code">${escapeHtml(snippet)}</div>
            </div>
        `;
    }).join('');
}
