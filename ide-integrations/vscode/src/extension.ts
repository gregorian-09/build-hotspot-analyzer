import * as vscode from 'vscode';
import {LanguageClient, LanguageClientOptions, ServerOptions, TransportKind} from 'vscode-languageclient/node';

interface Suggestion {
    id: string;
    title: string;
    description: string;
    priority: number;
    confidence: number;
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

interface ApplyResult {
    success: boolean;
    changedFiles: string[];
    errors: Array<{ message: string }>;
}

interface QuickPickItemWithId extends vscode.QuickPickItem {
    suggestionId: string;
}

let client: LanguageClient;

export function activate(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('buildHotspotAnalyzer');
    const serverPath = config.get<string>('serverPath', 'bha-lsp');

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

    context.subscriptions.push(
        vscode.commands.registerCommand('buildHotspotAnalyzer.analyzeProject', async () => {
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
                const result = await client.sendRequest<AnalysisResult>('workspace/executeCommand', {
                    command: 'bha.analyze',
                    arguments: [{
                        projectRoot: workspaceFolder.uri.fsPath,
                        buildDir: buildDir || undefined,
                        rebuild: false
                    }]
                });

                vscode.window.showInformationMessage(
                    `Analysis complete: ${result.suggestions.length} suggestions found`
                );

                if (result.suggestions.length > 0) {
                    showSuggestionsPanel(result);
                }
            } catch (error) {
                const errorMessage = error instanceof Error ? error.message : String(error);
                vscode.window.showErrorMessage(`Analysis failed: ${errorMessage}`);
            }
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('buildHotspotAnalyzer.showSuggestions', async () => {
            try {
                const result = await client.sendRequest<AnalysisResult>('workspace/executeCommand', {
                    command: 'bha.showMetrics',
                    arguments: []
                });

                if (result.suggestions && result.suggestions.length > 0) {
                    showSuggestionsPanel(result);
                } else {
                    vscode.window.showInformationMessage('No suggestions available. Run analysis first.');
                }
            } catch (error) {
                const errorMessage = error instanceof Error ? error.message : String(error);
                vscode.window.showErrorMessage(`Failed to get suggestions: ${errorMessage}`);
            }
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('buildHotspotAnalyzer.applySuggestion', async (suggestionId?: string) => {
            if (!suggestionId) {
                const result = await client.sendRequest<AnalysisResult>('workspace/executeCommand', {
                    command: 'bha.showMetrics',
                    arguments: []
                });

                if (!result.suggestions || result.suggestions.length === 0) {
                    vscode.window.showInformationMessage('No suggestions available');
                    return;
                }

                const items: QuickPickItemWithId[] = result.suggestions.map((s: Suggestion) => ({
                    label: s.title,
                    description: `Priority: ${s.priority}, Confidence: ${(s.confidence * 100).toFixed(0)}%`,
                    detail: s.description,
                    suggestionId: s.id
                }));

                const selected = await vscode.window.showQuickPick(items, {
                    placeHolder: 'Select a suggestion to apply'
                });

                if (!selected) return;
                suggestionId = selected.suggestionId;
            }

            const confirm = await vscode.window.showWarningMessage(
                'Apply this suggestion? This will modify your code.',
                { modal: true },
                'Apply'
            );

            if (confirm !== 'Apply') return;

            try {
                const applyResult = await client.sendRequest<ApplyResult>('workspace/executeCommand', {
                    command: 'bha.applySuggestion',
                    arguments: [{ suggestionId }]
                });

                if (applyResult.success) {
                    vscode.window.showInformationMessage(
                        `Suggestion applied successfully. Modified ${applyResult.changedFiles.length} files.`
                    );
                } else {
                    vscode.window.showErrorMessage(
                        `Failed to apply suggestion: ${applyResult.errors.map((e: { message: string }) => e.message).join(', ')}`
                    );
                }
            } catch (error) {
                const errorMessage = error instanceof Error ? error.message : String(error);
                vscode.window.showErrorMessage(`Failed to apply suggestion: ${errorMessage}`);
            }
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('buildHotspotAnalyzer.restartServer', async () => {
            if (client) {
                await client.stop();
                await client.start();
                vscode.window.showInformationMessage('BHA language server restarted');
            }
        })
    );

    client.start();

    if (config.get<boolean>('autoAnalyze', false)) {
        setTimeout(() => {
            vscode.commands.executeCommand('buildHotspotAnalyzer.analyzeProject');
        }, 2000);
    }
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

function showSuggestionsPanel(result: AnalysisResult) {
    const panel = vscode.window.createWebviewPanel(
        'bhaSuggestions',
        'Build Optimization Suggestions',
        vscode.ViewColumn.Two,
        { enableScripts: true }
    );

    const suggestions = result.suggestions || [];
    const metrics = result.baselineMetrics || { totalDurationMs: 0, filesCompiled: 0 };

    panel.webview.html = `
        <!DOCTYPE html>
        <html lang="">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <style>
                body {
                    font-family: var(--vscode-font-family),serif;
                    color: var(--vscode-foreground);
                    background-color: var(--vscode-editor-background);
                    padding: 20px;
                }
                h1 { font-size: 24px; margin-bottom: 10px; }
                h2 { font-size: 18px; margin-top: 20px; margin-bottom: 10px; }
                .metrics {
                    background: var(--vscode-editorWidget-background);
                    padding: 15px;
                    border-radius: 5px;
                    margin-bottom: 20px;
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
                .badge.priority { background: var(--vscode-badge-background); }
                .badge.confidence { background: var(--vscode-button-secondaryBackground); }
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
                }
                button:hover {
                    background: var(--vscode-button-hoverBackground);
                }
            </style>
        </head>
        <body>
            <h1>Build Hotspot Analysis</h1>

            <div class="metrics">
                <h2>Build Metrics</h2>
                <p><strong>Total Build Time:</strong> ${(metrics.totalDurationMs / 1000).toFixed(2)}s</p>
                <p><strong>Files Compiled:</strong> ${metrics.filesCompiled}</p>
            </div>

            <h2>Optimization Suggestions (${suggestions.length})</h2>
            ${suggestions.map((s: Suggestion) => `
                <div class="suggestion ${['high', 'medium', 'low'][s.priority]}">
                    <div class="suggestion-header">
                        <div>
                            <span class="suggestion-title">${escapeHtml(s.title)}</span>
                            <span class="badge priority">${['High', 'Medium', 'Low'][s.priority]}</span>
                            <span class="badge confidence">${(s.confidence * 100).toFixed(0)}%</span>
                        </div>
                    </div>
                    <p>${escapeHtml(s.description)}</p>
                    <div class="impact">
                        <strong>Estimated Impact:</strong>
                        ${s.estimatedImpact.timeSavedMs ? (s.estimatedImpact.timeSavedMs / 1000).toFixed(2) + 's' : 'N/A'}
                        (${s.estimatedImpact.percentage.toFixed(1)}%)
                        • ${s.estimatedImpact.filesAffected} files affected
                    </div>
                    <button onclick="applySuggestion('${s.id}')">Apply Suggestion</button>
                </div>
            `).join('')}

            <script>
                const vscode = acquireVsCodeApi();

                function applySuggestion(id) {
                    vscode.postMessage({
                        command: 'applySuggestion',
                        suggestionId: id
                    });
                }
            </script>
        </body>
        </html>
    `;

    panel.webview.onDidReceiveMessage(
        message => {
            if (message.command === 'applySuggestion') {
                vscode.commands.executeCommand(
                    'buildHotspotAnalyzer.applySuggestion',
                    message.suggestionId
                );
            }
        }
    );
}

function escapeHtml(text: string): string {
    return text
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#039;');
}
