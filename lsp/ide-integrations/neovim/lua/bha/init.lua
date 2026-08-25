-- Build Hotspot Analyzer LSP client for Neovim
-- Version: 0.1.0

local M = {}

local lsp = vim.lsp
local util = require('lspconfig.util')

-- ============================================================================
-- Configuration Constants
-- ============================================================================

M.defaults = {
    -- Server configuration
    cmd = { 'bha-lsp' },
    filetypes = { 'c', 'cpp', 'objc', 'objcpp' },
    root_dir = util.root_pattern('CMakeLists.txt', 'Makefile', 'meson.build', 'compile_commands.json', '.git'),
    single_file_support = false,
    settings = {},

    -- UI configuration
    auto_analyze_delay_ms = 2000,
    max_suggestions_display = 100,
    floating_window_width_ratio = 0.8,
    floating_window_height_ratio = 0.8,

    -- Behavior configuration
    min_confidence_threshold = 0.0,
    default_priority_filter = 2,  -- 0=High, 1=Medium, 2=Low
    confirm_before_apply = true,
    bulk_apply_atomic = false,
    history_limit = 10,
    activity_log_limit = 200,
    persist_history = true,
}

M.config = vim.tbl_deep_extend('force', {}, M.defaults)

local PRIORITY_LABELS = { 'HIGH', 'MEDIUM', 'LOW' }

-- ============================================================================
-- State Variables
-- ============================================================================

local state = {
    suggestions_cache = nil,
    analysis_result = nil,
    last_backup_id = nil,
    operation_counter = 0,
    history = {},
    activity_log = {},
    active_jobs = {},
    progress = {},
    operation_status = nil,
}

local get_client

-- ============================================================================
-- UUID Generation
-- ============================================================================

local function generate_uuid()
    -- RFC 4122 version 4 UUID
    local template = 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'
    return string.gsub(template, '[xy]', function(c)
        local v = (c == 'x') and math.random(0, 0xf) or math.random(8, 0xb)
        return string.format('%x', v)
    end)
end

local function generate_operation_id(prefix)
    state.operation_counter = state.operation_counter + 1
    return string.format('%s-%d-%s', prefix, state.operation_counter, generate_uuid())
end

-- ============================================================================
-- Bounds Checking Utilities
-- ============================================================================

local function safe_number(value, default)
    if type(value) == 'number' and value == value then  -- NaN check
        return value
    end
    return default or 0
end

local function safe_string(value, default)
    if type(value) == 'string' then
        return value
    end
    return default or ''
end

local function safe_priority(priority)
    if type(priority) == 'number' and priority >= 0 and priority <= 2 then
        return math.floor(priority)
    end
    return 2  -- Default to Low
end

local function safe_confidence(confidence)
    if type(confidence) == 'number' and confidence >= 0 and confidence <= 1 then
        return confidence
    end
    return 0
end

local function is_valid_suggestion(sug)
    if type(sug) ~= 'table' then
        return false
    end
    return type(sug.id) == 'string' and #sug.id > 0
        and type(sug.title) == 'string'
        and type(sug.priority) == 'number'
        and type(sug.confidence) == 'number'
end

local function filter_valid_suggestions(suggestions)
    if not suggestions then
        return {}
    end
    local valid = {}
    for _, sug in ipairs(suggestions) do
        if is_valid_suggestion(sug) then
            table.insert(valid, sug)
        end
    end
    return valid
end

local function safe_table_length(t)
    if type(t) == 'table' then
        return #t
    end
    return 0
end

local function workspace_root()
    local client = get_client()
    if client and client.config and type(client.config.root_dir) == 'string' then
        return vim.fn.fnamemodify(client.config.root_dir, ':p')
    end
    return vim.fn.fnamemodify(vim.fn.getcwd(), ':p')
end

local function history_file()
    return vim.fn.stdpath('state') .. '/bha/history.json'
end

local function load_history()
    if not M.config.persist_history then
        return
    end
    local path = history_file()
    if vim.fn.filereadable(path) ~= 1 then
        return
    end
    local ok, decoded = pcall(vim.fn.json_decode, table.concat(vim.fn.readfile(path), '\n'))
    if ok and type(decoded) == 'table' then
        state.history = decoded
    end
end

local function save_history()
    if not M.config.persist_history then
        return
    end
    local dir = vim.fn.fnamemodify(history_file(), ':h')
    vim.fn.mkdir(dir, 'p')
    local ok, encoded = pcall(vim.fn.json_encode, state.history)
    if ok and type(encoded) == 'string' then
        vim.fn.writefile({ encoded }, history_file())
    end
end

local function format_evidence(value)
    if value == 'observed' or value == 'Observed' then
        return 'Observed'
    elseif value == 'derived' or value == 'Derived' then
        return 'Derived'
    end
    return 'Unavailable'
end

local function append_activity(label, detail, details)
    local entry = {
        timestamp = os.date('!%Y-%m-%dT%H:%M:%SZ'),
        label = safe_string(label, 'BHA'),
        detail = safe_string(detail, ''),
        details = details or {},
    }
    table.insert(state.activity_log, 1, entry)
    local limit = M.config.activity_log_limit or M.defaults.activity_log_limit
    while #state.activity_log > limit do
        table.remove(state.activity_log)
    end
    state.operation_status = entry
end

local function build_timing(result)
    local metrics = result and result.baselineMetrics or {}
    local timing = result and result.buildTiming or {}
    local total = safe_number(timing.totalBuildTimeMs, safe_number(metrics.totalDurationMs, 0))
    local source = safe_string(timing.source, total == safe_number(metrics.totalDurationMs, 0)
        and 'trace-aggregate' or 'unknown')
    return total, source
end

