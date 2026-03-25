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
}

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

-- ============================================================================
-- LSP Command Execution
-- ============================================================================

local function get_client()
    local clients = vim.lsp.get_active_clients({ name = 'bha-lsp' })
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

-- ============================================================================
-- Core Commands
-- ============================================================================

function M.analyze_project()
    local operation_id = generate_operation_id('analyze')
    local root_dir = vim.fn.getcwd()
    local build_dir = vim.fn.input('Build directory (leave empty for auto-detect): ')

    local args = {
        {
            projectRoot = root_dir,
            buildDir = build_dir ~= '' and build_dir or vim.NIL,
            rebuild = false,
            operationId = operation_id,
        }
    }

    local client = get_client()
    if not client then
        vim.notify('BHA LSP server not running', vim.log.levels.ERROR)
        return
    end

    vim.notify(string.format('Analyzing project (operation: %s)...', operation_id), vim.log.levels.INFO)

    client.request('workspace/executeCommand', {
        command = 'bha.analyze',
        arguments = args,
    }, function(err, result)
        if err then
            vim.notify('Analysis failed: ' .. vim.inspect(err), vim.log.levels.ERROR)
            return
        end

        if result then
            state.analysis_result = result
            local valid_suggestions = filter_valid_suggestions(result.suggestions or {})
            state.suggestions_cache = valid_suggestions
            local num_suggestions = #valid_suggestions

            vim.notify(string.format('Analysis complete: %d suggestions found', num_suggestions), vim.log.levels.INFO)

            if num_suggestions > 0 then
                M.show_suggestions(result)
            end
        end
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
                state.analysis_result = res
                state.suggestions_cache = filter_valid_suggestions(res.suggestions or {})
            end
            M._display_suggestions(res)
        end)
    else
        M._display_suggestions(result)
    end
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
    table.insert(lines, 'Press: A = Apply All | S = Apply Safe | R = Revert | q = Close')
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

        if sug.estimatedImpact then
            local impact = sug.estimatedImpact
            local time_saved = safe_number(impact.timeSavedMs, 0) / 1000
            table.insert(lines, string.format(
                '    Impact: %.2fs (%.1f%%) | %d files affected',
                time_saved,
                safe_number(impact.percentage, 0),
                safe_number(impact.filesAffected, 0)
            ))
        end

        table.insert(lines, '')
    end

    if #suggestions > max_display then
        table.insert(lines, string.format('... and %d more suggestions', #suggestions - max_display))
        table.insert(lines, '')
    end

    table.insert(lines, 'Press <CR> on a suggestion number to apply it')
    table.insert(lines, 'Press q to close')

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

    local client = get_client()
    if not client then
        vim.notify('BHA LSP server not running', vim.log.levels.ERROR)
        return
    end

    local operation_id = generate_operation_id('apply')
    vim.notify(string.format('Applying suggestion (operation: %s)...', operation_id), vim.log.levels.INFO)

    client.request('workspace/executeCommand', {
        command = 'bha.applySuggestion',
        arguments = {{ suggestionId = suggestion_id, operationId = operation_id }},
    }, function(err, result)
        if err then
            vim.notify('Failed to apply suggestion: ' .. vim.inspect(err), vim.log.levels.ERROR)
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
            vim.notify(string.format('Suggestion applied successfully. Modified %d files.', num_files), vim.log.levels.INFO)
            vim.cmd('checktime')
        else
            local errors = result.errors or {}
            local error_msg = #errors > 0 and safe_string(errors[1].message, 'Unknown error') or 'Unknown error'
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

    local client = get_client()
    if not client then
        vim.notify('BHA LSP server not running', vim.log.levels.ERROR)
        return
    end

    local operation_id = generate_operation_id('apply-all')
    local min_priority = M.config.default_priority_filter or M.defaults.default_priority_filter

    vim.notify(string.format('Applying %d suggestions atomically (operation: %s)...', affected_count, operation_id), vim.log.levels.INFO)

    client.request('workspace/executeCommand', {
        command = 'bha.applyAllSuggestions',
        arguments = {{
            minPriority = min_priority,
            safeOnly = safe_only or false,
            atomic = true,
            operationId = operation_id,
        }},
    }, function(err, result)
        if err then
            vim.notify('Failed to apply suggestions: ' .. vim.inspect(err), vim.log.levels.ERROR)
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
            vim.notify(msg, vim.log.levels.INFO)
            vim.cmd('checktime')
        else
            local errors = result.errors or {}
            local error_summary = ''
            if #errors > 0 then
                error_summary = ' First error: ' .. safe_string(errors[1].message, 'Unknown')
            end
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

    local client = get_client()
    if not client then
        vim.notify('BHA LSP server not running', vim.log.levels.ERROR)
        return
    end

    local operation_id = generate_operation_id('revert')
    vim.notify(string.format('Reverting changes (operation: %s)...', operation_id), vim.log.levels.INFO)

    client.request('workspace/executeCommand', {
        command = 'bha.revertChanges',
        arguments = {{ backupId = state.last_backup_id, operationId = operation_id }},
    }, function(err, result)
        if err then
            vim.notify('Failed to revert: ' .. vim.inspect(err), vim.log.levels.ERROR)
            return
        end

        if not result then
            vim.notify('Revert returned no result', vim.log.levels.ERROR)
            return
        end

        if result.success then
            local num_files = safe_table_length(result.restoredFiles)
            state.last_backup_id = nil
            vim.notify(string.format('Reverted successfully. Restored %d files.', num_files), vim.log.levels.INFO)
            vim.cmd('checktime')
        else
            local errors = result.errors or {}
            local error_msg = #errors > 0 and safe_string(errors[1].message, 'Unknown error') or 'Unknown error'
            vim.notify('Failed to revert: ' .. error_msg, vim.log.levels.ERROR)
        end
    end)
end

-- ============================================================================
-- Setup
-- ============================================================================

function M.setup(opts)
    opts = opts or {}
    M.config = vim.tbl_deep_extend('force', M.defaults, opts)

    -- Register commands
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

    -- Register LSP config
    require('lspconfig.configs').bha_lsp = {
        default_config = {
            cmd = M.config.cmd,
            filetypes = M.config.filetypes,
            root_dir = M.config.root_dir,
            single_file_support = M.config.single_file_support,
            settings = M.config.settings,
        },
    }
end

return M
