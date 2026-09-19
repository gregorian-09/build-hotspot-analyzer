'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const vscode = require('vscode');

async function run() {
    for (const key of ['BHA_HOST_REAL_PROJECT_ROOT', 'BHA_HOST_REAL_BUILD_DIR',
        'BHA_HOST_REAL_TRACE_DIR', 'BHA_HOST_REAL_BUILD_PROFILE',
        'BHA_HOST_REAL_SUGGESTION_ID', 'BHA_HOST_REAL_RESULT_PATH']) {
        assert.ok(process.env[key], `Missing ${key}`);
    }
    const workspaceRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    assert.ok(workspaceRoot, 'Real host test requires the upstream worktree.');
    assert.strictEqual(path.resolve(workspaceRoot), path.resolve(process.env.BHA_HOST_REAL_PROJECT_ROOT));

    const resultPath = process.env.BHA_HOST_REAL_RESULT_PATH;
    const suggestionId = process.env.BHA_HOST_REAL_SUGGESTION_ID;
    const analysis = await vscode.commands.executeCommand('buildHotspotAnalyzer.analyzeProject', {
        buildDir: process.env.BHA_HOST_REAL_BUILD_DIR,
        traceDir: process.env.BHA_HOST_REAL_TRACE_DIR,
        buildProfile: JSON.parse(process.env.BHA_HOST_REAL_BUILD_PROFILE)
    });
    assert.ok(Array.isArray(analysis?.suggestions), 'The real LSP must return analysis suggestions.');
    const suggestion = analysis.suggestions.find((item) => item.id === suggestionId);
    assert.ok(suggestion, `CLI-selected suggestion ${suggestionId} was absent from VS Code analysis.`);
    assert.strictEqual(suggestion.autoApplicable, true);
    assert.strictEqual(suggestion.applicationMode, 'direct-edits');

    const statusBefore = execFileSync('git', ['-C', workspaceRoot, 'status', '--porcelain'], {
        encoding: 'utf8'
    });
    const apply = await vscode.commands.executeCommand('buildHotspotAnalyzer.applySuggestion', suggestionId);
    assert.strictEqual(apply?.success, true, 'Real LSP apply failed.');
    assert.ok(apply.changedFiles?.length > 0, 'Apply reported no changed files.');
    assert.strictEqual(apply.buildValidation?.ran, true, 'Post-edit build was not run.');
    assert.strictEqual(apply.buildValidation?.success, true, 'Post-edit build failed.');
    const changed = execFileSync('git', ['-C', workspaceRoot, 'status', '--porcelain'], {
        encoding: 'utf8'
    });
    assert.notStrictEqual(changed, statusBefore, 'The isolated upstream worktree has no new on-disk edit.');
    const repositoryRoot = execFileSync('git', ['-C', workspaceRoot, 'rev-parse', '--show-toplevel'], {
        encoding: 'utf8'
    }).trim();
    const visibleChanges = apply.changedFiles.filter((file) => {
        const absolute = path.resolve(workspaceRoot, file);
        const relative = path.relative(repositoryRoot, absolute);
        if (relative.startsWith('..') || path.isAbsolute(relative)) {
            return false;
        }
        return execFileSync('git', ['-C', repositoryRoot, 'status', '--porcelain',
            '--untracked-files=all', '--', relative], { encoding: 'utf8' }).trim().length > 0;
    });
    assert.ok(visibleChanges.length > 0, 'No reported changed file is modified in the upstream worktree.');
    fs.mkdirSync(path.dirname(resultPath), { recursive: true });
    fs.writeFileSync(resultPath, JSON.stringify({
        suggestionId, analysisId: analysis.analysisId, suggestionCount: analysis.suggestions.length,
        apply, gitStatus: changed
    }, null, 2) + '\n');
}

module.exports = { run };