local function remember_analysis_run(result)
    if type(result) ~= 'table' or (not result.baselineMetrics and not result.buildTiming and not result.filesAnalyzed) then
        return
    end
    local root = workspace_root()
    local total, source = build_timing(result)
    local entry = {
        analysisId = result.analysisId,
        recordedAt = os.date('!%Y-%m-%dT%H:%M:%SZ'),
        suggestionCount = #filter_valid_suggestions(result.suggestions or {}),
        totalBuildTimeMs = total,
        buildTimeSource = source,
        filesAnalyzed = safe_number(result.filesAnalyzed,
            safe_number((result.baselineMetrics or {}).filesCompiled, 0)),
    }
    local history = state.history[root] or {}
    local next = { entry }
    for _, previous in ipairs(history) do
        if not entry.analysisId or previous.analysisId ~= entry.analysisId then
            table.insert(next, previous)
        end
    end
    local limit = M.config.history_limit or M.defaults.history_limit
    while #next > limit do
        table.remove(next)
    end
    state.history[root] = next
    save_history()
end

local function format_trust_loop(trust_loop)
    if type(trust_loop) ~= 'table' or trust_loop.available ~= true then
        return { 'Measured rebuild result: unavailable' }
    end
    local actual = safe_number(trust_loop.actualSavingsMs, 0)
    local baseline = safe_number(trust_loop.baselineBuildMs, 0)
    local rebuild = safe_number(trust_loop.rebuildBuildMs, 0)
    local predicted = safe_number(trust_loop.predictedSavingsMs, 0)
    local status = safe_string(trust_loop.status, 'unknown')
    return {
        string.format('Measured rebuild delta: %.2fs (%s)', actual / 1000, status),
        string.format('Measured baseline/rebuild: %.2fs -> %.2fs', baseline / 1000, rebuild / 1000),
        string.format('Predicted savings: %.2fs; evidence is measured after validation', predicted / 1000),
    }
end

local function result_details(result, all_result)
    local lines = {}
    local validation = result.buildValidation or (all_result and all_result.buildValidation)
    local rollback = result.rollback or (all_result and all_result.rollback)
    if validation then
        table.insert(lines, string.format('Validation: %s', validation.ran
            and (validation.success and 'passed' or 'failed') or 'not run'))
    end
    if rollback then
        table.insert(lines, string.format('Rollback: %s', rollback.attempted
            and (rollback.success and 'succeeded' or 'failed') or 'not required'))
    end
    for _, line in ipairs(format_trust_loop(result.trustLoop or (all_result and all_result.trustLoop))) do
        table.insert(lines, line)
    end
    local errors = result.errors or (all_result and all_result.errors) or {}
    for index, error in ipairs(errors) do
        if index > 8 then
            break
        end
        table.insert(lines, 'Error: ' .. safe_string(error.message, 'Unknown apply error'))
    end
    return lines
end

local function active_job_id()
    for id, _ in pairs(state.active_jobs) do
        return id
    end
    return nil
end

-- ============================================================================
-- LSP Command Execution
-- ============================================================================

get_client = function()
    local clients
    if vim.lsp.get_clients then
        clients = vim.lsp.get_clients({ name = 'bha-lsp' })
    else
        clients = vim.lsp.get_active_clients({ name = 'bha-lsp' })
    end
    return clients[1]
end

local function execute_command(command, args, callback)
    local params = {
        command = command,
        arguments = args or {},
    }
    local client = get_client()
    if not client then
        vim.notify('BHA LSP server not running', vim.log.levels.ERROR)
        if callback then
            callback('BHA LSP server not running', nil)
        end
        return
    end
    client.request('workspace/executeCommand', params, function(err, result)
        if err then
            vim.notify('Command failed: ' .. vim.inspect(err), vim.log.levels.ERROR)
            if callback then
                callback(err, nil)
            end
        else
            if callback then
                callback(nil, result)
            end
        end
    end)
end

local function handle_progress(_, params)
    if type(params) ~= 'table' then
        return
    end
    local token = safe_string(params.token, vim.inspect(params.token or 'bha'))
    local value = params.value or {}
    local kind = safe_string(value.kind, 'report')
    local message = safe_string(value.message, '')
    if kind == 'begin' then
        state.progress[token] = {
            title = safe_string(value.title, 'BHA operation'),
            message = message,
            percentage = value.percentage,
        }
        append_activity('Progress started', state.progress[token].title, { message })
    elseif kind == 'report' then
        local progress = state.progress[token] or { title = 'BHA operation' }
        progress.message = message
        progress.percentage = value.percentage
        state.progress[token] = progress
    elseif kind == 'end' then
        local progress = state.progress[token] or { title = 'BHA operation' }
        append_activity('Progress ended', safe_string(value.message, progress.title), {
            progress.message or '',
        })
        state.progress[token] = nil
    end
end

local function handle_job_log(_, params)
    if type(params) ~= 'table' then
        return
    end
    local category = safe_string(params.category, 'job')
    local message = safe_string(params.message, '')
    if message == '' then
        return
    end
    append_activity('Job log [' .. category .. ']', message, {
        'Job: ' .. safe_string(params.jobId, 'unknown'),
    })
    if message:find('Re%-ranking') or message:find('Fault isolation') then
        vim.notify('BHA: ' .. message, vim.log.levels.INFO)
    end
end

local function handle_job_started(_, params)
    if type(params) ~= 'table' then
        return
    end
    local job_id = safe_string(params.jobId, '')
    append_activity('Background job started', safe_string(params.command, 'unknown'), {
        'Job: ' .. (job_id ~= '' and job_id or 'unknown'),
    })
end

local function handle_job_completed(_, params)
    if type(params) ~= 'table' then
        return
    end
    local job_id = safe_string(params.jobId, '')
    state.active_jobs[job_id] = nil
    append_activity('Background job completed', safe_string(params.command, 'unknown'), {
        'Job: ' .. (job_id ~= '' and job_id or 'unknown'),
        'Status: ' .. safe_string(params.status, 'unknown'),
    })
end

local function install_client_handlers(client)
    local handlers = {
        ['$/progress'] = handle_progress,
        ['bha/jobLog'] = handle_job_log,
        ['bha/jobStarted'] = handle_job_started,
        ['bha/jobCompleted'] = handle_job_completed,
        ['bha/jobCancellationRequested'] = function(_, params)
            if type(params) == 'table' then
                append_activity('Cancellation requested', safe_string(params.jobId, 'unknown'))
            end
        end,
    }
    client.handlers = client.handlers or {}
    for method, handler in pairs(handlers) do
        local previous = client.handlers[method]
        client.handlers[method] = function(err, result, ctx, config)
            handler(err, result, ctx, config)
            if previous then
                previous(err, result, ctx, config)
            end
        end
    end
