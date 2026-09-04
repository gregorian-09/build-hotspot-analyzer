#!/usr/bin/env node
'use strict';

const fs = require('fs');

let input = Buffer.alloc(0);

function send(id, result) {
    const body = Buffer.from(JSON.stringify({ jsonrpc: '2.0', id, result }), 'utf8');
    process.stdout.write(`Content-Length: ${body.length}\r\n\r\n`);
    process.stdout.write(body);
}

function sendError(id, code, message) {
    const body = Buffer.from(JSON.stringify({
        jsonrpc: '2.0',
        id,
        error: { code, message }
    }), 'utf8');
    process.stdout.write(`Content-Length: ${body.length}\r\n\r\n`);
    process.stdout.write(body);
}

function suggestionDetails() {
    return {
        id: 'ana-host-test',
        type: 5,
        title: 'Host test',
        description: 'Host test dirty-document guard',
        priority: 0,
        confidence: 1,
        autoApplicable: true,
        applicationMode: 'direct-edits',
        filesToModify: ['src/dirty.cpp'],
        textEdits: []
    };
}

function showMetrics() {
    return {
        analysisId: 'host-test-analysis',
        suggestions: [suggestionDetails()],
        baselineMetrics: { totalDurationMs: 1, filesCompiled: 1 },
        filesAnalyzed: 1
    };
}

function executeCommand(params) {
    const command = params?.command;
    if (command === 'bha.showMetrics') {
        return showMetrics();
    }
    if (command === 'bha.getSuggestionDetails') {
        return suggestionDetails();
    }
    if (command === 'bha.applySuggestion') {
        const markerPath = process.env.BHA_HOST_TEST_APPLY_MARKER;
        if (markerPath) {
            fs.writeFileSync(markerPath, 'apply request received\n', 'utf8');
        }
        return {
            success: true,
            changedFiles: [],
            errors: [],
            buildValidation: { requested: false, ran: false, success: true },
            rollback: { attempted: false, success: true }
        };
    }
    if (command === 'bha.getJobStatus' || command === 'bha.cancelJob') {
        return {};
    }
    return {};
}

function handle(message) {
    if (message.method === 'initialize') {
        send(message.id, {
            capabilities: {
                textDocumentSync: 1,
                executeCommandProvider: {
                    commands: ['bha.showMetrics', 'bha.getSuggestionDetails', 'bha.applySuggestion']
                }
            },
            serverInfo: { name: 'bha-host-test-server', version: '1' }
        });
        return;
    }
    if (message.method === 'shutdown') {
        send(message.id, null);
        return;
    }
    if (message.method === 'workspace/executeCommand') {
        send(message.id, executeCommand(message.params));
        return;
    }
    if (message.id !== undefined) {
        send(message.id, {});
    }
}

function drain() {
    while (true) {
        const separator = input.indexOf(Buffer.from('\r\n\r\n'));
        if (separator < 0) {
            return;
        }
        const header = input.subarray(0, separator).toString('ascii');
        const match = /(?:^|\r\n)Content-Length:\s*(\d+)\s*(?:\r\n|$)/i.exec(header);
        if (!match) {
            process.stderr.write('Missing Content-Length header\n');
            process.exitCode = 1;
            return;
        }
        const bodyStart = separator + 4;
        const bodyLength = Number(match[1]);
        if (input.length < bodyStart + bodyLength) {
            return;
        }
        const body = input.subarray(bodyStart, bodyStart + bodyLength).toString('utf8');
        input = input.subarray(bodyStart + bodyLength);
        try {
            handle(JSON.parse(body));
        } catch (error) {
            process.stderr.write(`Invalid JSON-RPC message: ${error}\n`);
        }
    }
}

process.stdin.on('data', (chunk) => {
    input = Buffer.concat([input, chunk]);
    drain();
});
