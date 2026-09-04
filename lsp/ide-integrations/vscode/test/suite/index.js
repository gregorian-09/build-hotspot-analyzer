'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

function delay(milliseconds) {
    return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

async function run() {
    const workspaceRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    assert.ok(workspaceRoot, 'The host test must open its temporary workspace.');

    const targetPath = process.env.BHA_HOST_TEST_TARGET || path.join(workspaceRoot, 'src', 'dirty.cpp');
    const markerPath = process.env.BHA_HOST_TEST_APPLY_MARKER || path.join(workspaceRoot, 'apply-requested.marker');
    const originalContent = fs.readFileSync(targetPath, 'utf8');
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(targetPath));

    try {
        const editor = await vscode.window.showTextDocument(document, { preview: false });
        const edited = await editor.edit((builder) => {
            builder.insert(new vscode.Position(0, 0), '// unsaved host-test edit\n');
        });
        assert.strictEqual(edited, true, 'The fixture edit must be accepted by VS Code.');
        assert.strictEqual(document.isDirty, true, 'The affected document must be dirty.');

        await vscode.commands.executeCommand('buildHotspotAnalyzer.applySuggestion', 'ana-host-test');
        await delay(500);

        assert.strictEqual(document.isDirty, true, 'The apply command must not discard unsaved edits.');
        assert.strictEqual(
            fs.readFileSync(targetPath, 'utf8'),
            originalContent,
            'The dirty-document refusal must not change the file on disk.'
        );
        assert.strictEqual(
            fs.existsSync(markerPath),
            false,
            'The extension must refuse before sending bha.applySuggestion.'
        );
    } finally {
        await vscode.commands.executeCommand('workbench.action.revertAndCloseActiveEditor');
    }
}

module.exports = { run };