end

local function run_async_command(command, args, title, callback)
    local payload = vim.deepcopy(args or {})
    payload.async = true
    append_activity(title, 'Queued', {})
    execute_command(command, { payload }, function(err, accepted)
        if err then
            callback(err, nil)
            return
        end
        if type(accepted) ~= 'table' or accepted.accepted ~= true or type(accepted.jobId) ~= 'string' then
            callback('BHA server did not accept asynchronous command', nil)
            return
        end

        local job_id = accepted.jobId
        state.active_jobs[job_id] = { command = command, title = title }
        local function poll()
            if not state.active_jobs[job_id] then
                return
            end
            execute_command('bha.getJobStatus', { { jobId = job_id } }, function(status_err, status)
                if status_err then
                    state.active_jobs[job_id] = nil
                    callback(status_err, nil)
                    return
                end
                local current = type(status) == 'table' and safe_string(status.status, 'unknown') or 'unknown'
                if current == 'completed' then
                    state.active_jobs[job_id] = nil
                    callback(nil, status.result, status)
                elseif current == 'cancelled' then
                    state.active_jobs[job_id] = nil
                    callback('BHA operation cancelled', nil, status)
                elseif current == 'failed' then
                    state.active_jobs[job_id] = nil
                    callback(safe_string(status.error, 'BHA operation failed'), nil, status)
                else
                    vim.defer_fn(poll, 250)
                end
            end)
        end
        poll()
    end)
end

function M.cancel_job(job_id)
    local id = job_id or active_job_id()
    if not id then
        vim.notify('No active BHA job', vim.log.levels.INFO)
        return
    end
    execute_command('bha.cancelJob', { { jobId = id } }, function(err)
        if err then
            vim.notify('Failed to cancel BHA job: ' .. vim.inspect(err), vim.log.levels.ERROR)
            return
        end
        append_activity('Cancellation requested', id)
        vim.notify('BHA cancellation requested for ' .. id, vim.log.levels.INFO)
    end)
end

-- ============================================================================
-- Core Commands
-- ============================================================================

function M.analyze_project()
    local operation_id = generate_operation_id('analyze')
    local root_dir = workspace_root()
    local build_dir = vim.fn.input('Build directory (leave empty for auto-detect): ')

    local args = {
        projectRoot = root_dir,
        buildDir = build_dir ~= '' and build_dir or vim.NIL,
        rebuild = false,
        operationId = operation_id,
    }

    vim.notify(string.format('Analyzing project (operation: %s)...', operation_id), vim.log.levels.INFO)
    run_async_command('bha.analyze', args, 'Analyze project', function(err, result)
        if err then
            vim.notify('Analysis failed: ' .. vim.inspect(err), vim.log.levels.ERROR)
            append_activity('Analysis failed', safe_string(err, 'Unknown error'))
            return
        end

        if result then
            state.analysis_result = result
            local valid_suggestions = filter_valid_suggestions(result.suggestions or {})
            state.suggestions_cache = valid_suggestions
            result.suggestions = valid_suggestions
            remember_analysis_run(result)
            local num_suggestions = #valid_suggestions
            local total, source = build_timing(result)
            append_activity('Analysis completed', string.format('%d suggestion(s); %.2fs build time (%s)',
                num_suggestions, total / 1000, source), {})

            vim.notify(string.format('Analysis complete: %d suggestions found', num_suggestions), vim.log.levels.INFO)

            if num_suggestions > 0 then
                M.show_suggestions(result)
            end
        end
    end)
end

function M.record_build_traces()
    local build_dir = vim.fn.input('Build directory (leave empty for auto-detect): ')
    local args = {
        projectRoot = workspace_root(),
        buildDir = build_dir ~= '' and build_dir or vim.NIL,
        operationId = generate_operation_id('record-build-traces'),
    }
    run_async_command('bha.recordBuildTraces', args, 'Record build traces', function(err, result)
        if err then
            append_activity('Trace recording failed', safe_string(err, 'Unknown error'))
            vim.notify('Trace recording failed: ' .. vim.inspect(err), vim.log.levels.ERROR)
            return
        end
        local count = safe_number(result and result.traceFileCount, safe_table_length(result and result.traceFiles))
        append_activity('Trace recording completed', string.format('%d trace file(s) recorded', count), {})
        vim.notify(string.format('Recorded %d trace file(s)', count), vim.log.levels.INFO)
    end)
end

function M.show_suggestions(analysis_result)
    local client = get_client()
    if not client then
        vim.notify('BHA LSP server not running', vim.log.levels.ERROR)
        return
    end

    local result = analysis_result
    if not result then
        client.request('workspace/executeCommand', {
            command = 'bha.showMetrics',
            arguments = {},
        }, function(err, res)
            if err then
                vim.notify('Failed to get suggestions: ' .. vim.inspect(err), vim.log.levels.ERROR)
                return
            end
            if res then
                local suggestions = filter_valid_suggestions(res.suggestions or {})
                state.suggestions_cache = suggestions
                if state.analysis_result then
                    state.analysis_result.suggestions = suggestions
                else
                    state.analysis_result = res
                    state.analysis_result.suggestions = suggestions
                end
            end
            M._display_suggestions(state.analysis_result or res)
        end)
    else
        M._display_suggestions(result)
    end
end

