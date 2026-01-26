local M = {}

local lsp = vim.lsp
local util = require('lspconfig.util')

M.config = {
    cmd = { 'bha-lsp' },
    filetypes = { 'c', 'cpp', 'objc', 'objcpp' },
    root_dir = util.root_pattern('CMakeLists.txt', 'Makefile', 'meson.build', 'compile_commands.json', '.git'),
    single_file_support = false,
    settings = {},
}

local function execute_command(command, args)
    local params = {
        command = command,
        arguments = args or {},
    }
    local client = vim.lsp.get_active_clients({ name = 'bha-lsp' })[1]
    if not client then
        vim.notify('BHA LSP server not running', vim.log.levels.ERROR)
        return
    end
    client.request('workspace/executeCommand', params, function(err, result)
        if err then
            vim.notify('Command failed: ' .. vim.inspect(err), vim.log.levels.ERROR)
        else
            return result
        end
    end)
end

function M.analyze_project()
    local root_dir = vim.fn.getcwd()
    local build_dir = vim.fn.input('Build directory (leave empty for auto-detect): ')

    local args = {
        {
            projectRoot = root_dir,
            buildDir = build_dir ~= '' and build_dir or vim.NIL,
            rebuild = false,
        }
    }

    local client = vim.lsp.get_active_clients({ name = 'bha-lsp' })[1]
    if not client then
        vim.notify('BHA LSP server not running', vim.log.levels.ERROR)
        return
    end

    vim.notify('Analyzing project...', vim.log.levels.INFO)

    client.request('workspace/executeCommand', {
        command = 'bha.analyze',
        arguments = args,
    }, function(err, result)
        if err then
            vim.notify('Analysis failed: ' .. vim.inspect(err), vim.log.levels.ERROR)
            return
        end

        if result then
            local num_suggestions = #(result.suggestions or {})
            vim.notify(string.format('Analysis complete: %d suggestions found', num_suggestions), vim.log.levels.INFO)

            if num_suggestions > 0 then
                M.show_suggestions(result)
            end
        end
    end)
end

function M.show_suggestions(analysis_result)
    local client = vim.lsp.get_active_clients({ name = 'bha-lsp' })[1]
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
            result = res
            M._display_suggestions(result)
        end)
    else
        M._display_suggestions(result)
    end
end

