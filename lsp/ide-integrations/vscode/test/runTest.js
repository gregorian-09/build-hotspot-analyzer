'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn, spawnSync } = require('child_process');

const extensionDevelopmentPath = path.resolve(__dirname, '..');
const extensionTestsPath = path.resolve(__dirname, 'suite', 'index.js');
const fakeServerPath = path.resolve(__dirname, 'fixtures', 'fake-lsp-server.js');

function findOnPath(names) {
    const pathEntries = (process.env.PATH || '').split(path.delimiter).filter(Boolean);
    for (const name of names) {
        for (const entry of pathEntries) {
            const candidate = path.join(entry, name);
            try {
                if (fs.statSync(candidate).isFile()) {
                    return candidate;
                }
            } catch {
                // Try the next PATH entry.
            }
        }
    }
    return undefined;
}

function findCodeExecutable() {
    const configured = process.env.VSCODE_EXECUTABLE?.trim();
    if (configured) {
        return configured;
    }

    const names = process.platform === 'win32'
        ? ['code.cmd', 'code.exe', 'code']
        : ['code', 'code-insiders'];
    const executable = findOnPath(names);
    if (executable) {
        return executable;
    }

    const lookup = process.platform === 'win32' ? 'where' : 'which';
    for (const name of names) {
        const result = spawnSync(lookup, [name], { encoding: 'utf8' });
        if (result.status === 0) {
            const firstLine = result.stdout.trim().split(/\r?\n/, 1)[0];
            if (firstLine) {
                return firstLine;
            }
        }
    }

    throw new Error(
        'VS Code was not found. Set VSCODE_EXECUTABLE to a VS Code CLI executable before running test:host.'
    );
}

function createServerCommand(workspaceRoot) {
    if (process.platform !== 'win32') {
        fs.chmodSync(fakeServerPath, 0o755);
        return fakeServerPath;
    }

    const commandPath = path.join(workspaceRoot, 'fake-lsp-server.cmd');
    const nodePath = process.execPath.replaceAll('"', '""');
    const scriptPath = fakeServerPath.replaceAll('"', '""');
    fs.writeFileSync(
        commandPath,
        `@echo off\r\n"${nodePath}" "${scriptPath}"\r\n`,
        'utf8'
    );
    return commandPath;
}

function createFixture() {
    const workspaceRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'bha-vscode-host-'));
    const sourceDirectory = path.join(workspaceRoot, 'src');
    const vscodeDirectory = path.join(workspaceRoot, '.vscode');
    const targetPath = path.join(sourceDirectory, 'dirty.cpp');
    const markerPath = path.join(workspaceRoot, 'apply-requested.marker');
    const originalContent = '#include <cstddef>\nint dirty_fixture() { return 0; }\n';

    fs.mkdirSync(sourceDirectory, { recursive: true });
    fs.mkdirSync(vscodeDirectory, { recursive: true });
    fs.writeFileSync(targetPath, originalContent, 'utf8');
    fs.writeFileSync(
        path.join(vscodeDirectory, 'settings.json'),
        `${JSON.stringify({
            'buildHotspotAnalyzer.serverPath': createServerCommand(workspaceRoot),
            'buildHotspotAnalyzer.autoAnalyze': false
        }, null, 2)}\n`,
        'utf8'
    );

    return { workspaceRoot, targetPath, markerPath, originalContent };
}

function runHost(executable, fixture) {
    const args = [
        `--extensionDevelopmentPath=${extensionDevelopmentPath}`,
        `--extensionTestsPath=${extensionTestsPath}`,
        fixture.workspaceRoot,
        '--disable-extensions',
        '--disable-gpu',
        '--user-data-dir',
        path.join(fixture.workspaceRoot, '.user-data')
    ];
    const environment = {
        ...process.env,
        BHA_HOST_TEST_TARGET: fixture.targetPath,
        BHA_HOST_TEST_APPLY_MARKER: fixture.markerPath
    };

    return new Promise((resolve, reject) => {
        const child = spawn(executable, args, {
            cwd: extensionDevelopmentPath,
            env: environment,
            stdio: 'inherit',
            shell: process.platform === 'win32' && /\.cmd$/i.test(executable)
        });
        child.once('error', reject);
        child.once('exit', (code, signal) => {
            if (code === 0) {
                resolve();
                return;
            }
            reject(new Error(`VS Code host test exited with ${signal || `code ${code}`}`));
        });
    });
}

async function main() {
    const executable = findCodeExecutable();
    const fixture = createFixture();
    try {
        await runHost(executable, fixture);
    } finally {
        fs.rmSync(fixture.workspaceRoot, { recursive: true, force: true });
    }
}

main().catch((error) => {
    console.error(error instanceof Error ? error.message : error);
    process.exitCode = 1;
});