local function open_text_float(lines, title, filetype)
    local buf = vim.api.nvim_create_buf(false, true)
    vim.api.nvim_buf_set_lines(buf, 0, -1, false, lines)
    vim.api.nvim_buf_set_option(buf, 'modifiable', false)
    vim.api.nvim_buf_set_option(buf, 'bufhidden', 'wipe')
    vim.api.nvim_buf_set_option(buf, 'filetype', filetype or 'markdown')

    local width_ratio = M.config.floating_window_width_ratio or M.defaults.floating_window_width_ratio
    local height_ratio = M.config.floating_window_height_ratio or M.defaults.floating_window_height_ratio
    local width = math.max(40, math.floor(vim.o.columns * width_ratio))
    local height = math.max(8, math.floor(vim.o.lines * height_ratio))
    width = math.min(width, vim.o.columns - 4)
    height = math.min(height, vim.o.lines - 4)
    local win = vim.api.nvim_open_win(buf, true, {
        relative = 'editor',
        width = width,
        height = height,
        row = math.floor((vim.o.lines - height) / 2),
        col = math.floor((vim.o.columns - width) / 2),
        style = 'minimal',
        border = 'rounded',
        title = ' ' .. safe_string(title, 'BHA') .. ' ',
        title_pos = 'center',
    })
    local function close_window()
        if vim.api.nvim_win_is_valid(win) then
            vim.api.nvim_win_close(win, true)
        end
    end
    for _, key in ipairs({ 'q', '<Esc>' }) do
        vim.api.nvim_buf_set_keymap(buf, 'n', key, '', {
            silent = true,
            noremap = true,
            callback = close_window,
        })
    end
    return buf, win, close_window
end

