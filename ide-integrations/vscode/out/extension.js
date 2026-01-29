"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.activate = activate;
exports.deactivate = deactivate;
const vscode = __importStar(require("vscode"));
const node_1 = require("vscode-languageclient/node");
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
};
const PRIORITY_LABELS = ['High', 'Medium', 'Low'];
const PRIORITY_CLASSES = ['high', 'medium', 'low'];
// ============================================================================
// UUID Generation
// ============================================================================
function generateUUID() {
    // RFC 4122 version 4 UUID
    return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, (c) => {
        const r = Math.random() * 16 | 0;
        const v = c === 'x' ? r : (r & 0x3 | 0x8);
        return v.toString(16);
    });
}
function generateOperationId(prefix) {
    return `${prefix}-${generateUUID()}`;
}
// ============================================================================
// Bounds Checking Utilities
// ============================================================================
function isValidSuggestion(s) {
    if (!s || typeof s !== 'object')
        return false;
    const obj = s;
    return (typeof obj.id === 'string' && obj.id.length > 0 &&
        typeof obj.title === 'string' &&
        typeof obj.description === 'string' &&
        typeof obj.priority === 'number' && obj.priority >= 0 && obj.priority <= 2 &&
        typeof obj.confidence === 'number' && obj.confidence >= 0 && obj.confidence <= 1);
}
function isValidAnalysisResult(result) {
    if (!result || typeof result !== 'object')
        return false;
    const obj = result;
    return Array.isArray(obj.suggestions);
}
function isValidApplyResult(result) {
    if (!result || typeof result !== 'object')
        return false;
    const obj = result;
    return typeof obj.success === 'boolean';
}
function isValidApplyAllResult(result) {
    if (!result || typeof result !== 'object')
        return false;
    const obj = result;
    return (typeof obj.success === 'boolean' &&
        typeof obj.appliedCount === 'number' &&
        typeof obj.skippedCount === 'number');
}
function isValidRevertResult(result) {
    if (!result || typeof result !== 'object')
        return false;
    const obj = result;
    return typeof obj.success === 'boolean';
}
function safeGetPriority(priority) {
    if (typeof priority !== 'number' || priority < 0 || priority > 2) {
        return 2; // Default to Low
    }
    return Math.floor(priority);
}
function safeGetConfidence(confidence) {
    if (typeof confidence !== 'number' || isNaN(confidence)) {
        return 0;
    }
    return Math.max(0, Math.min(1, confidence));
}
function safeGetNumber(value, defaultValue) {
    if (typeof value === 'number' && !isNaN(value)) {
        return value;
    }
    return defaultValue;
}
function safeGetString(value, defaultValue) {
    if (typeof value === 'string') {
        return value;
    }
    return defaultValue;
}
// ============================================================================
// Client State
// ============================================================================
let client;
let lastBackupId;
// ============================================================================
// Activation
// ============================================================================
function activate(context) {
    const config = vscode.workspace.getConfiguration('buildHotspotAnalyzer');
    const serverPath = config.get('serverPath', CONFIG.DEFAULT_SERVER_PATH);
    if (!serverPath || serverPath.trim().length === 0) {
        vscode.window.showErrorMessage('BHA: Server path is not configured');
        return;
    }
    const serverOptions = {
        command: serverPath,
        args: [],
        transport: node_1.TransportKind.stdio
    };
    const clientOptions = {
        documentSelector: [
            { scheme: 'file', language: 'cpp' },
            { scheme: 'file', language: 'c' }
        ],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.{cpp,cc,cxx,c,h,hpp,hxx}')
        }
    };
    client = new node_1.LanguageClient('buildHotspotAnalyzer', 'Build Hotspot Analyzer', serverOptions, clientOptions);
    // Register commands
    context.subscriptions.push(vscode.commands.registerCommand('buildHotspotAnalyzer.analyzeProject', cmdAnalyzeProject), vscode.commands.registerCommand('buildHotspotAnalyzer.showSuggestions', cmdShowSuggestions), vscode.commands.registerCommand('buildHotspotAnalyzer.applySuggestion', cmdApplySuggestion), vscode.commands.registerCommand('buildHotspotAnalyzer.applyAllSuggestions', cmdApplyAllSuggestions), vscode.commands.registerCommand('buildHotspotAnalyzer.revertChanges', cmdRevertChanges), vscode.commands.registerCommand('buildHotspotAnalyzer.restartServer', cmdRestartServer));
    client.start();
    if (config.get('autoAnalyze', false)) {
        setTimeout(() => {
            vscode.commands.executeCommand('buildHotspotAnalyzer.analyzeProject');
        }, CONFIG.AUTO_ANALYZE_DELAY_MS);
    }
}
function deactivate() {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
// ============================================================================
// Command Handlers
// ============================================================================
async function cmdAnalyzeProject() {
    const operationId = generateOperationId('analyze');
    const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
    if (!workspaceFolder) {
        vscode.window.showErrorMessage('No workspace folder open');
        return;
    }
    const buildDir = await vscode.window.showInputBox({
        prompt: 'Build directory (optional, leave empty for auto-detect)',
        placeHolder: 'build'
    });
    try {
        const result = await client.sendRequest('workspace/executeCommand', {
            command: 'bha.analyze',
            arguments: [{
                    projectRoot: workspaceFolder.uri.fsPath,
                    buildDir: buildDir || undefined,
                    rebuild: false,
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
        vscode.window.showInformationMessage(`Analysis complete: ${validSuggestions.length} suggestions found`);
        if (validSuggestions.length > 0) {
            showSuggestionsPanel(result);
        }
    }
    catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        vscode.window.showErrorMessage(`Analysis failed: ${errorMessage}`);
    }
}
async function cmdShowSuggestions() {
    try {
        const result = await client.sendRequest('workspace/executeCommand', {
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
        }
        else {
            vscode.window.showInformationMessage('No suggestions available. Run analysis first.');
        }
    }
    catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        vscode.window.showErrorMessage(`Failed to get suggestions: ${errorMessage}`);
    }
}
async function cmdApplySuggestion(suggestionId) {
    const operationId = generateOperationId('apply');
    if (!suggestionId) {
        const result = await client.sendRequest('workspace/executeCommand', {
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
        const items = validSuggestions
            .slice(0, CONFIG.MAX_QUICK_PICK_ITEMS)
            .map((s) => ({
            label: safeGetString(s.title, 'Untitled'),
            description: `Priority: ${PRIORITY_LABELS[safeGetPriority(s.priority)]}, Confidence: ${(safeGetConfidence(s.confidence) * 100).toFixed(0)}%`,
            detail: safeGetString(s.description, ''),
            suggestionId: s.id
        }));
        const selected = await vscode.window.showQuickPick(items, {
            placeHolder: 'Select a suggestion to apply'
        });
        if (!selected)
            return;
        suggestionId = selected.suggestionId;
    }
    // Validate suggestion ID
    if (!suggestionId || suggestionId.trim().length === 0) {
        vscode.window.showErrorMessage('Invalid suggestion ID');
        return;
    }
    const confirm = await vscode.window.showWarningMessage('Apply this suggestion? This will modify your code.', { modal: true }, 'Apply');
    if (confirm !== 'Apply')
        return;
    try {
        const applyResult = await client.sendRequest('workspace/executeCommand', {
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
            vscode.window.showInformationMessage(`Suggestion applied successfully. Modified ${numFiles} files.`);
        }
        else {
            const errors = Array.isArray(applyResult.errors) ? applyResult.errors : [];
            const errorMsgs = errors.map((e) => safeGetString(e?.message, 'Unknown error'));
            vscode.window.showErrorMessage(`Failed to apply suggestion: ${errorMsgs.join(', ') || 'Unknown error'}`);
        }
    }
    catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        vscode.window.showErrorMessage(`Failed to apply suggestion: ${errorMessage}`);
    }
}
async function cmdApplyAllSuggestions() {
    const operationId = generateOperationId('apply-all');
    // Get current suggestions
    const result = await client.sendRequest('workspace/executeCommand', {
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
    if (!filterChoice)
        return;
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
        if (safeOnly && !s.autoApplicable)
            return false;
        return s.priority <= minPriority;
    }).length;
    if (affectedCount === 0) {
        vscode.window.showInformationMessage('No suggestions match the selected criteria');
        return;
    }
    const confirm = await vscode.window.showWarningMessage(`Apply ${affectedCount} suggestions? This will modify your code. A backup will be created for rollback.`, { modal: true }, 'Apply All');
    if (confirm !== 'Apply All')
        return;
    try {
        // Use atomic apply with transaction semantics
        const applyResult = await client.sendRequest('workspace/executeCommand', {
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
            const action = await vscode.window.showInformationMessage(message, 'OK', 'Revert All');
            if (action === 'Revert All' && lastBackupId) {
                await cmdRevertChanges();
            }
        }
        else {
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
            vscode.window.showErrorMessage(`Apply all failed: ${failedCount} errors. ${errorDetails}. Changes have been rolled back.`);
        }
    }
    catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        vscode.window.showErrorMessage(`Failed to apply suggestions: ${errorMessage}`);
    }
}
async function cmdRevertChanges() {
    const operationId = generateOperationId('revert');
    if (!lastBackupId) {
        vscode.window.showInformationMessage('No backup available to revert');
        return;
    }
    const confirm = await vscode.window.showWarningMessage('Revert all changes from the last apply operation?', { modal: true }, 'Revert');
    if (confirm !== 'Revert')
        return;
    try {
        const revertResult = await client.sendRequest('workspace/executeCommand', {
            command: 'bha.revertChanges',
            arguments: [{ backupId: lastBackupId, operationId }]
        });
        if (!isValidRevertResult(revertResult)) {
            vscode.window.showErrorMessage('Revert returned invalid result');
            return;
        }
        if (revertResult.success) {
            const numFiles = Array.isArray(revertResult.restoredFiles) ? revertResult.restoredFiles.length : 0;
            vscode.window.showInformationMessage(`Reverted successfully. Restored ${numFiles} files.`);
            lastBackupId = undefined;
        }
        else {
            const errors = Array.isArray(revertResult.errors) ? revertResult.errors : [];
            const errorMsgs = errors.map((e) => safeGetString(e?.message, 'Unknown error'));
            vscode.window.showErrorMessage(`Failed to revert: ${errorMsgs.join(', ') || 'Unknown error'}`);
        }
    }
    catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        vscode.window.showErrorMessage(`Failed to revert changes: ${errorMessage}`);
    }
}
async function cmdRestartServer() {
    if (client) {
        try {
            await client.stop();
            await client.start();
            vscode.window.showInformationMessage('BHA language server restarted');
        }
        catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            vscode.window.showErrorMessage(`Failed to restart server: ${errorMessage}`);
        }
    }
}
// ============================================================================
// UI Components
// ============================================================================
function showSuggestionsPanel(result) {
    const panel = vscode.window.createWebviewPanel('bhaSuggestions', 'Build Optimization Suggestions', CONFIG.WEBVIEW_COLUMN, { enableScripts: true });
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
            ${suggestions.map((s) => {
        const priority = safeGetPriority(s.priority);
        const confidence = safeGetConfidence(s.confidence);
        const impact = s.estimatedImpact || { timeSavedMs: 0, percentage: 0, filesAffected: 0 };
        const timeSaved = safeGetNumber(impact.timeSavedMs, 0);
        const percentage = safeGetNumber(impact.percentage, 0);
        const filesAffected = safeGetNumber(impact.filesAffected, 0);
        return `
                <div class="suggestion ${PRIORITY_CLASSES[priority] || 'low'}">
                    <div class="suggestion-header">
                        <div>
                            <span class="suggestion-title">${escapeHtml(s.title)}</span>
                            <span class="badge priority">${PRIORITY_LABELS[priority] || 'Unknown'}</span>
                            <span class="badge confidence">${(confidence * 100).toFixed(0)}%</span>
                            ${s.autoApplicable ? '<span class="badge auto">Auto</span>' : ''}
                        </div>
                    </div>
                    <p>${escapeHtml(s.description)}</p>
                    ${s.targetUri ? `<div class="target-file">Target: ${escapeHtml(s.targetUri.replace('file://', ''))}</div>` : ''}
                    <div class="impact">
                        <strong>Estimated Impact:</strong>
                        ${timeSaved > 0 ? (timeSaved / 1000).toFixed(2) + 's' : 'N/A'}
                        (${percentage.toFixed(1)}%)
                        • ${filesAffected} files affected
                    </div>
                    <button onclick="applySuggestion('${escapeHtml(s.id)}')">Apply Suggestion</button>
                </div>
            `;
    }).join('')}

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
    panel.webview.onDidReceiveMessage(async (message) => {
        if (!message || typeof message.command !== 'string')
            return;
        switch (message.command) {
            case 'applySuggestion':
                if (typeof message.suggestionId === 'string' && message.suggestionId.trim()) {
                    vscode.commands.executeCommand('buildHotspotAnalyzer.applySuggestion', message.suggestionId);
                }
                break;
            case 'applyAll':
                vscode.commands.executeCommand('buildHotspotAnalyzer.applyAllSuggestions');
                break;
            case 'revert':
                vscode.commands.executeCommand('buildHotspotAnalyzer.revertChanges');
                break;
        }
    });
}
function escapeHtml(text) {
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
//# sourceMappingURL=extension.js.map