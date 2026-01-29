"use strict";
var __awaiter = (this && this.__awaiter) || function (thisArg, _arguments, P, generator) {
    function adopt(value) { return value instanceof P ? value : new P(function (resolve) { resolve(value); }); }
    return new (P || (P = Promise))(function (resolve, reject) {
        function fulfilled(value) { try { step(generator.next(value)); } catch (e) { reject(e); } }
        function rejected(value) { try { step(generator["throw"](value)); } catch (e) { reject(e); } }
        function step(result) { result.done ? resolve(result.value) : adopt(result.value).then(fulfilled, rejected); }
        step((generator = generator.apply(thisArg, _arguments || [])).next());
    });
};
var __generator = (this && this.__generator) || function (thisArg, body) {
    var _ = { label: 0, sent: function() { if (t[0] & 1) throw t[1]; return t[1]; }, trys: [], ops: [] }, f, y, t, g = Object.create((typeof Iterator === "function" ? Iterator : Object).prototype);
    return g.next = verb(0), g["throw"] = verb(1), g["return"] = verb(2), typeof Symbol === "function" && (g[Symbol.iterator] = function() { return this; }), g;
    function verb(n) { return function (v) { return step([n, v]); }; }
    function step(op) {
        if (f) throw new TypeError("Generator is already executing.");
        while (g && (g = 0, op[0] && (_ = 0)), _) try {
            if (f = 1, y && (t = op[0] & 2 ? y["return"] : op[0] ? y["throw"] || ((t = y["return"]) && t.call(y), 0) : y.next) && !(t = t.call(y, op[1])).done) return t;
            if (y = 0, t) op = [op[0] & 2, t.value];
            switch (op[0]) {
                case 0: case 1: t = op; break;
                case 4: _.label++; return { value: op[1], done: false };
                case 5: _.label++; y = op[1]; op = [0]; continue;
                case 7: op = _.ops.pop(); _.trys.pop(); continue;
                default:
                    if (!(t = _.trys, t = t.length > 0 && t[t.length - 1]) && (op[0] === 6 || op[0] === 2)) { _ = 0; continue; }
                    if (op[0] === 3 && (!t || (op[1] > t[0] && op[1] < t[3]))) { _.label = op[1]; break; }
                    if (op[0] === 6 && _.label < t[1]) { _.label = t[1]; t = op; break; }
                    if (t && _.label < t[2]) { _.label = t[2]; _.ops.push(op); break; }
                    if (t[2]) _.ops.pop();
                    _.trys.pop(); continue;
            }
            op = body.call(thisArg, _);
        } catch (e) { op = [6, e]; y = 0; } finally { f = t = 0; }
        if (op[0] & 5) throw op[1]; return { value: op[0] ? op[1] : void 0, done: true };
    }
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.activate = activate;
exports.deactivate = deactivate;
var vscode = require("vscode");
var node_1 = require("vscode-languageclient/node");
// ============================================================================
// Configuration Constants
// ============================================================================
var CONFIG = {
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
var PRIORITY_LABELS = ['High', 'Medium', 'Low'];
var PRIORITY_CLASSES = ['high', 'medium', 'low'];
// ============================================================================
// UUID Generation
// ============================================================================
function generateUUID() {
    // RFC 4122 version 4 UUID
    return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function (c) {
        var r = Math.random() * 16 | 0;
        var v = c === 'x' ? r : (r & 0x3 | 0x8);
        return v.toString(16);
    });
}
function generateOperationId(prefix) {
    return "".concat(prefix, "-").concat(generateUUID());
}
// ============================================================================
// Bounds Checking Utilities
// ============================================================================
function isValidSuggestion(s) {
    if (!s || typeof s !== 'object')
        return false;
    var obj = s;
    return (typeof obj.id === 'string' && obj.id.length > 0 &&
        typeof obj.title === 'string' &&
        typeof obj.description === 'string' &&
        typeof obj.priority === 'number' && obj.priority >= 0 && obj.priority <= 2 &&
        typeof obj.confidence === 'number' && obj.confidence >= 0 && obj.confidence <= 1);
}
function isValidAnalysisResult(result) {
    if (!result || typeof result !== 'object')
        return false;
    var obj = result;
    return Array.isArray(obj.suggestions);
}
function isValidApplyResult(result) {
    if (!result || typeof result !== 'object')
        return false;
    var obj = result;
    return typeof obj.success === 'boolean';
}
function isValidApplyAllResult(result) {
    if (!result || typeof result !== 'object')
        return false;
    var obj = result;
    return (typeof obj.success === 'boolean' &&
        typeof obj.appliedCount === 'number' &&
        typeof obj.skippedCount === 'number');
}
function isValidRevertResult(result) {
    if (!result || typeof result !== 'object')
        return false;
    var obj = result;
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
var client;
var lastBackupId;
// ============================================================================
// Activation
// ============================================================================
function activate(context) {
    var config = vscode.workspace.getConfiguration('buildHotspotAnalyzer');
    var serverPath = config.get('serverPath', CONFIG.DEFAULT_SERVER_PATH);
    if (!serverPath || serverPath.trim().length === 0) {
        vscode.window.showErrorMessage('BHA: Server path is not configured');
        return;
    }
    var serverOptions = {
        command: serverPath,
        args: [],
        transport: node_1.TransportKind.stdio
    };
    var clientOptions = {
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
        setTimeout(function () {
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
function cmdAnalyzeProject() {
    return __awaiter(this, void 0, void 0, function () {
        var operationId, workspaceFolder, buildDir, result, validSuggestions, error_1, errorMessage;
        var _a;
        return __generator(this, function (_b) {
            switch (_b.label) {
                case 0:
                    operationId = generateOperationId('analyze');
                    workspaceFolder = (_a = vscode.workspace.workspaceFolders) === null || _a === void 0 ? void 0 : _a[0];
                    if (!workspaceFolder) {
                        vscode.window.showErrorMessage('No workspace folder open');
                        return [2 /*return*/];
                    }
                    return [4 /*yield*/, vscode.window.showInputBox({
                            prompt: 'Build directory (optional, leave empty for auto-detect)',
                            placeHolder: 'build'
                        })];
                case 1:
                    buildDir = _b.sent();
                    _b.label = 2;
                case 2:
                    _b.trys.push([2, 4, , 5]);
                    return [4 /*yield*/, client.sendRequest('workspace/executeCommand', {
                            command: 'bha.analyze',
                            arguments: [{
                                    projectRoot: workspaceFolder.uri.fsPath,
                                    buildDir: buildDir || undefined,
                                    rebuild: false,
                                    operationId: operationId
                                }]
                        })];
                case 3:
                    result = _b.sent();
                    if (!isValidAnalysisResult(result)) {
                        vscode.window.showErrorMessage('Analysis returned invalid result');
                        return [2 /*return*/];
                    }
                    validSuggestions = result.suggestions.filter(isValidSuggestion);
                    result.suggestions = validSuggestions;
                    vscode.window.showInformationMessage("Analysis complete: ".concat(validSuggestions.length, " suggestions found"));
                    if (validSuggestions.length > 0) {
                        showSuggestionsPanel(result);
                    }
                    return [3 /*break*/, 5];
                case 4:
                    error_1 = _b.sent();
                    errorMessage = error_1 instanceof Error ? error_1.message : String(error_1);
                    vscode.window.showErrorMessage("Analysis failed: ".concat(errorMessage));
                    return [3 /*break*/, 5];
                case 5: return [2 /*return*/];
            }
        });
    });
}
function cmdShowSuggestions() {
    return __awaiter(this, void 0, void 0, function () {
        var result, validSuggestions, error_2, errorMessage;
        return __generator(this, function (_a) {
            switch (_a.label) {
                case 0:
                    _a.trys.push([0, 2, , 3]);
                    return [4 /*yield*/, client.sendRequest('workspace/executeCommand', {
                            command: 'bha.showMetrics',
                            arguments: []
                        })];
                case 1:
                    result = _a.sent();
                    if (!isValidAnalysisResult(result)) {
                        vscode.window.showInformationMessage('No valid suggestions available. Run analysis first.');
                        return [2 /*return*/];
                    }
                    validSuggestions = result.suggestions.filter(isValidSuggestion);
                    result.suggestions = validSuggestions;
                    if (validSuggestions.length > 0) {
                        showSuggestionsPanel(result);
                    }
                    else {
                        vscode.window.showInformationMessage('No suggestions available. Run analysis first.');
                    }
                    return [3 /*break*/, 3];
                case 2:
                    error_2 = _a.sent();
                    errorMessage = error_2 instanceof Error ? error_2.message : String(error_2);
                    vscode.window.showErrorMessage("Failed to get suggestions: ".concat(errorMessage));
                    return [3 /*break*/, 3];
                case 3: return [2 /*return*/];
            }
        });
    });
}
function cmdApplySuggestion(suggestionId) {
    return __awaiter(this, void 0, void 0, function () {
        var operationId, result, validSuggestions, items, selected, confirm, applyResult, numFiles, errors, errorMsgs, error_3, errorMessage;
        return __generator(this, function (_a) {
            switch (_a.label) {
                case 0:
                    operationId = generateOperationId('apply');
                    if (!!suggestionId) return [3 /*break*/, 3];
                    return [4 /*yield*/, client.sendRequest('workspace/executeCommand', {
                            command: 'bha.showMetrics',
                            arguments: []
                        })];
                case 1:
                    result = _a.sent();
                    if (!isValidAnalysisResult(result) || result.suggestions.length === 0) {
                        vscode.window.showInformationMessage('No suggestions available');
                        return [2 /*return*/];
                    }
                    validSuggestions = result.suggestions.filter(isValidSuggestion);
                    if (validSuggestions.length === 0) {
                        vscode.window.showInformationMessage('No valid suggestions available');
                        return [2 /*return*/];
                    }
                    items = validSuggestions
                        .slice(0, CONFIG.MAX_QUICK_PICK_ITEMS)
                        .map(function (s) { return ({
                        label: safeGetString(s.title, 'Untitled'),
                        description: "Priority: ".concat(PRIORITY_LABELS[safeGetPriority(s.priority)], ", Confidence: ").concat((safeGetConfidence(s.confidence) * 100).toFixed(0), "%"),
                        detail: safeGetString(s.description, ''),
                        suggestionId: s.id
                    }); });
                    return [4 /*yield*/, vscode.window.showQuickPick(items, {
                            placeHolder: 'Select a suggestion to apply'
                        })];
                case 2:
                    selected = _a.sent();
                    if (!selected)
                        return [2 /*return*/];
                    suggestionId = selected.suggestionId;
                    _a.label = 3;
                case 3:
                    // Validate suggestion ID
                    if (!suggestionId || suggestionId.trim().length === 0) {
                        vscode.window.showErrorMessage('Invalid suggestion ID');
                        return [2 /*return*/];
                    }
                    return [4 /*yield*/, vscode.window.showWarningMessage('Apply this suggestion? This will modify your code.', { modal: true }, 'Apply')];
                case 4:
                    confirm = _a.sent();
                    if (confirm !== 'Apply')
                        return [2 /*return*/];
                    _a.label = 5;
                case 5:
                    _a.trys.push([5, 7, , 8]);
                    return [4 /*yield*/, client.sendRequest('workspace/executeCommand', {
                            command: 'bha.applySuggestion',
                            arguments: [{ suggestionId: suggestionId, operationId: operationId }]
                        })];
                case 6:
                    applyResult = _a.sent();
                    if (!isValidApplyResult(applyResult)) {
                        vscode.window.showErrorMessage('Apply returned invalid result');
                        return [2 /*return*/];
                    }
                    if (applyResult.success) {
                        lastBackupId = applyResult.backupId;
                        numFiles = Array.isArray(applyResult.changedFiles) ? applyResult.changedFiles.length : 0;
                        vscode.window.showInformationMessage("Suggestion applied successfully. Modified ".concat(numFiles, " files."));
                    }
                    else {
                        errors = Array.isArray(applyResult.errors) ? applyResult.errors : [];
                        errorMsgs = errors.map(function (e) { return safeGetString(e === null || e === void 0 ? void 0 : e.message, 'Unknown error'); });
                        vscode.window.showErrorMessage("Failed to apply suggestion: ".concat(errorMsgs.join(', ') || 'Unknown error'));
                    }
                    return [3 /*break*/, 8];
                case 7:
                    error_3 = _a.sent();
                    errorMessage = error_3 instanceof Error ? error_3.message : String(error_3);
                    vscode.window.showErrorMessage("Failed to apply suggestion: ".concat(errorMessage));
                    return [3 /*break*/, 8];
                case 8: return [2 /*return*/];
            }
        });
    });
}
function cmdApplyAllSuggestions() {
    return __awaiter(this, void 0, void 0, function () {
        var operationId, result, validSuggestions, filterChoice, minPriority, safeOnly, affectedCount, confirm, applyResult, message, action, failedCount, errors, errorDetails, error_4, errorMessage;
        return __generator(this, function (_a) {
            switch (_a.label) {
                case 0:
                    operationId = generateOperationId('apply-all');
                    return [4 /*yield*/, client.sendRequest('workspace/executeCommand', {
                            command: 'bha.showMetrics',
                            arguments: []
                        })];
                case 1:
                    result = _a.sent();
                    if (!isValidAnalysisResult(result) || result.suggestions.length === 0) {
                        vscode.window.showInformationMessage('No suggestions available to apply');
                        return [2 /*return*/];
                    }
                    validSuggestions = result.suggestions.filter(isValidSuggestion);
                    if (validSuggestions.length === 0) {
                        vscode.window.showInformationMessage('No valid suggestions available');
                        return [2 /*return*/];
                    }
                    return [4 /*yield*/, vscode.window.showQuickPick([
                            { label: 'All suggestions', value: 'all' },
                            { label: 'High priority only', value: 'high' },
                            { label: 'High and Medium priority', value: 'high-medium' },
                            { label: 'Auto-applicable only (safe)', value: 'safe' }
                        ], {
                            placeHolder: 'Select which suggestions to apply'
                        })];
                case 2:
                    filterChoice = _a.sent();
                    if (!filterChoice)
                        return [2 /*return*/];
                    minPriority = 2;
                    safeOnly = false;
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
                    affectedCount = validSuggestions.filter(function (s) {
                        if (safeOnly && !s.autoApplicable)
                            return false;
                        return s.priority <= minPriority;
                    }).length;
                    if (affectedCount === 0) {
                        vscode.window.showInformationMessage('No suggestions match the selected criteria');
                        return [2 /*return*/];
                    }
                    return [4 /*yield*/, vscode.window.showWarningMessage("Apply ".concat(affectedCount, " suggestions? This will modify your code. A backup will be created for rollback."), { modal: true }, 'Apply All')];
                case 3:
                    confirm = _a.sent();
                    if (confirm !== 'Apply All')
                        return [2 /*return*/];
                    _a.label = 4;
                case 4:
                    _a.trys.push([4, 11, , 12]);
                    return [4 /*yield*/, client.sendRequest('workspace/executeCommand', {
                            command: 'bha.applyAllSuggestions',
                            arguments: [{
                                    minPriority: minPriority,
                                    safeOnly: safeOnly,
                                    atomic: true, // Request atomic transaction
                                    operationId: operationId
                                }]
                        })];
                case 5:
                    applyResult = _a.sent();
                    if (!isValidApplyAllResult(applyResult)) {
                        vscode.window.showErrorMessage('Apply all returned invalid result');
                        return [2 /*return*/];
                    }
                    lastBackupId = applyResult.backupId;
                    if (!applyResult.success) return [3 /*break*/, 9];
                    message = "Applied ".concat(applyResult.appliedCount, " suggestions successfully.") +
                        (applyResult.skippedCount > 0 ? " Skipped ".concat(applyResult.skippedCount, ".") : '');
                    return [4 /*yield*/, vscode.window.showInformationMessage(message, 'OK', 'Revert All')];
                case 6:
                    action = _a.sent();
                    if (!(action === 'Revert All' && lastBackupId)) return [3 /*break*/, 8];
                    return [4 /*yield*/, cmdRevertChanges()];
                case 7:
                    _a.sent();
                    _a.label = 8;
                case 8: return [3 /*break*/, 10];
                case 9:
                    failedCount = safeGetNumber(applyResult.failedCount, 0);
                    errors = Array.isArray(applyResult.errors) ? applyResult.errors : [];
                    errorDetails = '';
                    if (errors.length > 0) {
                        errorDetails = errors.slice(0, 3)
                            .map(function (e) { return safeGetString(e === null || e === void 0 ? void 0 : e.message, 'Unknown'); })
                            .join('; ');
                        if (errors.length > 3) {
                            errorDetails += " (+".concat(errors.length - 3, " more)");
                        }
                    }
                    vscode.window.showErrorMessage("Apply all failed: ".concat(failedCount, " errors. ").concat(errorDetails, ". Changes have been rolled back."));
                    _a.label = 10;
                case 10: return [3 /*break*/, 12];
                case 11:
                    error_4 = _a.sent();
                    errorMessage = error_4 instanceof Error ? error_4.message : String(error_4);
                    vscode.window.showErrorMessage("Failed to apply suggestions: ".concat(errorMessage));
                    return [3 /*break*/, 12];
                case 12: return [2 /*return*/];
            }
        });
    });
}
function cmdRevertChanges() {
    return __awaiter(this, void 0, void 0, function () {
        var operationId, confirm, revertResult, numFiles, errors, errorMsgs, error_5, errorMessage;
        return __generator(this, function (_a) {
            switch (_a.label) {
                case 0:
                    operationId = generateOperationId('revert');
                    if (!lastBackupId) {
                        vscode.window.showInformationMessage('No backup available to revert');
                        return [2 /*return*/];
                    }
                    return [4 /*yield*/, vscode.window.showWarningMessage('Revert all changes from the last apply operation?', { modal: true }, 'Revert')];
                case 1:
                    confirm = _a.sent();
                    if (confirm !== 'Revert')
                        return [2 /*return*/];
                    _a.label = 2;
                case 2:
                    _a.trys.push([2, 4, , 5]);
                    return [4 /*yield*/, client.sendRequest('workspace/executeCommand', {
                            command: 'bha.revertChanges',
                            arguments: [{ backupId: lastBackupId, operationId: operationId }]
                        })];
                case 3:
                    revertResult = _a.sent();
                    if (!isValidRevertResult(revertResult)) {
                        vscode.window.showErrorMessage('Revert returned invalid result');
                        return [2 /*return*/];
                    }
                    if (revertResult.success) {
                        numFiles = Array.isArray(revertResult.restoredFiles) ? revertResult.restoredFiles.length : 0;
                        vscode.window.showInformationMessage("Reverted successfully. Restored ".concat(numFiles, " files."));
                        lastBackupId = undefined;
                    }
                    else {
                        errors = Array.isArray(revertResult.errors) ? revertResult.errors : [];
                        errorMsgs = errors.map(function (e) { return safeGetString(e === null || e === void 0 ? void 0 : e.message, 'Unknown error'); });
                        vscode.window.showErrorMessage("Failed to revert: ".concat(errorMsgs.join(', ') || 'Unknown error'));
                    }
                    return [3 /*break*/, 5];
                case 4:
                    error_5 = _a.sent();
                    errorMessage = error_5 instanceof Error ? error_5.message : String(error_5);
                    vscode.window.showErrorMessage("Failed to revert changes: ".concat(errorMessage));
                    return [3 /*break*/, 5];
                case 5: return [2 /*return*/];
            }
        });
    });
}
function cmdRestartServer() {
    return __awaiter(this, void 0, void 0, function () {
        var error_6, errorMessage;
        return __generator(this, function (_a) {
            switch (_a.label) {
                case 0:
                    if (!client) return [3 /*break*/, 5];
                    _a.label = 1;
                case 1:
                    _a.trys.push([1, 4, , 5]);
                    return [4 /*yield*/, client.stop()];
                case 2:
                    _a.sent();
                    return [4 /*yield*/, client.start()];
                case 3:
                    _a.sent();
                    vscode.window.showInformationMessage('BHA language server restarted');
                    return [3 /*break*/, 5];
                case 4:
                    error_6 = _a.sent();
                    errorMessage = error_6 instanceof Error ? error_6.message : String(error_6);
                    vscode.window.showErrorMessage("Failed to restart server: ".concat(errorMessage));
                    return [3 /*break*/, 5];
                case 5: return [2 /*return*/];
            }
        });
    });
}
// ============================================================================
// UI Components
// ============================================================================
function showSuggestionsPanel(result) {
    var _this = this;
    var panel = vscode.window.createWebviewPanel('bhaSuggestions', 'Build Optimization Suggestions', CONFIG.WEBVIEW_COLUMN, { enableScripts: true });
    var suggestions = result.suggestions || [];
    var metrics = result.baselineMetrics || { totalDurationMs: 0, filesCompiled: 0 };
    var totalDuration = safeGetNumber(metrics.totalDurationMs, 0);
    var filesCompiled = safeGetNumber(metrics.filesCompiled, 0);
    panel.webview.html = "\n        <!DOCTYPE html>\n        <html lang=\"en\">\n        <head>\n            <meta charset=\"UTF-8\">\n            <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n            <meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline';\">\n            <style>\n                body {\n                    font-family: var(--vscode-font-family), system-ui, sans-serif;\n                    color: var(--vscode-foreground);\n                    background-color: var(--vscode-editor-background);\n                    padding: 20px;\n                    line-height: 1.5;\n                }\n                h1 { font-size: 24px; margin-bottom: 10px; }\n                h2 { font-size: 18px; margin-top: 20px; margin-bottom: 10px; }\n                .metrics {\n                    background: var(--vscode-editorWidget-background);\n                    padding: 15px;\n                    border-radius: 5px;\n                    margin-bottom: 20px;\n                }\n                .actions {\n                    margin-bottom: 20px;\n                    display: flex;\n                    gap: 10px;\n                }\n                .suggestion {\n                    background: var(--vscode-editorWidget-background);\n                    border-left: 4px solid var(--vscode-activityBarBadge-background);\n                    padding: 15px;\n                    margin-bottom: 15px;\n                    border-radius: 3px;\n                }\n                .suggestion.high { border-left-color: #f14c4c; }\n                .suggestion.medium { border-left-color: #cca700; }\n                .suggestion.low { border-left-color: #89d185; }\n                .suggestion-header {\n                    display: flex;\n                    justify-content: space-between;\n                    align-items: center;\n                    margin-bottom: 10px;\n                    flex-wrap: wrap;\n                    gap: 8px;\n                }\n                .suggestion-title {\n                    font-size: 16px;\n                    font-weight: bold;\n                }\n                .badge {\n                    display: inline-block;\n                    padding: 2px 8px;\n                    border-radius: 3px;\n                    font-size: 12px;\n                    margin-left: 5px;\n                }\n                .badge.priority { background: var(--vscode-badge-background); color: var(--vscode-badge-foreground); }\n                .badge.confidence { background: var(--vscode-button-secondaryBackground); }\n                .badge.auto { background: #89d185; color: #000; }\n                .impact {\n                    color: var(--vscode-descriptionForeground);\n                    margin: 10px 0;\n                }\n                button {\n                    background: var(--vscode-button-background);\n                    color: var(--vscode-button-foreground);\n                    border: none;\n                    padding: 6px 14px;\n                    cursor: pointer;\n                    border-radius: 2px;\n                    font-size: 13px;\n                }\n                button:hover {\n                    background: var(--vscode-button-hoverBackground);\n                }\n                button.secondary {\n                    background: var(--vscode-button-secondaryBackground);\n                    color: var(--vscode-button-secondaryForeground);\n                }\n                button.secondary:hover {\n                    background: var(--vscode-button-secondaryHoverBackground);\n                }\n                .target-file {\n                    font-size: 12px;\n                    color: var(--vscode-descriptionForeground);\n                    margin-top: 5px;\n                }\n            </style>\n        </head>\n        <body>\n            <h1>Build Hotspot Analysis</h1>\n\n            <div class=\"metrics\">\n                <h2>Build Metrics</h2>\n                <p><strong>Total Build Time:</strong> ".concat((totalDuration / 1000).toFixed(2), "s</p>\n                <p><strong>Files Compiled:</strong> ").concat(filesCompiled, "</p>\n            </div>\n\n            <div class=\"actions\">\n                <button onclick=\"applyAll('all')\">Apply All</button>\n                <button class=\"secondary\" onclick=\"applyAll('safe')\">Apply Safe Only</button>\n                <button class=\"secondary\" onclick=\"revertAll()\">Revert Changes</button>\n            </div>\n\n            <h2>Optimization Suggestions (").concat(suggestions.length, ")</h2>\n            ").concat(suggestions.map(function (s) {
        var priority = safeGetPriority(s.priority);
        var confidence = safeGetConfidence(s.confidence);
        var impact = s.estimatedImpact || { timeSavedMs: 0, percentage: 0, filesAffected: 0 };
        var timeSaved = safeGetNumber(impact.timeSavedMs, 0);
        var percentage = safeGetNumber(impact.percentage, 0);
        var filesAffected = safeGetNumber(impact.filesAffected, 0);
        return "\n                <div class=\"suggestion ".concat(PRIORITY_CLASSES[priority] || 'low', "\">\n                    <div class=\"suggestion-header\">\n                        <div>\n                            <span class=\"suggestion-title\">").concat(escapeHtml(s.title), "</span>\n                            <span class=\"badge priority\">").concat(PRIORITY_LABELS[priority] || 'Unknown', "</span>\n                            <span class=\"badge confidence\">").concat((confidence * 100).toFixed(0), "%</span>\n                            ").concat(s.autoApplicable ? '<span class="badge auto">Auto</span>' : '', "\n                        </div>\n                    </div>\n                    <p>").concat(escapeHtml(s.description), "</p>\n                    ").concat(s.targetUri ? "<div class=\"target-file\">Target: ".concat(escapeHtml(s.targetUri.replace('file://', '')), "</div>") : '', "\n                    <div class=\"impact\">\n                        <strong>Estimated Impact:</strong>\n                        ").concat(timeSaved > 0 ? (timeSaved / 1000).toFixed(2) + 's' : 'N/A', "\n                        (").concat(percentage.toFixed(1), "%)\n                        \u2022 ").concat(filesAffected, " files affected\n                    </div>\n                    <button onclick=\"applySuggestion('").concat(escapeHtml(s.id), "')\">Apply Suggestion</button>\n                </div>\n            ");
    }).join(''), "\n\n            <script>\n                const vscode = acquireVsCodeApi();\n\n                function applySuggestion(id) {\n                    if (!id || id.trim() === '') {\n                        console.error('Invalid suggestion ID');\n                        return;\n                    }\n                    vscode.postMessage({\n                        command: 'applySuggestion',\n                        suggestionId: id\n                    });\n                }\n\n                function applyAll(mode) {\n                    vscode.postMessage({\n                        command: 'applyAll',\n                        mode: mode\n                    });\n                }\n\n                function revertAll() {\n                    vscode.postMessage({\n                        command: 'revert'\n                    });\n                }\n            </script>\n        </body>\n        </html>\n    ");
    panel.webview.onDidReceiveMessage(function (message) { return __awaiter(_this, void 0, void 0, function () {
        return __generator(this, function (_a) {
            if (!message || typeof message.command !== 'string')
                return [2 /*return*/];
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
            return [2 /*return*/];
        });
    }); });
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