function M._display_suggestions(result)
    if not result then
        vim.notify('No analysis result available. Run :BHAAnalyze first.', vim.log.levels.INFO)
        return
    end

    local suggestions = filter_valid_suggestions(result.suggestions or {})
    state.suggestions_cache = suggestions

    if #suggestions == 0 then
        vim.notify('No suggestions available. Run :BHAAnalyze first.', vim.log.levels.INFO)
        return
    end

    local max_display = M.config.max_suggestions_display or M.defaults.max_suggestions_display
    local lines = {}

    table.insert(lines, '# Build Hotspot Analysis')
    table.insert(lines, '')

    -- Metrics section
    if result.baselineMetrics then
        local metrics = result.baselineMetrics
        table.insert(lines, '## Build Metrics')
        table.insert(lines, string.format('Total Build Time: %.2fs',
            safe_number(metrics.totalDurationMs, 0) / 1000))
        table.insert(lines, string.format('Files Compiled: %d',
            safe_number(metrics.filesCompiled, 0)))
        table.insert(lines, '')
    end

    -- Actions
    table.insert(lines, '## Actions')
    table.insert(lines, 'Press: A = Apply All | S = Apply Safe | D = Details | P = Preview | R = Revert | q = Close')
    table.insert(lines, '')

    -- Suggestions
    local display_count = math.min(#suggestions, max_display)
    table.insert(lines, string.format('## Optimization Suggestions (%d of %d)', display_count, #suggestions))
    table.insert(lines, '')

    for i = 1, display_count do
        local sug = suggestions[i]
        local priority = safe_priority(sug.priority)
        local confidence = safe_confidence(sug.confidence)
        local priority_label = PRIORITY_LABELS[priority + 1] or 'UNKNOWN'
        local auto_label = sug.autoApplicable and ' [Auto]' or ''

        table.insert(lines, string.format('[%d] %s%s', i, safe_string(sug.title, 'Untitled'), auto_label))
        table.insert(lines, string.format('    Priority: %s | Confidence: %d%%', priority_label, math.floor(confidence * 100)))
        table.insert(lines, string.format('    %s', safe_string(sug.description, '')))

        local evidence = format_evidence(sug.estimatedSavingsEvidence)
        table.insert(lines, '    Savings Evidence: ' .. evidence)
        if sug.estimatedImpact then
            local impact = sug.estimatedImpact
            local time_saved = safe_number(impact.timeSavedMs, 0) / 1000
            if evidence ~= 'Unavailable' then
                table.insert(lines, string.format(
                    '    Estimated Savings: %.2fs | Build Reduction: %.1f%% | %d files affected',
                    time_saved,
                    safe_number(impact.percentage, 0),
                    safe_number(impact.filesAffected, 0)
                ))
            else
                table.insert(lines, string.format('    Estimated Savings: Unavailable | %d files affected',
                    safe_number(impact.filesAffected, 0)))
            end
        end

        table.insert(lines, '')
    end

    if #suggestions > max_display then
        table.insert(lines, string.format('... and %d more suggestions', #suggestions - max_display))
        table.insert(lines, '')
    end

    if state.operation_status then
        table.insert(lines, '')
        table.insert(lines, '## Latest Operation')
        table.insert(lines, '    ' .. safe_string(state.operation_status.label, 'BHA'))
        table.insert(lines, '    ' .. safe_string(state.operation_status.detail, ''))
    end
    table.insert(lines, '')
    table.insert(lines, 'Press <CR> on a suggestion number to apply it')
    table.insert(lines, 'Press H = History | L = Activity Log | C = Cancel Active Job | q = Close')

    -- Create floating window
    local buf = vim.api.nvim_create_buf(false, true)
    vim.api.nvim_buf_set_lines(buf, 0, -1, false, lines)
    vim.api.nvim_buf_set_option(buf, 'modifiable', false)
    vim.api.nvim_buf_set_option(buf, 'filetype', 'markdown')

    local width_ratio = M.config.floating_window_width_ratio or M.defaults.floating_window_width_ratio
    local height_ratio = M.config.floating_window_height_ratio or M.defaults.floating_window_height_ratio
    local width = math.floor(vim.o.columns * width_ratio)
    local height = math.floor(vim.o.lines * height_ratio)
    local row = math.floor((vim.o.lines - height) / 2)
    local col = math.floor((vim.o.columns - width) / 2)

    local win = vim.api.nvim_open_win(buf, true, {
        relative = 'editor',
        width = width,
        height = height,
        row = row,
        col = col,
        style = 'minimal',
        border = 'rounded',
        title = ' Build Hotspot Analysis ',
        title_pos = 'center',
    })

    -- Key mappings
    local function close_window()
        if vim.api.nvim_win_is_valid(win) then
            vim.api.nvim_win_close(win, true)
        end
    end

    vim.api.nvim_buf_set_keymap(buf, 'n', 'q', '', {
        silent = true,
        noremap = true,
        callback = close_window,
    })

    vim.api.nvim_buf_set_keymap(buf, 'n', '<Esc>', '', {
        silent = true,
        noremap = true,
        callback = close_window,
    })

    vim.api.nvim_buf_set_keymap(buf, 'n', '<CR>', '', {
        silent = true,
        noremap = true,
        callback = function()
            local line = vim.api.nvim_win_get_cursor(win)[1]
            local content = vim.api.nvim_buf_get_lines(buf, line - 1, line, false)[1]
            if content then
                local idx = content:match('^%[(%d+)%]')
                if idx then
                    local suggestion = suggestions[tonumber(idx)]
                    if suggestion and suggestion.id then
                        close_window()
                        M.apply_suggestion(suggestion.id)
                    end
                end
            end
        end
    })

    local function suggestion_at_cursor()
        local line = vim.api.nvim_win_get_cursor(win)[1]
        local content = vim.api.nvim_buf_get_lines(buf, line - 1, line, false)[1] or ''
        local idx = content:match('^%[(%d+)%]')
        return idx and suggestions[tonumber(idx)] or nil
    end

    vim.api.nvim_buf_set_keymap(buf, 'n', 'D', '', {
        silent = true,
        noremap = true,
        callback = function()
            local suggestion = suggestion_at_cursor()
            if suggestion then
                close_window()
                M.show_suggestion_details(suggestion.id)
            end
        end,
    })

    vim.api.nvim_buf_set_keymap(buf, 'n', 'P', '', {
        silent = true,
        noremap = true,
        callback = function()
            local suggestion = suggestion_at_cursor()
            if suggestion then
                close_window()
                M.preview_suggestion(suggestion.id)
            end
        end,
    })

    vim.api.nvim_buf_set_keymap(buf, 'n', 'A', '', {
        silent = true,
        noremap = true,
        callback = function()
            close_window()
            M.apply_all_suggestions(false)
        end
    })

    vim.api.nvim_buf_set_keymap(buf, 'n', 'S', '', {
        silent = true,
        noremap = true,
        callback = function()
            close_window()
            M.apply_all_suggestions(true)
        end
    })

    vim.api.nvim_buf_set_keymap(buf, 'n', 'R', '', {
        silent = true,
        noremap = true,
        callback = function()
            close_window()
            M.revert_changes()
        end
    })

    vim.api.nvim_buf_set_keymap(buf, 'n', 'H', '', {
        silent = true,
        noremap = true,
        callback = function()
            close_window()
            M.show_history()
        end,
    })

    vim.api.nvim_buf_set_keymap(buf, 'n', 'L', '', {
        silent = true,
        noremap = true,
        callback = function()
            close_window()
            M.show_activity_log()
        end,
    })

    vim.api.nvim_buf_set_keymap(buf, 'n', 'C', '', {
        silent = true,
        noremap = true,
        callback = function()
            M.cancel_job()
        end,
    })
end

function M.apply_suggestion(suggestion_id)
    local id = suggestion_id
    if not id then
        local suggestions = state.suggestions_cache or {}
        if #suggestions == 0 then
            vim.notify('No suggestions available', vim.log.levels.ERROR)
            return
        end

        local items = {}
        for i, sug in ipairs(suggestions) do
            if is_valid_suggestion(sug) then
                local priority = safe_priority(sug.priority)
                local priority_label = PRIORITY_LABELS[priority + 1] or 'UNKNOWN'
                local auto_label = sug.autoApplicable and ' [Auto]' or ''
                table.insert(items, string.format('[%s]%s %s - %s',
                    priority_label,
                    auto_label,
                    safe_string(sug.title, 'Untitled'),
                    safe_string(sug.description, '')))
            end
        end

        vim.ui.select(items, {
            prompt = 'Select suggestion to apply:',
        }, function(choice, idx)
            if not idx then return end
            local selected = suggestions[idx]
            if selected and selected.id then
                M._do_apply_suggestion(selected.id)
            end
        end)
    else
        M._do_apply_suggestion(id)
    end
end

function M._do_apply_suggestion(suggestion_id)
    -- Validate suggestion ID
    if type(suggestion_id) ~= 'string' or #suggestion_id == 0 then
        vim.notify('Invalid suggestion ID', vim.log.levels.ERROR)
        return
    end

    if M.config.confirm_before_apply then
        local confirm = vim.fn.confirm('Apply this suggestion? This will modify your code.', '&Yes\n&No', 2)
        if confirm ~= 1 then
            return
        end
    end

    local operation_id = generate_operation_id('apply')
    vim.notify(string.format('Applying suggestion (operation: %s)...', operation_id), vim.log.levels.INFO)
    run_async_command('bha.applySuggestion', {
        suggestionId = suggestion_id,
        operationId = operation_id,
    }, 'Apply suggestion', function(err, result)
        if err then
            vim.notify('Failed to apply suggestion: ' .. vim.inspect(err), vim.log.levels.ERROR)
            append_activity('Suggestion apply failed', safe_string(err, 'Unknown error'))
            return
        end

        if not result then
            vim.notify('Apply returned no result', vim.log.levels.ERROR)
            return
        end

        if result.success then
            if result.backupId then
                state.last_backup_id = result.backupId
            end
            local num_files = safe_table_length(result.changedFiles)
            local details = result_details(result)
            append_activity('Suggestion applied', string.format('Modified %d file(s)', num_files), details)
            vim.notify(string.format('Suggestion applied successfully. Modified %d files.', num_files), vim.log.levels.INFO)
            vim.cmd('checktime')
        else
            local errors = result.errors or {}
            local error_msg = #errors > 0 and safe_string(errors[1].message, 'Unknown error') or 'Unknown error'
            append_activity('Suggestion rolled back or failed', error_msg, result_details(result))
            vim.notify('Failed to apply suggestion: ' .. error_msg, vim.log.levels.ERROR)
        end
    end)
end

function M.apply_all_suggestions(safe_only)
    local suggestions = state.suggestions_cache or {}
    if #suggestions == 0 then
        vim.notify('No suggestions available. Run :BHAAnalyze first.', vim.log.levels.ERROR)
        return
    end

    -- Count affected suggestions
    local affected_count = 0
    for _, sug in ipairs(suggestions) do
        if is_valid_suggestion(sug) then
            if safe_only then
                if sug.autoApplicable then
                    affected_count = affected_count + 1
                end
            else
                affected_count = affected_count + 1
            end
        end
    end

    if affected_count == 0 then
        vim.notify('No suggestions match the criteria', vim.log.levels.INFO)
        return
    end

    local prompt = string.format('Apply %d %ssuggestions? This will modify your code. A backup will be created.',
        affected_count,
        safe_only and 'auto-applicable ' or '')

    local confirm = vim.fn.confirm(prompt, '&Yes\n&No', 2)
    if confirm ~= 1 then
        return
    end

    local operation_id = generate_operation_id('apply-all')
    local min_priority = M.config.default_priority_filter or M.defaults.default_priority_filter

    vim.notify(string.format('Applying %d suggestions (%s validation, operation: %s)...', affected_count,
        M.config.bulk_apply_atomic and 'atomic' or 'fault-isolating', operation_id), vim.log.levels.INFO)

    run_async_command('bha.applyAllSuggestions', {
        minPriority = min_priority,
        safeOnly = safe_only or false,
        atomic = M.config.bulk_apply_atomic,
        operationId = operation_id,
    }, 'Apply suggestions', function(err, result)
        if err then
            vim.notify('Failed to apply suggestions: ' .. vim.inspect(err), vim.log.levels.ERROR)
            append_activity('Bulk apply failed', safe_string(err, 'Unknown error'))
            return
        end

        if not result then
            vim.notify('Apply all returned no result', vim.log.levels.ERROR)
            return
        end

        if result.backupId then
            state.last_backup_id = result.backupId
        end

        local applied_count = safe_number(result.appliedCount, 0)
        local skipped_count = safe_number(result.skippedCount, 0)
        local failed_count = safe_number(result.failedCount, 0)

        if result.success then
            local msg = string.format('Applied %d suggestions successfully.', applied_count)
            if skipped_count > 0 then
                msg = msg .. string.format(' Skipped: %d.', skipped_count)
            end
            local details = result_details(result, result)
            if result.appliedSuggestionIds and #result.appliedSuggestionIds > 0 then
                table.insert(details, 'Applied IDs: ' .. table.concat(result.appliedSuggestionIds, ', '))
            end
            append_activity('Bulk apply completed', msg, details)
            vim.notify(msg, vim.log.levels.INFO)
            vim.cmd('checktime')
        else
            local errors = result.errors or {}
            local error_summary = ''
            if #errors > 0 then
                error_summary = ' First error: ' .. safe_string(errors[1].message, 'Unknown')
            end
            append_activity('Bulk apply rolled back or failed', error_summary, result_details(result, result))
            vim.notify(string.format('Apply all failed. %d errors.%s Changes rolled back.', failed_count, error_summary), vim.log.levels.ERROR)
        end
    end)
end

function M.revert_changes()
    if not state.last_backup_id then
        vim.notify('No backup available to revert', vim.log.levels.INFO)
        return
    end

    local confirm = vim.fn.confirm('Revert all changes from the last apply operation?', '&Yes\n&No', 2)
    if confirm ~= 1 then
        return
    end

    local operation_id = generate_operation_id('revert')
    vim.notify(string.format('Reverting changes (operation: %s)...', operation_id), vim.log.levels.INFO)
    run_async_command('bha.revertChanges', {
        backupId = state.last_backup_id,
        operationId = operation_id,
    }, 'Revert changes', function(err, result)
        if err then
            vim.notify('Failed to revert: ' .. vim.inspect(err), vim.log.levels.ERROR)
            append_activity('Revert failed', safe_string(err, 'Unknown error'))
            return
        end

        if not result then
            vim.notify('Revert returned no result', vim.log.levels.ERROR)
            return
        end

        if result.success then
            local num_files = safe_table_length(result.restoredFiles)
            state.last_backup_id = nil
            append_activity('Changes reverted', string.format('Restored %d file(s)', num_files), {})
            vim.notify(string.format('Reverted successfully. Restored %d files.', num_files), vim.log.levels.INFO)
            vim.cmd('checktime')
        else
            local errors = result.errors or {}
            local error_msg = #errors > 0 and safe_string(errors[1].message, 'Unknown error') or 'Unknown error'
            append_activity('Revert failed', error_msg, {})
            vim.notify('Failed to revert: ' .. error_msg, vim.log.levels.ERROR)
        end
    end)
end

local function choose_suggestion(prompt, callback)
    local suggestions = state.suggestions_cache or {}
    if #suggestions == 0 then
        vim.notify('No suggestions available. Run :BHAAnalyze first.', vim.log.levels.INFO)
        return
    end
    local items = {}
    for _, suggestion in ipairs(suggestions) do
        table.insert(items, string.format('[%s] %s',
            PRIORITY_LABELS[safe_priority(suggestion.priority) + 1] or 'UNKNOWN',
            safe_string(suggestion.title, 'Untitled')))
    end
    vim.ui.select(items, { prompt = prompt }, function(_, index)
        if index then
            callback(suggestions[index].id)
        end
    end)
end

function M.show_suggestion_details(suggestion_id)
    if not suggestion_id then
        choose_suggestion('Select suggestion details:', M.show_suggestion_details)
        return
    end
    execute_command('bha.getSuggestionDetails', { { suggestionId = suggestion_id } }, function(err, details)
        if err or type(details) ~= 'table' then
            vim.notify('Failed to get suggestion details: ' .. vim.inspect(err or details), vim.log.levels.ERROR)
            return
        end
        local lines = {
            '# ' .. safe_string(details.title, 'BHA Suggestion'),
            '',
            'Evidence: ' .. format_evidence(details.estimatedSavingsEvidence),
            'Priority: ' .. (PRIORITY_LABELS[safe_priority(details.priority) + 1] or 'UNKNOWN'),
            string.format('Confidence: %d%%', math.floor(safe_confidence(details.confidence) * 100)),
            'Application mode: ' .. safe_string(details.applicationMode, 'unknown'),
            '',
            '## Description',
            safe_string(details.description, 'Unavailable'),
        }
        if details.rationale and details.rationale ~= '' then
            table.insert(lines, '')
            table.insert(lines, '## Rationale')
            table.insert(lines, details.rationale)
        end
        if details.applicationGuidance and details.applicationGuidance ~= '' then
            table.insert(lines, '')
            table.insert(lines, '## Application Guidance')
            table.insert(lines, details.applicationGuidance)
        end
        if details.autoApplyBlockedReason and details.autoApplyBlockedReason ~= '' then
            table.insert(lines, '')
            table.insert(lines, '## Manual Review')
            table.insert(lines, details.autoApplyBlockedReason)
        end
        for label, key in pairs({
            ['Files to create'] = 'filesToCreate',
            ['Files to modify'] = 'filesToModify',
            ['Dependencies'] = 'dependencies',
        }) do
            if type(details[key]) == 'table' and #details[key] > 0 then
                table.insert(lines, '')
                table.insert(lines, '## ' .. label)
                for _, item in ipairs(details[key]) do
                    table.insert(lines, '- ' .. safe_string(item, ''))
                end
            end
        end
        local edits = details.textEdits or details.text_edits or {}
        if #edits > 0 then
            table.insert(lines, '')
            table.insert(lines, '## Concrete Text Edits')
            for _, edit in ipairs(edits) do
                table.insert(lines, string.format('- %s:%d:%d', safe_string(edit.file, 'unknown'),
                    safe_number(edit.startLine, 0) + 1, safe_number(edit.startCol, 0) + 1))
                table.insert(lines, '  ' .. safe_string(edit.newText, '[delete]'))
            end
        end
        open_text_float(lines, 'BHA Suggestion Details')
    end)
end

local function utf8_codepoint(text, index)
    local first = string.byte(text, index)
    if not first then
        return 0, 0
    end
    if first < 0x80 then
        return 1, first
    elseif first >= 0xC2 and first <= 0xDF then
        return 2, ((first - 0xC0) * 0x40) + (string.byte(text, index + 1) - 0x80)
    elseif first >= 0xE0 and first <= 0xEF then
        return 3, ((first - 0xE0) * 0x1000) + ((string.byte(text, index + 1) - 0x80) * 0x40) +
            (string.byte(text, index + 2) - 0x80)
    elseif first >= 0xF0 and first <= 0xF4 then
        return 4, ((first - 0xF0) * 0x40000) + ((string.byte(text, index + 1) - 0x80) * 0x1000) +
            ((string.byte(text, index + 2) - 0x80) * 0x40) + (string.byte(text, index + 3) - 0x80)
    end
    return 1, first
end

local function utf16_column_to_byte(line, column)
    if column < 0 then
        return nil
    end
    local byte_index = 1
    local units = 0
    while byte_index <= #line do
        if units == column then
            return byte_index - 1
        end
        local length, codepoint = utf8_codepoint(line, byte_index)
        local width = codepoint > 0xFFFF and 2 or 1
        if units + width > column then
            return nil
        end
        units = units + width
        byte_index = byte_index + length
    end
    return units == column and #line or nil
end

local function line_column_to_offset(content, line_number, column)
    if line_number < 0 or column < 0 then
        return nil
    end
    local line_start = 1
    for _ = 1, line_number do
        local newline = content:find('\n', line_start, true)
        if not newline then
            return nil
        end
        line_start = newline + 1
    end
    local line_end = content:find('\n', line_start, true) or (#content + 1)
    local line = content:sub(line_start, line_end - 1)
    if line:sub(-1) == '\r' then
        line = line:sub(1, -2)
    end
    local relative = utf16_column_to_byte(line, column)
    return relative and (line_start - 1 + relative) or nil
end

local function apply_preview_edits(content, edits)
    local ranges = {}
    for index, edit in ipairs(edits) do
        local start_offset = line_column_to_offset(content, safe_number(edit.startLine, -1), safe_number(edit.startCol, -1))
        local end_offset = line_column_to_offset(content, safe_number(edit.endLine, -1), safe_number(edit.endCol, -1))
        if not start_offset or not end_offset or end_offset < start_offset then
            return nil
        end
        table.insert(ranges, { start = start_offset, finish = end_offset,
            new_text = safe_string(edit.newText, ''), index = index })
    end
    table.sort(ranges, function(left, right)
        return left.start < right.start or (left.start == right.start and
            (left.finish < right.finish or (left.finish == right.finish and left.index < right.index)))
    end)
    for index = 2, #ranges do
        if ranges[index - 1].finish > ranges[index].start then
            return nil
        end
    end
    local preview = content
    for index = #ranges, 1, -1 do
        local edit = ranges[index]
        preview = preview:sub(1, edit.start) .. edit.new_text .. preview:sub(edit.finish + 1)
    end
    return preview
end

local function resolve_edit_path(candidate)
    local path = safe_string(candidate, '')
    if path == '' then
        return nil
    end
    if path:sub(1, 7) == 'file://' and vim.uri_to_fname then
        local ok, decoded = pcall(vim.uri_to_fname, path)
        if ok then
            path = decoded
        end
    elseif not path:match('^/') and not path:match('^%a:[/\\]') then
        path = workspace_root() .. '/' .. path
    end
    return vim.fn.fnamemodify(path, ':p')
end

local function read_file(path)
    local handle = io.open(path, 'rb')
    if not handle then
        return ''
    end
    local content = handle:read('*a') or ''
    handle:close()
    return content
end

function M.preview_suggestion(suggestion_id)
    if not suggestion_id then
        choose_suggestion('Select suggestion diff preview:', M.preview_suggestion)
        return
    end
    execute_command('bha.getSuggestionDetails', { { suggestionId = suggestion_id } }, function(err, details)
        if err or type(details) ~= 'table' then
            vim.notify('Failed to get suggestion details: ' .. vim.inspect(err or details), vim.log.levels.ERROR)
            return
        end
        local edits = details.textEdits or details.text_edits or {}
        if #edits == 0 then
            vim.notify('This suggestion has no concrete text edits to preview', vim.log.levels.INFO)
            return
        end
        local edits_by_file = {}
        for _, edit in ipairs(edits) do
            local path = resolve_edit_path(edit.file)
            if not path then
                vim.notify('Suggestion contains an invalid edit path', vim.log.levels.ERROR)
                return
            end
            edits_by_file[path] = edits_by_file[path] or {}
            table.insert(edits_by_file[path], edit)
        end
        local lines = { '# BHA Diff Preview', '' }
        for path, file_edits in pairs(edits_by_file) do
            local original = read_file(path)
            local preview = apply_preview_edits(original, file_edits)
            if not preview then
                table.insert(lines, 'Unable to map edits to ' .. path)
            elseif type(vim.diff) == 'function' then
                table.insert(lines, '--- ' .. path)
                table.insert(lines, '+++ BHA preview')
                local diff = vim.diff(original, preview, { result_type = 'unified', ctxlen = 3 })
                if diff == '' then
                    table.insert(lines, '(no textual change)')
                else
                    for line in diff:gmatch('[^\n]*\n?') do
                        if line ~= '' then
                            table.insert(lines, line:gsub('\n$', ''))
                        end
                    end
                end
                table.insert(lines, '')
            else
                table.insert(lines, '--- ' .. path)
                table.insert(lines, 'Neovim does not provide vim.diff; preview content follows:')
                for line in preview:gmatch('[^\n]*\n?') do
                    if line ~= '' then
                        table.insert(lines, line:gsub('\n$', ''))
                    end
                end
            end
        end
        open_text_float(lines, 'BHA Diff Preview', 'diff')
    end)
end

function M.show_history()
    local lines = { '# BHA Analysis History', '', 'Workspace: ' .. workspace_root(), '' }
    local history = state.history[workspace_root()] or {}
    if #history == 0 then
        table.insert(lines, 'No completed analysis runs are stored for this workspace.')
    else
        for index, run in ipairs(history) do
            table.insert(lines, string.format('%d. %s', index, safe_string(run.recordedAt, 'unknown time')))
            table.insert(lines, string.format('   Build: %.2fs (%s)', safe_number(run.totalBuildTimeMs, 0) / 1000,
                safe_string(run.buildTimeSource, 'unknown')))
            table.insert(lines, string.format('   Suggestions: %d | Compilation units: %d',
                safe_number(run.suggestionCount, 0), safe_number(run.filesAnalyzed, 0)))
        end
    end
    open_text_float(lines, 'BHA Analysis History')
end

function M.show_activity_log()
    local lines = { '# BHA Activity Log', '' }
    if #state.activity_log == 0 then
        table.insert(lines, 'No BHA activity has been recorded in this session.')
    else
        for _, entry in ipairs(state.activity_log) do
            table.insert(lines, string.format('[%s] %s: %s', safe_string(entry.timestamp, 'unknown'),
                safe_string(entry.label, 'BHA'), safe_string(entry.detail, '')))
            for _, detail in ipairs(entry.details or {}) do
                if detail ~= '' then
                    table.insert(lines, '  ' .. detail)
                end
            end
        end
    end
    open_text_float(lines, 'BHA Activity Log')
end

function M.show_progress()
    local lines = { '# BHA Active Progress', '' }
    local count = 0
    for token, progress in pairs(state.progress) do
        count = count + 1
        table.insert(lines, string.format('%s: %s (%s)', safe_string(progress.title, 'BHA operation'),
            safe_string(progress.message, 'running'), safe_string(token, 'unknown token')))
    end
    if count == 0 then
        table.insert(lines, 'No active progress notifications.')
    end
    open_text_float(lines, 'BHA Progress')
end

-- ============================================================================
-- Setup
-- ============================================================================

function M.setup(opts)
    opts = opts or {}
    local user_on_attach = opts.on_attach
    M.config = vim.tbl_deep_extend('force', M.defaults, opts)
    load_history()

    local on_attach = function(client, bufnr)
        install_client_handlers(client)
        if user_on_attach then
            user_on_attach(client, bufnr)
        end
    end

    -- Register commands
    vim.api.nvim_create_user_command('BHARecordBuildTraces', M.record_build_traces, {
        desc = 'Record build traces for the current project',
    })
    vim.api.nvim_create_user_command('BHAAnalyze', M.analyze_project, {
        desc = 'Analyze project build performance',
    })
    vim.api.nvim_create_user_command('BHAShowSuggestions', function()
        M.show_suggestions()
    end, {
        desc = 'Show build optimization suggestions',
    })
    vim.api.nvim_create_user_command('BHAApplySuggestion', function()
        M.apply_suggestion()
    end, {
        desc = 'Apply a single suggestion',
    })
    vim.api.nvim_create_user_command('BHAApplyAll', function(cmd_opts)
        local safe_only = cmd_opts.bang
        M.apply_all_suggestions(safe_only)
    end, {
        bang = true,
        desc = 'Apply all suggestions (use ! for safe-only)',
    })
    vim.api.nvim_create_user_command('BHARevert', M.revert_changes, {
        desc = 'Revert last applied changes',
    })
    vim.api.nvim_create_user_command('BHASuggestionDetails', function()
        M.show_suggestion_details()
    end, {
        desc = 'Show evidence and application details for a suggestion',
    })
    vim.api.nvim_create_user_command('BHAPreviewSuggestion', function()
        M.preview_suggestion()
    end, {
        desc = 'Preview concrete suggestion edits as a native diff',
    })
    vim.api.nvim_create_user_command('BHAHistory', M.show_history, {
        desc = 'Show persisted BHA analysis history',
    })
    vim.api.nvim_create_user_command('BHAActivityLog', M.show_activity_log, {
        desc = 'Show BHA progress, job, and operation activity',
    })
    vim.api.nvim_create_user_command('BHAProgress', M.show_progress, {
        desc = 'Show active BHA progress notifications',
    })
    vim.api.nvim_create_user_command('BHACancelJob', function(cmd_opts)
        M.cancel_job(cmd_opts.args ~= '' and cmd_opts.args or nil)
    end, {
        nargs = '?',
        desc = 'Cancel an active BHA job, optionally by job ID',
    })

    -- Register LSP config
    require('lspconfig.configs').bha_lsp = {
        default_config = {
            cmd = M.config.cmd,
            filetypes = M.config.filetypes,
            root_dir = M.config.root_dir,
            single_file_support = M.config.single_file_support,
            settings = M.config.settings,
            on_attach = on_attach,
        },
    }
end

return M
