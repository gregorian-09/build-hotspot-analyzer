import * as vscode from 'vscode';
import {LanguageClient, LanguageClientOptions, ServerOptions, TransportKind} from 'vscode-languageclient/node';

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
}

interface AnalysisResult {
    suggestions: Suggestion[];
    baselineMetrics?: {
        totalDurationMs: number;
        filesCompiled: number;
    };
}

interface RecordBuildResult {
    success: boolean;
    buildSystem?: string;
    buildType?: string;
    compiler?: string;
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
    parallelJobs?: number;
    traceOutputDir?: string;
    extraArgs: string[];
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

function isValidRecordBuildResult(result: unknown): result is RecordBuildResult {
    if (!result || typeof result !== 'object') return false;
    const obj = result as Record<string, unknown>;
    return typeof obj.success === 'boolean';
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

// ============================================================================
// Client State
// ============================================================================

let client: LanguageClient;
let lastBackupId: string | undefined;

// ============================================================================
// Activation
// ============================================================================

export function activate(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('buildHotspotAnalyzer');
    const serverPath = config.get<string>('serverPath', CONFIG.DEFAULT_SERVER_PATH);

    if (!serverPath || serverPath.trim().length === 0) {
        vscode.window.showErrorMessage('BHA: Server path is not configured');
        return;
    }

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

    // Register commands
    context.subscriptions.push(
        vscode.commands.registerCommand('buildHotspotAnalyzer.recordBuildTraces', cmdRecordBuildTraces),
        vscode.commands.registerCommand('buildHotspotAnalyzer.recordBuildTracesAdvanced', cmdRecordBuildTracesAdvanced),
        vscode.commands.registerCommand('buildHotspotAnalyzer.analyzeProject', cmdAnalyzeProject),
        vscode.commands.registerCommand('buildHotspotAnalyzer.showSuggestions', cmdShowSuggestions),
        vscode.commands.registerCommand('buildHotspotAnalyzer.applySuggestion', cmdApplySuggestion),
        vscode.commands.registerCommand('buildHotspotAnalyzer.applyAllSuggestions', cmdApplyAllSuggestions),
        vscode.commands.registerCommand('buildHotspotAnalyzer.revertChanges', cmdRevertChanges),
        vscode.commands.registerCommand('buildHotspotAnalyzer.restartServer', cmdRestartServer)
    );

    client.start();

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
    return client.stop();
}

// ============================================================================
// Command Handlers
// ============================================================================

async function promptForBuildDir(): Promise<string | undefined> {
    return vscode.window.showInputBox({
        prompt: 'Build directory (optional, leave empty for auto-detect)',
        placeHolder: 'build'
    });
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
    const buildDir = await promptForBuildDir();
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

    const compiler = await vscode.window.showInputBox({
        title: 'Compiler Override',
        prompt: 'Compiler executable or absolute path (optional)',
        placeHolder: 'clang++, g++, icpx, /usr/bin/clang++'
    });
    if (compiler === undefined) {
        return undefined;
    }
    options.compiler = compiler.trim() || undefined;

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

async function runAnalysis(buildDir: string | undefined, rebuild: boolean): Promise<AnalysisResult | undefined> {
    const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
    if (!workspaceFolder) {
        vscode.window.showErrorMessage('No workspace folder open');
        return undefined;
    }

    const operationId = generateOperationId(rebuild ? 'build-and-analyze' : 'analyze');

    try {
        const result = await client.sendRequest<unknown>('workspace/executeCommand', {
            command: 'bha.analyze',
            arguments: [{
                projectRoot: workspaceFolder.uri.fsPath,
                buildDir: buildDir || undefined,
                rebuild,
                operationId
            }]
        });

        if (!isValidAnalysisResult(result)) {
            vscode.window.showErrorMessage('Analysis returned invalid result');
            return;
        }

        // Filter out invalid suggestions
        const validSuggestions = result.suggestions.filter(isValidSuggestion);
        result.suggestions = validSuggestions;

        vscode.window.showInformationMessage(
            `Analysis complete: ${validSuggestions.length} suggestions found`
        );

        if (validSuggestions.length > 0) {
            showSuggestionsPanel(result);
        }
        return result;
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
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
        const result = await client.sendRequest<unknown>('workspace/executeCommand', {
            command: 'bha.recordBuildTraces',
            arguments: [{
                projectRoot: workspaceFolder.uri.fsPath,
                buildDir: options.buildDir,
                cleanFirst: options.cleanFirst,
                verbose: options.verbose,
                buildSystem: options.buildSystem,
                buildType: options.buildType,
                compiler: options.compiler,
                parallelJobs: options.parallelJobs,
                traceOutputDir: options.traceOutputDir,
                extraArgs: options.extraArgs,
                operationId: generateOperationId(advanced ? 'record-build-traces-advanced' : 'record-build-traces')
            }]
        });

        if (!isValidRecordBuildResult(result)) {
            vscode.window.showErrorMessage('Build trace recording returned invalid result');
            return;
        }

        const traceFileCount = safeGetNumber(result.traceFileCount, 0);
        const buildSystem = safeGetString(result.buildSystem, 'unknown build system');
        const buildTimeMs = safeGetNumber(result.buildTimeMs, 0);
        const message = `Build traces recorded: ${traceFileCount} trace files via ${buildSystem} in ${(buildTimeMs / 1000).toFixed(2)}s`;
        const analyzeNow = 'Analyze Now';
        const choice = await vscode.window.showInformationMessage(message, analyzeNow);
        if (choice === analyzeNow) {
            await runAnalysis(options.buildDir, false);
        }
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
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
    const buildDir = await promptForBuildDir();
    if (buildDir === undefined) {
        return;
    }
    await runAnalysis(buildDir || undefined, false);
}

async function cmdShowSuggestions(): Promise<void> {
    try {
        const result = await client.sendRequest<unknown>('workspace/executeCommand', {
            command: 'bha.showMetrics',
            arguments: []
        });

        if (!isValidAnalysisResult(result)) {
            vscode.window.showInformationMessage('No valid suggestions available. Run analysis first.');
            return;
        }

        const validSuggestions = result.suggestions.filter(isValidSuggestion);
        result.suggestions = validSuggestions;

        if (validSuggestions.length > 0) {
            showSuggestionsPanel(result);
        } else {
            vscode.window.showInformationMessage('No suggestions available. Run analysis first.');
        }
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        vscode.window.showErrorMessage(`Failed to get suggestions: ${errorMessage}`);
    }
}

async function cmdApplySuggestion(suggestionId?: string): Promise<void> {
    const operationId = generateOperationId('apply');

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
        const applyResult = await client.sendRequest<unknown>('workspace/executeCommand', {
            command: 'bha.applySuggestion',
            arguments: [{ suggestionId, operationId }]
        });

        if (!isValidApplyResult(applyResult)) {
            vscode.window.showErrorMessage('Apply returned invalid result');
            return;
        }

        if (applyResult.success) {
            lastBackupId = applyResult.backupId;
            const numFiles = Array.isArray(applyResult.changedFiles) ? applyResult.changedFiles.length : 0;
            vscode.window.showInformationMessage(
                `Suggestion applied successfully. Modified ${numFiles} files.`
            );
        } else {
            const errors = Array.isArray(applyResult.errors) ? applyResult.errors : [];
            const errorMsgs = errors.map((e) => safeGetString(e?.message, 'Unknown error'));
            const rollback = applyResult.rollback;
            const rollbackSuffix = rollback?.attempted
                ? ` Rollback ${rollback.success ? 'succeeded' : 'failed'} (${safeGetString(rollback.reason, 'unknown')}).`
                : '';
            vscode.window.showErrorMessage(
                `Failed to apply suggestion: ${errorMsgs.join(', ') || 'Unknown error'}.${rollbackSuffix}`
            );
        }
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        vscode.window.showErrorMessage(`Failed to apply suggestion: ${errorMessage}`);
    }
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
        if (safeOnly && !s.autoApplicable) return false;
        return s.priority <= minPriority;
    }).length;

    if (affectedCount === 0) {
        vscode.window.showInformationMessage('No suggestions match the selected criteria');
        return;
    }

    const confirm = await vscode.window.showWarningMessage(
        `Apply ${affectedCount} suggestions? This will modify your code. A backup will be created for rollback.`,
        { modal: true },
        'Apply All'
    );

    if (confirm !== 'Apply All') return;

    try {
        // Use atomic apply with transaction semantics
        const applyResult = await client.sendRequest<unknown>('workspace/executeCommand', {
            command: 'bha.applyAllSuggestions',
            arguments: [{
                minPriority,
                safeOnly,
                atomic: true, // Request atomic transaction
                operationId
            }]
        });

        if (!isValidApplyAllResult(applyResult)) {
            vscode.window.showErrorMessage('Apply all returned invalid result');
            return;
        }

        lastBackupId = applyResult.backupId;

        if (applyResult.success) {
            const message = `Applied ${applyResult.appliedCount} suggestions successfully.` +
                (applyResult.skippedCount > 0 ? ` Skipped ${applyResult.skippedCount}.` : '');

            const action = await vscode.window.showInformationMessage(
                message,
                'OK',
                'Revert All'
            );

            if (action === 'Revert All' && lastBackupId) {
                await cmdRevertChanges();
            }
        } else {
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
            const rollbackDetails = rollback?.attempted
                ? ` Rollback ${rollback.success ? 'succeeded' : 'failed'} (${safeGetString(rollback.reason, 'unknown')}).`
                : '';

            vscode.window.showErrorMessage(
                `Apply all failed: ${failedCount} errors. ${errorDetails}.${rollbackDetails}`
            );
        }
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        vscode.window.showErrorMessage(`Failed to apply suggestions: ${errorMessage}`);
    }
}

async function cmdRevertChanges(): Promise<void> {
    const operationId = generateOperationId('revert');

    if (!lastBackupId) {
        vscode.window.showInformationMessage('No backup available to revert');
        return;
    }

    const confirm = await vscode.window.showWarningMessage(
        'Revert all changes from the last apply operation?',
        { modal: true },
        'Revert'
    );

    if (confirm !== 'Revert') return;

    try {
        const revertResult = await client.sendRequest<unknown>('workspace/executeCommand', {
            command: 'bha.revertChanges',
            arguments: [{ backupId: lastBackupId, operationId }]
        });

        if (!isValidRevertResult(revertResult)) {
            vscode.window.showErrorMessage('Revert returned invalid result');
            return;
        }

        if (revertResult.success) {
            const numFiles = Array.isArray(revertResult.restoredFiles) ? revertResult.restoredFiles.length : 0;
            vscode.window.showInformationMessage(
                `Reverted successfully. Restored ${numFiles} files.`
            );
            lastBackupId = undefined;
        } else {
            const errors = Array.isArray(revertResult.errors) ? revertResult.errors : [];
            const errorMsgs = errors.map((e) => safeGetString(e?.message, 'Unknown error'));
            vscode.window.showErrorMessage(
                `Failed to revert: ${errorMsgs.join(', ') || 'Unknown error'}`
            );
        }
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        vscode.window.showErrorMessage(`Failed to revert changes: ${errorMessage}`);
    }
}

async function cmdRestartServer(): Promise<void> {
    if (client) {
        try {
            await client.stop();
            await client.start();
            vscode.window.showInformationMessage('BHA language server restarted');
        } catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            vscode.window.showErrorMessage(`Failed to restart server: ${errorMessage}`);
        }
    }
}

