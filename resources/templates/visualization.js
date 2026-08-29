        let currentTransform = d3.zoomIdentity;
        let graphSimulation = null;
        let graphZoom = null;
        let allFilesData = [];

        document.addEventListener('DOMContentLoaded', function() {
            allFilesData = (analysisData.files || []).slice();
            bindControls();
            updateFileStats();
            applyFileLimit();
        });

        function showTab(tabId) {
            document.querySelectorAll('.tab').forEach(tab => {
                const active = tab.dataset.tab === tabId;
                tab.classList.toggle('active', active);
                tab.setAttribute('aria-selected', active ? 'true' : 'false');
            });
            document.querySelectorAll('.tab-content').forEach(content => {
                const active = content.id === tabId;
                content.classList.toggle('active', active);
                content.hidden = !active;
            });

            if (tabId === 'include-tree') renderIncludeTree();
            if (tabId === 'timeline') renderTimeline();
            if (tabId === 'treemap') renderTreemap();
            if (tabId === 'templates') renderTemplates();
            if (tabId === 'memory') renderMemoryChart();
            if (tabId === 'dependencies') renderDependencyGraph();
            if (tabId === 'build-context') renderBuildContext();
        }

        function bindControls() {
            document.querySelectorAll('.tab[data-tab]').forEach(tab => {
                tab.addEventListener('click', () => showTab(tab.dataset.tab));
            });

            const actions = {
                'filter-files': filterFiles,
                'sort-files': sortFiles,
                'apply-file-limit': applyFileLimit,
                'render-include-tree': renderIncludeTree,
                'reset-include-tree': resetIncludeTree,
                'render-timeline': renderTimeline,
                'render-treemap': renderTreemap,
                'render-templates': renderTemplates,
                'render-memory': renderMemoryChart,
                'render-dependencies': renderDependencyGraph,
                'reset-zoom': resetZoom
            };

            document.querySelectorAll('[data-action]').forEach(control => {
                const action = actions[control.dataset.action];
                if (!action) return;
                const eventName = control.matches('input[type="text"]') ? 'input' :
                    control.matches('select') ? 'change' : 'click';
                control.addEventListener(eventName, action);
            });

            document.querySelectorAll('#timeline-limit, #treemap-limit, #template-limit, #memory-limit, #dep-limit, #tree-depth, #tree-limit')
                .forEach(control => control.addEventListener('change', () => {
                    const tab = control.closest('.tab-content');
                    if (tab && tab.classList.contains('active')) {
                        const renderers = {
                            'include-tree': renderIncludeTree,
                            timeline: renderTimeline,
                            treemap: renderTreemap,
                            templates: renderTemplates,
                            memory: renderMemoryChart,
                            dependencies: renderDependencyGraph
                        };
                        renderers[tab.id]?.();
                    }
                }));
        }

        function numericValue(value) {
            const number = Number(value);
            return Number.isFinite(number) && number > 0 ? number : 0;
        }

        function fileCompileTime(file) {
            return numericValue(file.compile_time_ms);
        }

        function templateCompileTime(template) {
            return numericValue(template.total_time_ms);
        }

        function templateLabel(template) {
            return template.full_signature || template.name || 'Unnamed specialization';
        }

        function updateFileStats() {
            const tbody = document.querySelector('#files-table tbody');
            const rows = tbody.querySelectorAll('tr');
            const visibleRows = Array.from(rows).filter(r => r.style.display !== 'none');

            document.getElementById('files-shown').textContent = visibleRows.length;
            document.getElementById('files-total').textContent = rows.length;
        }

        function filterFiles() {
            const query = document.getElementById('file-search').value.toLowerCase();
            const tbody = document.querySelector('#files-table tbody');
            const rows = tbody.querySelectorAll('tr');

            rows.forEach(row => {
                row.style.display = row.textContent.toLowerCase().includes(query) ? '' : 'none';
            });
            updateFileStats();
        }

        function sortFiles() {
            const sortBy = document.getElementById('files-sort').value;
            const tbody = document.querySelector('#files-table tbody');
            const rows = Array.from(tbody.querySelectorAll('tr'));

            rows.sort((a, b) => {
                switch(sortBy) {
                    case 'time-desc':
                        return parseFloat(b.dataset.time) - parseFloat(a.dataset.time);
                    case 'time-asc':
                        return parseFloat(a.dataset.time) - parseFloat(b.dataset.time);
                    case 'name-asc':
                        return a.dataset.name.localeCompare(b.dataset.name);
                    default:
                        return 0;
                }
            });

            rows.forEach(row => tbody.appendChild(row));
            updateFileStats();
        }

        function applyFileLimit() {
            const limit = parseInt(document.getElementById('files-limit').value) || 100;
            const tbody = document.querySelector('#files-table tbody');
            const rows = Array.from(tbody.querySelectorAll('tr'));

            rows.forEach((row, idx) => {
                row.style.display = idx < limit ? '' : 'none';
            });
            updateFileStats();
        }

        function renderIncludeTree() {
            const container = d3.select('#include-tree-container');
            container.selectAll('*').remove();

            if (!analysisData.dependencies || !analysisData.dependencies.graph) {
                container.html('<div class="loading"><p>No dependency data available</p></div>');
                return;
            }

            const maxDepth = parseInt(document.getElementById('tree-depth').value) || 3;
            const maxNodes = parseInt(document.getElementById('tree-limit').value) || 50;

            const width = Math.max(320, container.node().getBoundingClientRect().width);
            const height = 800;

            const svg = container.append('svg')
                .attr('width', width)
                .attr('height', height);

            const g = svg.append('g');

            const zoom = d3.zoom()
                .scaleExtent([0.1, 4])
                .on('zoom', (event) => {
                    g.attr('transform', event.transform);
                });

            svg.call(zoom);

            // Build tree from graph data with limits
            const graph = analysisData.dependencies.graph;
            const allNodes = graph.nodes || [];
            const allLinks = graph.links || [];

            if (!allNodes.length) {
                container.html('<div class="loading"><p>No nodes available</p></div>');
                return;
            }

            const allNodeMap = new Map(allNodes.map(n => [n.id, n]));
            const childrenById = new Map(allNodes.map(n => [n.id, []]));

            // Build parent-child relationships
            const hasIncoming = new Set();
            allLinks.forEach(link => {
                const sourceId = typeof link.source === 'object' ? link.source.id : link.source;
                const targetId = typeof link.target === 'object' ? link.target.id : link.target;

                if (allNodeMap.has(sourceId) && allNodeMap.has(targetId)) {
                    hasIncoming.add(targetId);
                    const children = childrenById.get(sourceId);
                    if (children && !children.includes(targetId)) {
                        children.push(targetId);
                    }
                }
            });

            // Find root nodes (nodes with no incoming edges) - these are typically source files
            let roots = allNodes.filter(n => !hasIncoming.has(n.id));

            if (roots.length === 0) {
                // If no roots found (circular deps), pick nodes with most outgoing connections
                const outgoingCount = new Map();
                allLinks.forEach(l => {
                    const sourceId = typeof l.source === 'object' ? l.source.id : l.source;
                    outgoingCount.set(sourceId, (outgoingCount.get(sourceId) || 0) + 1);
                });
                roots = allNodes
                    .slice()
                    .sort((a, b) => (outgoingCount.get(b.id) || 0) - (outgoingCount.get(a.id) || 0))
                    .slice(0, 5);
            } else {
                // Limit roots to prevent overcrowding
                roots = roots.slice(0, Math.min(5, Math.ceil(maxNodes / 10)));
            }

            if (roots.length === 0) {
                container.html('<div class="loading"><p>No root nodes found</p></div>');
                return;
            }

            function cloneTree(nodeId, path, depth, budget) {
                const source = allNodeMap.get(nodeId);
                if (!source || budget.remaining <= 0) return null;

                const node = {...source, children: []};
                budget.remaining -= 1;
                if (depth >= maxDepth) return node;

                const nextPath = new Set(path);
                nextPath.add(nodeId);
                const childIds = childrenById.get(nodeId) || [];
                for (const childId of childIds) {
                    if (nextPath.has(childId)) continue;
                    const child = cloneTree(childId, nextPath, depth + 1, budget);
                    if (child) node.children.push(child);
                    if (budget.remaining <= 0) break;
                }
                return node;
            }

            // Tree layout for each root
            const tree = d3.tree().size([height - 100, Math.max(120, width - 200)]);

            let yOffset = 50;
            let nodesPerRoot = Math.floor(maxNodes / roots.length);

            roots.forEach((root, idx) => {
                const rootNode = cloneTree(root.id, new Set(), 0, {remaining: nodesPerRoot});
                if (!rootNode) return;

                const hierarchy = d3.hierarchy(rootNode);
                const treeData = tree(hierarchy);

                const treeG = g.append('g')
                    .attr('transform', `translate(100, ${yOffset})`);

                // Links
                treeG.selectAll('.tree-link')
                    .data(treeData.links())
                    .join('path')
                    .attr('class', 'tree-link')
                    .attr('d', d3.linkHorizontal()
                        .x(d => d.y)
                        .y(d => d.x));

                // Nodes
                const node = treeG.selectAll('.tree-node')
                    .data(treeData.descendants())
                    .join('g')
                    .attr('class', d => `tree-node ${d.data.type || 'header'}`)
                    .attr('transform', d => `translate(${d.y},${d.x})`)
                    .on('click', function(event, d) {
                        if (d.children && d.children.length) {
                            d._children = d.children;
                            d.children = null;
                        } else if (d._children && d._children.length) {
                            d.children = d._children;
                            d._children = null;
                        }
                        renderIncludeTree();
                    })
                    .on('mouseover', (event, d) => {
                        showTooltip(event, {
                            name: d.data.id.split('/').pop().split('\\\\').pop(),
                            fullPath: d.data.id,
                            type: d.data.type,
                            depth: d.depth,
                            children: (d.children || []).length + (d._children || []).length
                        });
                    })
                    .on('mouseout', hideTooltip);

                node.append('circle')
                    .attr('r', d => {
                        if (d.children && d.children.length > 0) return 6;
                        if (d._children && d._children.length > 0) return 5;
                        return 4;
                    })
                    .style('fill', d => {
                        if (d._children) return 'var(--text-muted)'; // Has collapsed children
                        return null; // Use CSS default
                    });

                node.append('text')
                    .attr('dx', 10)
                    .attr('dy', 4)
                    .text(d => {
                        const name = d.data.id.split('/').pop().split('\\\\').pop();
                        const shortName = name.length > 30 ? name.substring(0, 27) + '...' : name;
                        const childIndicator = d._children ? ` (+${d._children.length})` : '';
                        return shortName + childIndicator;
                    });

                yOffset += Math.max(treeData.height * 80 + 100, 150);
            });

            svg.attr('height', Math.max(height, yOffset + 40));
            svg.call(zoom.transform, d3.zoomIdentity);
        }

        function resetIncludeTree() {
            renderIncludeTree();
        }

        function renderTimeline() {
            const container = d3.select('#timeline-container');
            container.selectAll('*').remove();

            if (!analysisData.files || !analysisData.files.length) {
                container.html('<div class="loading"><p>No file data available</p></div>');
                return;
            }

            const limit = parseInt(document.getElementById('timeline-limit').value) || 100;
            const sortBy = document.getElementById('timeline-sort').value;

            let files = analysisData.files.slice();

            if (sortBy === 'time') {
                files.sort((a, b) => fileCompileTime(b) - fileCompileTime(a));
            } else {
                files.sort((a, b) => (a.path || '').localeCompare(b.path || ''));
            }

            files = files.slice(0, limit);

            const width = Math.max(640, container.node().getBoundingClientRect().width);
            const barHeight = 20;
            const padding = 2;
            const height = Math.min(files.length * (barHeight + padding) + 60, 2000);

            const svg = container.append('svg')
                .attr('width', width)
                .attr('height', height);

            const maxTime = Math.max(1, d3.max(files, fileCompileTime) || 0);
            const xScale = d3.scaleLinear()
                .domain([0, maxTime])
                .range([200, width - 40]);

            const g = svg.append('g')
                .attr('transform', 'translate(0, 30)');

            // Axis
            g.append('g')
                .attr('transform', `translate(0, -10)`)
                .call(d3.axisTop(xScale).ticks(10).tickFormat(d => d + 'ms'))
                .selectAll('text')
                .style('fill', 'var(--text-secondary)');

            // Bars
            files.forEach((file, i) => {
                const y = i * (barHeight + padding);
                const fileName = file.path.split('/').pop().split('\\\\').pop();
                const totalTime = fileCompileTime(file);
                const frontendTime = Math.min(totalTime, numericValue(file.frontend_time_ms));
                const backendTime = Math.max(0, totalTime - frontendTime);

                // Frontend bar
                g.append('rect')
                    .attr('class', 'timeline-bar')
                    .attr('x', 200)
                    .attr('y', y)
                    .attr('width', Math.max(0, xScale(frontendTime) - 200))
                    .attr('height', barHeight)
                    .attr('fill', 'var(--accent-color)')
                    .on('mouseover', (event) => {
                        showTooltip(event, {
                            name: fileName,
                            fullPath: file.path,
                            time: frontendTime,
                            phase: 'Frontend'
                        });
                    })
                    .on('mouseout', hideTooltip);

                // Backend bar
                g.append('rect')
                    .attr('class', 'timeline-bar')
                    .attr('x', xScale(frontendTime))
                    .attr('y', y)
                    .attr('width', Math.max(0, xScale(totalTime) - xScale(frontendTime)))
                    .attr('height', barHeight)
                    .attr('fill', 'var(--warning-color)')
                    .on('mouseover', (event) => {
                        showTooltip(event, {
                            name: fileName,
                            fullPath: file.path,
                            time: backendTime,
                            phase: 'Backend'
                        });
                    })
                    .on('mouseout', hideTooltip);

                // Label
                g.append('text')
                    .attr('x', 195)
                    .attr('y', y + barHeight / 2)
                    .attr('dy', '0.35em')
                    .attr('text-anchor', 'end')
                    .style('font-size', '11px')
                    .style('fill', 'var(--text-primary)')
                    .text(fileName.length > 25 ? fileName.substring(0, 22) + '...' : fileName);
            });
        }

        function renderTreemap() {
            const container = d3.select('#treemap-container');
            container.selectAll('*').remove();

            if (!analysisData.files || !analysisData.files.length) {
                container.html('<div class="loading"><p>No file data available</p></div>');
                return;
            }

            const limit = parseInt(document.getElementById('treemap-limit').value) || 100;

            const width = Math.max(640, container.node().getBoundingClientRect().width);
            const height = 600;

            const svg = container.append('svg')
                .attr('width', width)
                .attr('height', height);

            // Get top N files by time
            let files = analysisData.files.slice()
                .sort((a, b) => fileCompileTime(b) - fileCompileTime(a))
                .slice(0, limit);

            // Prepare data
            const root = {
                name: 'root',
                children: files.map(f => ({
                    name: f.path.split('/').pop().split('\\\\').pop(),
                    fullPath: f.path,
                    value: fileCompileTime(f),
                    time: fileCompileTime(f)
                }))
            };

            const hierarchy = d3.hierarchy(root)
                .sum(d => d.value)
                .sort((a, b) => b.value - a.value);

            const treemap = d3.treemap()
                .size([width, height])
                .padding(2)
                .round(true);

            treemap(hierarchy);

            const maxTime = Math.max(1, d3.max(files, fileCompileTime) || 0);
            const colorScale = d3.scaleSequential(d3.interpolateRgb('#172638', '#f0b35b'))
                .domain([0, maxTime]);

            const cell = svg.selectAll('g')
                .data(hierarchy.leaves())
                .join('g')
                .attr('transform', d => `translate(${d.x0},${d.y0})`);

            cell.append('rect')
                .attr('class', 'treemap-cell')
                .attr('width', d => d.x1 - d.x0)
                .attr('height', d => d.y1 - d.y0)
                .attr('fill', d => colorScale(d.data.time))
                .on('mouseover', (event, d) => {
                    showTooltip(event, {
                        name: d.data.name,
                        fullPath: d.data.fullPath,
                        time: d.data.time,
                        lines: d.data.lines
                    });
                })
                .on('mouseout', hideTooltip);

            cell.append('text')
                .attr('class', 'treemap-label')
                .attr('x', 4)
                .attr('y', 16)
                .text(d => {
                    const width = d.x1 - d.x0;
                    if (width < 60) return '';
                    const name = d.data.name;
                    return name.length > width / 7 ? name.substring(0, Math.floor(width / 7)) + '...' : name;
                });
        }

        function renderTemplates() {
            const container = d3.select('#template-container');
            container.selectAll('*').remove();

            if (!analysisData.templates || !analysisData.templates.templates ||
                !analysisData.templates.templates.length) {
                container.html('<div class="loading"><p>No template data available</p></div>');
                return;
            }

            const limit = parseInt(document.getElementById('template-limit').value) || 50;
            const templates = analysisData.templates.templates
                .slice()
                .sort((a, b) => templateCompileTime(b) - templateCompileTime(a))
                .slice(0, limit);
            const width = Math.max(720, container.node().getBoundingClientRect().width);
            const margin = {top: 20, right: 100, bottom: 20, left: 300};
            const rowHeight = 32;
            const height = Math.max(280, templates.length * rowHeight + margin.top + margin.bottom);
            const maxTime = Math.max(1, d3.max(templates, templateCompileTime) || 0);
            const svg = container.append('svg')
                .attr('width', width)
                .attr('height', height)
                .attr('viewBox', `0 0 ${width} ${height}`)
                .attr('role', 'img')
                .attr('aria-label', 'Template instantiation time ranking');
            const x = d3.scaleLinear().domain([0, maxTime]).range([margin.left, width - margin.right]);
            const rows = svg.append('g').attr('class', 'template-ranking');

            rows.selectAll('.template-row')
                .data(templates)
                .join('g')
                .attr('class', 'template-row')
                .attr('transform', (_, index) => `translate(0, ${margin.top + index * rowHeight})`)
                .on('mouseover', (event, template) => showTooltip(event, {
                    name: templateLabel(template),
                    time: templateCompileTime(template),
                    count: template.instantiation_count,
                    percentage: template.time_percent
                }))
                .on('mouseout', hideTooltip)
                .call(row => {
                    row.append('text')
                        .attr('class', 'template-label')
                        .attr('x', margin.left - 12)
                        .attr('y', rowHeight / 2)
                        .attr('text-anchor', 'end')
                        .attr('dy', '0.35em')
                        .text(template => truncateLabel(templateLabel(template), 52));
                    row.append('rect')
                        .attr('class', 'template-bar-track')
                        .attr('x', margin.left)
                        .attr('y', 5)
                        .attr('width', Math.max(0, width - margin.left - margin.right))
                        .attr('height', rowHeight - 10);
                    row.append('rect')
                        .attr('class', 'template-bar')
                        .attr('x', margin.left)
                        .attr('y', 5)
                        .attr('width', template => Math.max(2, x(templateCompileTime(template)) - margin.left))
                        .attr('height', rowHeight - 10);
                    row.append('text')
                        .attr('class', 'template-value')
                        .attr('x', template => x(templateCompileTime(template)) + 8)
                        .attr('y', rowHeight / 2)
                        .attr('dy', '0.35em')
                        .text(template => `${templateCompileTime(template).toFixed(1)} ms`);
                });
        }

        function renderDependencyGraph() {
            const container = d3.select('#dependency-graph-container');
            const svg = d3.select('#dependency-graph');
            if (graphSimulation) {
                graphSimulation.stop();
                graphSimulation = null;
            }
            svg.selectAll('*').remove();

            if (!analysisData.dependencies || !analysisData.dependencies.graph) {
                svg.append('text')
                   .attr('x', 20).attr('y', 30)
                   .text('No dependency graph data available')
                   .style('fill', 'var(--text-secondary)');
                return;
            }

            const limit = parseInt(document.getElementById('dep-limit').value) || 50;

            const graph = analysisData.dependencies.graph;

            if (!graph.nodes || !graph.nodes.length) {
                svg.append('text')
                   .attr('x', 20).attr('y', 30)
                   .text('No nodes in dependency graph')
                   .style('fill', 'var(--text-secondary)');
                return;
            }

            // Calculate node importance (number of connections)
            const nodeConnections = new Map();
            graph.nodes.forEach(n => nodeConnections.set(n.id, 0));
            (graph.links || []).forEach(l => {
                const sourceId = typeof l.source === 'object' ? l.source.id : l.source;
                const targetId = typeof l.target === 'object' ? l.target.id : l.target;
                nodeConnections.set(sourceId, (nodeConnections.get(sourceId) || 0) + 1);
                nodeConnections.set(targetId, (nodeConnections.get(targetId) || 0) + 1);
            });

            // Separate sources and headers
            const sourceNodes = graph.nodes.filter(n => n.type === 'source');
            const headerNodes = graph.nodes.filter(n => n.type === 'header');

            // Get top nodes from each category to ensure variety
            const halfLimit = Math.floor(limit / 2);
            const topSources = sourceNodes
                .slice()
                .sort((a, b) => (nodeConnections.get(b.id) || 0) - (nodeConnections.get(a.id) || 0))
                .slice(0, Math.min(halfLimit, sourceNodes.length));

            const topHeaders = headerNodes
                .slice()
                .sort((a, b) => (nodeConnections.get(b.id) || 0) - (nodeConnections.get(a.id) || 0))
                .slice(0, Math.min(limit - topSources.length, headerNodes.length));

            const topNodes = [...topSources, ...topHeaders];
            const topNodeIds = new Set(topNodes.map(n => n.id));

            let nodes = topNodes.map(d => ({...d}));
            let links = (graph.links || [])
                .filter(l => {
                    const sourceId = typeof l.source === 'object' ? l.source.id : l.source;
                    const targetId = typeof l.target === 'object' ? l.target.id : l.target;
                    return topNodeIds.has(sourceId) && topNodeIds.has(targetId);
                })
                .map(d => ({
                    source: typeof d.source === 'object' ? d.source.id : d.source,
                    target: typeof d.target === 'object' ? d.target.id : d.target,
                    type: d.type
                }));

            if (!nodes.length) {
                svg.append('text')
                   .attr('x', 20).attr('y', 30)
                   .text('No nodes to display')
                   .style('fill', 'var(--text-secondary)');
                return;
            }

            const width = Math.max(640, container.node().getBoundingClientRect().width);
            const height = 600;

            svg.attr('width', width).attr('height', height);

            const g = svg.append('g');

            graphZoom = d3.zoom()
                .scaleExtent([0.1, 10])
                .on('zoom', (event) => {
                    g.attr('transform', event.transform);
                    currentTransform = event.transform;
                });

            svg.call(graphZoom);

            const simulation = d3.forceSimulation(nodes)
                .force('link', d3.forceLink(links)
                    .id(d => d.id)
                    .distance(100)
                    .strength(0.5))
                .force('charge', d3.forceManyBody()
                    .strength(-300)
                    .distanceMax(400))
                .force('center', d3.forceCenter(width / 2, height / 2))
                .force('collision', d3.forceCollide().radius(30));

            graphSimulation = simulation;

            const defs = svg.append('defs');

            defs.append('marker')
                .attr('id', 'arrowhead')
                .attr('viewBox', '0 -5 10 10')
                .attr('refX', 20)
                .attr('refY', 0)
                .attr('markerWidth', 6)
                .attr('markerHeight', 6)
                .attr('orient', 'auto')
                .append('path')
                .attr('d', 'M0,-5L10,0L0,5')
                .attr('fill', 'var(--border-color)');

            const link = g.append('g')
                .selectAll('path')
                .data(links)
                .join('path')
                .attr('class', 'link')
                .attr('stroke', 'var(--border-color)')
                .attr('stroke-width', 1.5)
                .attr('fill', 'none')
                .attr('marker-end', 'url(#arrowhead)')
                .on('mouseover', function() {
                    d3.select(this)
                        .attr('stroke', 'var(--accent-color)')
                        .attr('stroke-width', 2.5);
                })
                .on('mouseout', function() {
                    d3.select(this)
                        .attr('stroke', 'var(--border-color)')
                        .attr('stroke-width', 1.5);
                });

            const node = g.append('g')
                .selectAll('g')
                .data(nodes)
                .join('g')
                .attr('class', 'node')
                .call(d3.drag()
                    .on('start', dragstarted)
                    .on('drag', dragged)
                    .on('end', dragended));

            node.append('circle')
                .attr('r', d => d.type === 'source' ? 10 : 7)
                .attr('fill', d => d.type === 'source' ? 'var(--success-color)' : 'var(--accent-color)')
                .attr('stroke', '#fff')
                .attr('stroke-width', 2)
                .on('mouseover', function(event, d) {
                    showTooltip(event, {
                        name: d.id.split('/').pop().split('\\\\').pop(),
                        fullPath: d.id,
                        type: d.type,
                        connections: nodeConnections.get(d.id) || 0
                    });
                    d3.select(this)
                        .attr('r', d.type === 'source' ? 14 : 11)
                        .style('filter', 'brightness(1.5)');
                })
                .on('mouseout', function(event, d) {
                    hideTooltip();
                    d3.select(this)
                        .attr('r', d.type === 'source' ? 10 : 7)
                        .style('filter', 'brightness(1)');
                });

            node.append('text')
                .text(d => {
                    const name = d.id.split('/').pop().split('\\\\').pop();
                    return name.length > 20 ? name.substring(0, 17) + '...' : name;
                })
                .attr('x', 12)
                .attr('y', 4)
                .style('font-size', '10px')
                .style('fill', 'var(--text-primary)')
                .style('pointer-events', 'none');

            simulation.on('tick', () => {
                link.attr('d', d => {
                    const dx = d.target.x - d.source.x;
                    const dy = d.target.y - d.source.y;
                    const dr = Math.sqrt(dx * dx + dy * dy);
                    return `M${d.source.x},${d.source.y}A${dr},${dr} 0 0,1 ${d.target.x},${d.target.y}`;
                });

                node.attr('transform', d => `translate(${d.x},${d.y})`);
            });

            function dragstarted(event, d) {
                if (!event.active) simulation.alphaTarget(0.3).restart();
                d.fx = d.x;
                d.fy = d.y;
            }

            function dragged(event, d) {
                d.fx = event.x;
                d.fy = event.y;
            }

            function dragended(event, d) {
                if (!event.active) simulation.alphaTarget(0);
                d.fx = null;
                d.fy = null;
            }

            svg.call(graphZoom.transform, d3.zoomIdentity.translate(width / 2, height / 2).scale(0.8).translate(-width / 2, -height / 2));
        }

        function resetZoom() {
            const svg = d3.select('#dependency-graph');
            const container = d3.select('#dependency-graph-container');
            const width = container.node().getBoundingClientRect().width;
            const height = 600;

            if (!graphZoom) return;
            svg.transition()
                .duration(300)
                .call(graphZoom.transform,
                    d3.zoomIdentity.translate(width / 2, height / 2).scale(0.8).translate(-width / 2, -height / 2));
        }

        // ==================== TOOLTIP HELPER ====================
        let tooltip = null;

        function showTooltip(event, data) {
            if (!tooltip) {
                tooltip = d3.select('body').append('div')
                    .attr('class', 'tooltip')
                    .style('opacity', 0);
            }

            let html = '';
            if (data.name) {
                html = `<strong>${escapeHtml(data.name)}</strong><br/>`;
            }
            if (data.fullPath) {
                html += `Path: ${escapeHtml(data.fullPath)}<br/>`;
            }
            if (Number.isFinite(Number(data.time))) {
                html += `Time: ${Number(data.time).toFixed(1)} ms<br/>`;
            }
            if (data.phase) {
                html += `Phase: ${escapeHtml(data.phase)}<br/>`;
            }
            if (Number.isFinite(Number(data.lines))) {
                html += `Lines: ${Number(data.lines).toLocaleString()}<br/>`;
            }
            if (Number.isFinite(Number(data.count))) {
                html += `Instantiations: ${Number(data.count).toLocaleString()}<br/>`;
            }
            if (Number.isFinite(Number(data.percentage))) {
                html += `Percentage: ${Number(data.percentage).toFixed(1)}%<br/>`;
            }
            if (data.type) {
                html += `Type: ${escapeHtml(data.type)}<br/>`;
            }
            if (Number.isFinite(Number(data.connections))) {
                html += `Connections: ${Number(data.connections).toLocaleString()}<br/>`;
            }
            if (Number.isFinite(Number(data.depth))) {
                html += `Depth: ${Number(data.depth).toLocaleString()}<br/>`;
            }
            if (Number.isFinite(Number(data.children)) && data.children > 0) {
                html += `Children: ${Number(data.children).toLocaleString()}<br/>`;
            }
            if (data.stack !== undefined) {
                html += `Stack Usage: ${formatBytes(data.stack)}`;
            }

            tooltip.html(html)
                .style('left', (event.pageX + 10) + 'px')
                .style('top', (event.pageY - 10) + 'px')
                .transition()
                .duration(200)
                .style('opacity', 1);
        }

        function hideTooltip() {
            if (tooltip) {
                tooltip.transition()
                    .duration(200)
                    .style('opacity', 0);
            }
        }

        function renderMemoryChart() {
            const container = document.getElementById('memory-chart-container');
            if (!container) return;

            container.innerHTML = '';

            if (!analysisData.files || !analysisData.files.length) {
                container.innerHTML = '<div class="empty-state">No file data available.</div>';
                updateMemoryStats([]);
                return;
            }

            const sortBy = document.getElementById('memory-sort')?.value || 'stack';
            const limit = parseInt(document.getElementById('memory-limit')?.value) || 50;

            const filesWithMemory = analysisData.files
                .filter(f => f.memory && f.memory.max_stack_bytes > 0)
                .map(f => ({
                    file: f.path,
                    stack: f.memory.max_stack_bytes || 0
                }));

            if (filesWithMemory.length === 0) {
                container.innerHTML = '<div class="empty-state">No stack usage data available in this trace.</div>';
                updateMemoryStats([]);
                return;
            }

            filesWithMemory.sort((a, b) => {
                if (sortBy === 'stack') return b.stack - a.stack;
                return a.file.localeCompare(b.file);
            });

            const topFiles = filesWithMemory.slice(0, limit);
            updateMemoryStats(filesWithMemory);
            updateMemoryTable(topFiles);

            const margin = {top: 20, right: 40, bottom: 150, left: 80};
            const width = Math.max(420, container.clientWidth - margin.left - margin.right);
            const height = 500 - margin.top - margin.bottom;

            const svg = d3.select(container)
                .append('svg')
                .attr('width', width + margin.left + margin.right)
                .attr('height', height + margin.top + margin.bottom)
                .append('g')
                .attr('transform', `translate(${margin.left},${margin.top})`);

            const x = d3.scaleBand()
                .domain(topFiles.map(d => d.file))
                .range([0, width])
                .padding(0.2);

            const maxValue = Math.max(1, d3.max(topFiles, d => d.stack) || 0);
            const y = d3.scaleLinear()
                .domain([0, maxValue * 1.1])
                .range([height, 0]);

            svg.append('g')
                .attr('transform', `translate(0,${height})`)
                .call(d3.axisBottom(x))
                .selectAll('text')
                .attr('transform', 'rotate(-45)')
                .style('text-anchor', 'end')
                .style('font-size', '10px')
                .text(d => d.length > 30 ? d.substring(0, 27) + '...' : d);

            svg.append('g')
                .call(d3.axisLeft(y).ticks(10).tickFormat(d => formatBytes(d)));

            svg.selectAll('.bar-stack')
                .data(topFiles)
                .enter()
                .append('rect')
                .attr('class', 'bar-stack')
                .attr('x', d => x(d.file))
                .attr('y', d => y(d.stack))
                .attr('width', x.bandwidth())
                .attr('height', d => height - y(d.stack))
                .attr('fill', 'var(--accent-color)')
                .style('opacity', 0.8)
                .on('mouseover', (event, d) => {
                    showTooltip(event, {
                        name: d.file.split('/').pop().split('\\\\').pop(),
                        fullPath: d.file,
                        stack: d.stack
                    });
                })
                .on('mouseout', hideTooltip);
        }

        function updateMemoryStats(filesWithMemory) {
            const totalStack = filesWithMemory.reduce((sum, f) => sum + f.stack, 0);
            const maxStack = filesWithMemory.length > 0 ? Math.max(...filesWithMemory.map(f => f.stack)) : 0;
            const avgStack = filesWithMemory.length > 0 ? totalStack / filesWithMemory.length : 0;

            document.getElementById('max-stack-usage').textContent = formatBytes(maxStack);
            document.getElementById('total-stack-usage').textContent = formatBytes(totalStack);
            document.getElementById('avg-stack-usage').textContent = formatBytes(avgStack);
            document.getElementById('files-with-stack').textContent = filesWithMemory.length;
        }

        function updateMemoryTable(files) {
            const tbody = document.getElementById('memory-table-body');
            if (!tbody) return;

            tbody.innerHTML = files.map(f => `
                <tr>
                    <td><i class="fas fa-file-code table-icon" aria-hidden="true"></i>${escapeHtml(f.file)}</td>
                    <td><strong>${formatBytes(f.stack)}</strong></td>
                </tr>
            `).join('');
        }

        function formatMetric(value, suffix = '', digits = 1) {
            if (value === null || value === undefined || !Number.isFinite(Number(value))) {
                return '<span class="unavailable">unavailable</span>';
            }
            return Number(value).toFixed(digits) + suffix;
        }

        function formatCount(value) {
            if (value === null || value === undefined || !Number.isFinite(Number(value))) {
                return '<span class="unavailable">unavailable</span>';
            }
            return Number(value).toLocaleString();
        }

        function truncateLabel(value, maxLength) {
            const text = String(value || '');
            return text.length > maxLength ? text.substring(0, maxLength - 3) + '...' : text;
        }

        function renderMetricCards(metrics) {
            return `<div class="summary-grid context-metrics">
                ${metrics.map(metric => `<article class="summary-card">
                    <p class="summary-label">${escapeHtml(metric.label)}</p>
                    <div class="value">${metric.value}</div>
                    ${metric.unit ? `<div class="unit">${metric.unit}</div>` : ''}
                </article>`).join('')}
            </div>`;
        }

        function renderBuildContext() {
            const container = document.getElementById('build-context-container');
            if (!container) return;

            const performance = analysisData.performance || {};
            const session = analysisData.build_session || {};
            const telemetry = session.host_telemetry || {};
            const host = session.host_system || {};
            const linker = analysisData.linker || {};
            const targets = analysisData.targets || {};
            const modules = analysisData.modules || {};
            const resources = analysisData.process_resources || {};
            const symbols = analysisData.symbols || {};
            const suggestions = analysisData.suggestions || [];
            const capabilities = (analysisData.summary || {}).metric_capabilities || [];

            let html = renderMetricCards([
                {label: 'Sequential Time', value: formatMetric(performance.sequential_time_ms, ' ms'), unit: 'sum of timed work'},
                {label: 'Parallel Time', value: formatMetric(performance.parallel_time_ms, ' ms'), unit: 'overlap duration'},
                {label: 'Parallelism', value: formatMetric(performance.parallelism_efficiency, 'x'), unit: 'serial / wall time'},
                {label: 'Median File', value: formatMetric(performance.median_file_time_ms, ' ms'), unit: 'translation unit'},
                {label: 'P90 File', value: formatMetric(performance.p90_file_time_ms, ' ms'), unit: 'translation unit'},
                {label: 'P99 File', value: formatMetric(performance.p99_file_time_ms, ' ms'), unit: 'translation unit'}
            ]);

            const criticalPath = Array.isArray(session.critical_path) && session.critical_path.length
                ? session.critical_path.map(escapeHtml).join(' &rarr; ')
                : '<span class="unavailable">unavailable</span>';
            html += `<div class="subsection-heading"><h3><i class="fas fa-clock" aria-hidden="true"></i> Build Session</h3></div>
                <table><thead><tr><th>Metric</th><th>Value</th></tr></thead><tbody>
                    <tr><td>Total commands</td><td>${formatCount(session.total_commands)}</td></tr>
                    <tr><td>Timed commands</td><td>${formatCount(session.timed_commands)}</td></tr>
                    <tr><td>Wall-clock time</td><td>${formatMetric(session.wall_clock_time_ms, ' ms')}</td></tr>
                    <tr><td>Serial time</td><td>${formatMetric(session.serial_time_ms, ' ms')}</td></tr>
                    <tr><td>Peak parallelism</td><td>${formatCount(session.peak_parallelism)}</td></tr>
                    <tr><td>Average parallelism</td><td>${formatMetric(session.average_parallelism, 'x')}</td></tr>
                    <tr><td>Critical path</td><td>${formatMetric(session.critical_path_time_ms, ' ms')}<br>${criticalPath}</td></tr>
                    <tr><td>Compile trace references</td><td>${formatCount(session.compile_trace_references)}</td></tr>
                </tbody></table>`;

            const steps = Array.isArray(session.step_metrics) ? session.step_metrics : [];
            html += `<div class="subsection-heading"><h3><i class="fas fa-list-ol" aria-hidden="true"></i> Build steps</h3></div>`;
            html += steps.length
                ? `<div class="table-wrap table-wrap-compact"><table><thead><tr><th>Role</th><th>Commands</th><th>Timed</th><th>Wall Time</th><th>Results</th><th>Failures</th></tr></thead><tbody>${steps.map(step => `<tr>
                    <td>${escapeHtml(step.role || 'unknown')}</td>
                    <td>${formatCount(step.total_commands)}</td>
                    <td>${formatCount(step.timed_commands)}</td>
                    <td>${formatMetric(step.wall_clock_time_ms, ' ms')}</td>
                    <td>${formatCount(step.result_observations)}</td>
                    <td>${formatCount(step.failed_commands)}</td>
                </tr>`).join('')}</tbody></table></div>`
                : '<div class="info-badge">No build-step metrics were supplied.</div>';

            html += `<div class="subsection-heading"><h3><i class="fas fa-server" aria-hidden="true"></i> Host and process resources</h3></div>
                <table><thead><tr><th>Domain</th><th>Metric</th><th>Value</th></tr></thead><tbody>
                    <tr><td>Host</td><td>Operating system</td><td>${escapeHtml(host.os_name || 'unavailable')}</td></tr>
                    <tr><td>Host</td><td>Logical CPUs</td><td>${formatCount(host.logical_cpu_count)}</td></tr>
                    <tr><td>Host</td><td>Physical memory</td><td>${formatMetric(host.total_physical_memory_mib, ' MiB')}</td></tr>
                    <tr><td>Telemetry</td><td>Peak memory used</td><td>${formatMetric(telemetry.peak_memory_used_kib, ' KiB')}</td></tr>
                    <tr><td>Telemetry</td><td>Peak CPU load before</td><td>${formatMetric(telemetry.peak_before_cpu_load_average)}</td></tr>
                    <tr><td>Process</td><td>Observations</td><td>${formatCount(resources.observations)}</td></tr>
                    <tr><td>Process</td><td>Total process time</td><td>${formatMetric(resources.total_process_time_ms, ' ms')}</td></tr>
                    <tr><td>Process</td><td>Peak memory</td><td>${formatCount(resources.peak_memory_kib)} KiB</td></tr>
                </tbody></table>`;

            html += `<div class="subsection-heading"><h3><i class="fas fa-link" aria-hidden="true"></i> Linker, targets, and modules</h3></div>
                <table><thead><tr><th>Domain</th><th>Metric</th><th>Value</th></tr></thead><tbody>
                    <tr><td>Linker</td><td>Invocations</td><td>${formatCount(linker.invocations)}</td></tr>
                    <tr><td>Linker</td><td>Wall-clock time</td><td>${formatMetric(linker.wall_clock_time_ms, ' ms')}</td></tr>
                    <tr><td>Linker</td><td>Output size</td><td>${formatCount(linker.output_bytes)} bytes</td></tr>
                    <tr><td>Targets</td><td>Matched commands</td><td>${formatCount(targets.matched_commands)} / ${formatCount(targets.target_commands)}</td></tr>
                    <tr><td>Targets</td><td>PCH headers</td><td>${formatCount(targets.pch_headers)}</td></tr>
                    <tr><td>Modules</td><td>Resolved dependencies</td><td>${formatCount(modules.resolved_dependencies)}</td></tr>
                    <tr><td>Modules</td><td>Unresolved dependencies</td><td>${formatCount(modules.unresolved_dependencies)}</td></tr>
                </tbody></table>`;

            const targetRows = Array.isArray(targets.targets) ? targets.targets : [];
            if (targetRows.length) {
                html += `<h4 class="context-subheading">Target ownership</h4><div class="table-wrap table-wrap-compact"><table><thead><tr><th>Name</th><th>Type</th><th>Compile Commands</th><th>Compile Time</th><th>Output</th></tr></thead><tbody>${targetRows.map(target => `<tr>
                    <td>${escapeHtml(target.name || target.id || 'unnamed')}</td>
                    <td>${escapeHtml(target.type || 'unknown')}</td>
                    <td>${formatCount(target.compile_commands)}</td>
                    <td>${formatMetric(target.compile_wall_clock_time_ms, ' ms')}</td>
                    <td>${formatCount(target.output_bytes)} bytes</td>
                </tr>`).join('')}</tbody></table></div>`;
            }

            const symbolRows = Array.isArray(symbols.symbols) ? symbols.symbols.slice(0, 100) : [];
            html += `<div class="subsection-heading"><h3><i class="fas fa-shapes" aria-hidden="true"></i> Symbols</h3></div>`;
            html += symbolRows.length
                ? `<div class="table-wrap table-wrap-compact"><table><thead><tr><th>Symbol</th><th>Type</th><th>Defined In</th><th>Usages</th></tr></thead><tbody>${symbolRows.map(symbol => `<tr>
                    <td>${escapeHtml(symbol.name || 'unnamed')}</td>
                    <td>${escapeHtml(symbol.type || 'unavailable')}</td>
                    <td>${escapeHtml(symbol.defined_in || 'unavailable')}</td>
                    <td>${formatCount(symbol.usage_count)}</td>
                </tr>`).join('')}</tbody></table></div>`
                : '<div class="info-badge">No symbol records were supplied.</div>';

            html += `<div class="subsection-heading"><h3><i class="fas fa-shield-alt" aria-hidden="true"></i> Metric Evidence</h3></div>`;
            html += capabilities.length
                ? `<div class="table-wrap table-wrap-compact"><table><thead><tr><th>Metric</th><th>Evidence</th><th>Producer</th><th>Scope</th><th>Limitation</th></tr></thead><tbody>${capabilities.map(capability => `<tr>
                    <td>${escapeHtml(capability.metric || 'unnamed')}</td>
                    <td>${escapeHtml(capability.evidence || 'unavailable')}</td>
                    <td>${escapeHtml(capability.producer || 'unavailable')}</td>
                    <td>${escapeHtml(capability.scope || 'unavailable')}</td>
                    <td>${escapeHtml(capability.limitation || 'none recorded')}</td>
                </tr>`).join('')}</tbody></table></div>`
                : '<div class="info-badge">No metric capability records were supplied.</div>';

            html += `<div class="subsection-heading"><h3><i class="fas fa-lightbulb" aria-hidden="true"></i> Suggestion Evidence</h3></div>`;
            html += suggestions.length
                ? `<div class="suggestion-list">${suggestions.map(suggestion => `<details class="suggestion-card">
                    <summary><strong>${escapeHtml(suggestion.title || suggestion.id || 'Suggestion')}</strong> <span class="meta-pill">${escapeHtml(suggestion.type || 'unknown')}</span></summary>
                    <div class="suggestion-description">${escapeHtml(suggestion.description || 'No description supplied.')}</div>
                    <table><tbody>
                        <tr><td>Priority</td><td>${escapeHtml(suggestion.priority || 'unavailable')}</td></tr>
                        <tr><td>Confidence</td><td>${formatMetric(Number(suggestion.confidence) * 100, '%')}</td></tr>
                        <tr><td>Estimated savings</td><td>${formatMetric(suggestion.estimated_savings_ms, ' ms')} (${escapeHtml(suggestion.estimated_savings_evidence || 'unavailable')})</td></tr>
                        <tr><td>Target</td><td>${escapeHtml((suggestion.target_file || {}).path || 'unavailable')}</td></tr>
                        <tr><td>Application</td><td>${escapeHtml(suggestion.application_mode || 'advisory')}</td></tr>
                        <tr><td>Edits</td><td>${formatCount((suggestion.edits || []).length)}</td></tr>
                    </tbody></table>
                </details>`).join('')}</div>`
                : '<div class="info-badge">Suggestions were not requested for this report.</div>';

            container.innerHTML = html;
        }

        function formatBytes(bytes) {
            if (!Number.isFinite(Number(bytes)) || Number(bytes) <= 0) return '-';
            bytes = Number(bytes);
            if (bytes < 1024) return Number(bytes).toFixed(2) + ' B';
            if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(2) + ' KB';
            return (bytes / (1024 * 1024)).toFixed(2) + ' MB';
        }

        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }

        window.showTab = showTab;
        window.filterFiles = filterFiles;
        window.sortFiles = sortFiles;
        window.applyFileLimit = applyFileLimit;
        window.resetZoom = resetZoom;
        window.resetIncludeTree = resetIncludeTree;
        window.renderIncludeTree = renderIncludeTree;
        window.renderTimeline = renderTimeline;
        window.renderTreemap = renderTreemap;
        window.renderTemplates = renderTemplates;
        window.renderDependencyGraph = renderDependencyGraph;
        window.renderMemoryChart = renderMemoryChart;
        window.renderBuildContext = renderBuildContext;