function M._display_suggestions(result)
    local suggestions = result.suggestions or {}
    if #suggestions == 0 then
        vim.notify('No suggestions available. Run :BHAAnalyze first.', vim.log.levels.INFO)
        return
    end

    local lines = {}
    local highlights = {}

    table.insert(lines, '# Build Hotspot Analysis')
    table.insert(lines, '')

    if result.baselineMetrics then
        local metrics = result.baselineMetrics
        table.insert(lines, '## Build Metrics')
        table.insert(lines, string.format('Total Build Time: %.2fs', (metrics.totalDurationMs or 0) / 1000))
        table.insert(lines, string.format('Files Compiled: %d', metrics.filesCompiled or 0))
        table.insert(lines, '')
    end

    table.insert(lines, string.format('## Optimization Suggestions (%d)', #suggestions))
    table.insert(lines, '')

    for i, sug in ipairs(suggestions) do
        local priority = ({ 'HIGH', 'MEDIUM', 'LOW' })[sug.priority + 1] or 'UNKNOWN'
        local confidence = math.floor(sug.confidence * 100)

        table.insert(lines, string.format('[%d] %s', i, sug.title))
        table.insert(lines, string.format('    Priority: %s | Confidence: %d%%', priority, confidence))
        table.insert(lines, string.format('    %s', sug.description))

        if sug.estimatedImpact then
            local impact = sug.estimatedImpact
            local time_saved = (impact.timeSavedMs or 0) / 1000
            table.insert(lines, string.format(
                '    Impact: %.2fs (%.1f%%) • %d files affected',
                time_saved,
                impact.percentage or 0,
                impact.filesAffected or 0
            ))
        end

        table.insert(lines, '')
    end

    table.insert(lines, 'Press <CR> on a suggestion number to apply it')
    table.insert(lines, 'Press q to close')

    local buf = vim.api.nvim_create_buf(false, true)
    vim.api.nvim_buf_set_lines(buf, 0, -1, false, lines)
    vim.api.nvim_buf_set_option(buf, 'modifiable', false)
    vim.api.nvim_buf_set_option(buf, 'filetype', 'markdown')

    local width = math.floor(vim.o.columns * 0.8)
    local height = math.floor(vim.o.lines * 0.8)
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

    vim.api.nvim_buf_set_keymap(buf, 'n', 'q', ':close<CR>', { silent = true, noremap = true })
    vim.api.nvim_buf_set_keymap(buf, 'n', '<CR>', '', {
        silent = true,
        noremap = true,
        callback = function()
            local line = vim.api.nvim_win_get_cursor(win)[1]
            local content = vim.api.nvim_buf_get_lines(buf, line - 1, line, false)[1]
            local idx = content:match('^%[(%d+)%]')
            if idx then
                local suggestion = suggestions[tonumber(idx)]
                if suggestion then
                    M.apply_suggestion(suggestion.id)
                    vim.api.nvim_win_close(win, true)
                end
            end
        end
    })

    M._suggestions_cache = suggestions
end

function M.apply_suggestion(suggestion_id)
    local id = suggestion_id
    if not id then
        local suggestions = M._suggestions_cache or {}
        if #suggestions == 0 then
            vim.notify('No suggestions available', vim.log.levels.ERROR)
            return
        end

        local items = {}
        for i, sug in ipairs(suggestions) do
            local priority = ({ 'HIGH', 'MEDIUM', 'LOW' })[sug.priority + 1] or 'UNKNOWN'
            table.insert(items, string.format('[%s] %s - %s', priority, sug.title, sug.description))
        end

        vim.ui.select(items, {
            prompt = 'Select suggestion to apply:',
        }, function(choice, idx)
            if not idx then return end
            id = suggestions[idx].id
            M._do_apply_suggestion(id)
        end)
    else
        M._do_apply_suggestion(id)
    end
end

function M._do_apply_suggestion(suggestion_id)
    local confirm = vim.fn.confirm('Apply this suggestion? This will modify your code.', '&Yes\n&No', 2)
    if confirm ~= 1 then
        return
    end

    local client = vim.lsp.get_active_clients({ name = 'bha-lsp' })[1]
    if not client then
        vim.notify('BHA LSP server not running', vim.log.levels.ERROR)
        return
    end

    vim.notify('Applying suggestion...', vim.log.levels.INFO)

    client.request('workspace/executeCommand', {
        command = 'bha.applySuggestion',
        arguments = {{ suggestionId = suggestion_id }},
    }, function(err, result)
        if err then
            vim.notify('Failed to apply suggestion: ' .. vim.inspect(err), vim.log.levels.ERROR)
            return
        end

        if result.success then
            local num_files = #(result.changedFiles or {})
            vim.notify(string.format('Suggestion applied successfully. Modified %d files.', num_files), vim.log.levels.INFO)

            vim.cmd('checktime')
        else
            local errors = result.errors or {}
            local error_msg = #errors > 0 and errors[1].message or 'Unknown error'
            vim.notify('Failed to apply suggestion: ' .. error_msg, vim.log.levels.ERROR)
        end
    end)
end

function M.setup(opts)
    opts = opts or {}
    M.config = vim.tbl_deep_extend('force', M.config, opts)

    vim.api.nvim_create_user_command('BHAAnalyze', M.analyze_project, {})
    vim.api.nvim_create_user_command('BHAShowSuggestions', function() M.show_suggestions() end, {})
    vim.api.nvim_create_user_command('BHAApplySuggestion', function() M.apply_suggestion() end, {})

    require('lspconfig.configs').bha_lsp = {
        default_config = M.config,
    }
end

return M