// ============================================================================
// UI Components
// ============================================================================

function showSuggestionsPanel(result: AnalysisResult): void {
    const panel = vscode.window.createWebviewPanel(
        'bhaSuggestions',
        'Build Optimization Suggestions',
        CONFIG.WEBVIEW_COLUMN,
        { enableScripts: true }
    );

    const suggestions = result.suggestions || [];
    const metrics = result.baselineMetrics || { totalDurationMs: 0, filesCompiled: 0 };

    const totalDuration = safeGetNumber(metrics.totalDurationMs, 0);
    const filesCompiled = safeGetNumber(metrics.filesCompiled, 0);

    panel.webview.html = `
        <!DOCTYPE html>
        <html lang="en">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline';">
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
                    border-radius: 5px;
                    margin-bottom: 20px;
                }
                .actions {
                    margin-bottom: 20px;
                    display: flex;
                    gap: 10px;
                }
                .suggestion {
                    background: var(--vscode-editorWidget-background);
                    border-left: 4px solid var(--vscode-activityBarBadge-background);
                    padding: 15px;
                    margin-bottom: 15px;
                    border-radius: 3px;
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
                .badge {
                    display: inline-block;
                    padding: 2px 8px;
                    border-radius: 3px;
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
                    margin: 10px 0;
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
                button.secondary {
                    background: var(--vscode-button-secondaryBackground);
                    color: var(--vscode-button-secondaryForeground);
                }
                button.secondary:hover {
                    background: var(--vscode-button-secondaryHoverBackground);
                }
                .target-file {
                    font-size: 12px;
                    color: var(--vscode-descriptionForeground);
                    margin-top: 5px;
                }
            </style>
        </head>
        <body>
            <h1>Build Hotspot Analysis</h1>

            <div class="metrics">
                <h2>Build Metrics</h2>
                <p><strong>Total Build Time:</strong> ${(totalDuration / 1000).toFixed(2)}s</p>
                <p><strong>Files Compiled:</strong> ${filesCompiled}</p>
            </div>

            <div class="actions">
                <button onclick="applyAll('all')">Apply All</button>
                <button class="secondary" onclick="applyAll('safe')">Apply Safe Only</button>
                <button class="secondary" onclick="revertAll()">Revert Changes</button>
            </div>

            <h2>Optimization Suggestions (${suggestions.length})</h2>
            ${suggestions.map((s: Suggestion) => {
                const priority = safeGetPriority(s.priority);
                const confidence = safeGetConfidence(s.confidence);
                const impact = s.estimatedImpact || { timeSavedMs: 0, percentage: 0, filesAffected: 0 };
                const timeSaved = safeGetNumber(impact.timeSavedMs, 0);
                const percentage = safeGetNumber(impact.percentage, 0);
                const filesAffected = safeGetNumber(impact.filesAffected, 0);
                const mode = formatApplicationMode(s.applicationMode);
                const isAdvisory = s.applicationMode === 'advisory';
                const buttonLabel = s.applicationMode === 'external-refactor'
                    ? 'Apply via Refactor Tool'
                    : 'Apply Suggestion';
                const guidance = safeGetString(s.applicationGuidance, '');
                const blockedReason = safeGetString(s.autoApplyBlockedReason, '');

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
                    <p>${escapeHtml(s.description)}</p>
                    ${s.refactorClassName ? `<div class="target-file">Class: ${escapeHtml(s.refactorClassName)}</div>` : ''}
                    ${s.targetUri ? `<div class="target-file">Target: ${escapeHtml(s.targetUri.replace('file://', ''))}</div>` : ''}
                    ${guidance ? `<div class="target-file">Apply guidance: ${escapeHtml(guidance)}</div>` : ''}
                    <div class="impact">
                        <strong>Estimated Impact:</strong>
                        ${timeSaved > 0 ? (timeSaved / 1000).toFixed(2) + 's' : 'N/A'}
                        (${percentage.toFixed(1)}%)
                        • ${filesAffected} files affected
                    </div>
                    ${isAdvisory
                        ? `<div class="target-file">Manual review only.${blockedReason ? ` Blocker: ${escapeHtml(blockedReason)}` : ''}</div>`
                        : `<button onclick="applySuggestion('${escapeHtml(s.id)}')">${escapeHtml(buttonLabel)}</button>`}
                </div>
            `}).join('')}

            <script>
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
