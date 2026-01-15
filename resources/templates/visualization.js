        let currentTransform = d3.zoomIdentity;
        let graphSimulation = null;
        let allFilesData = [];

        // Initialize
        document.addEventListener('DOMContentLoaded', function() {
            allFilesData = (analysisData.files || []).slice();
            updateFileStats();
            applyFileLimit();
        });

        function showTab(tabId) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
            document.querySelector('.tab[onclick*="' + tabId + '"]').classList.add('active');
            document.getElementById(tabId).classList.add('active');

            if (tabId === 'include-tree') renderIncludeTree();
            if (tabId === 'timeline') renderTimeline();
            if (tabId === 'treemap') renderTreemap();
            if (tabId === 'templates') renderTemplates();
            if (tabId === 'dependencies') renderDependencyGraph();
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
                    case 'lines-desc':
                        return parseInt(b.dataset.lines) - parseInt(a.dataset.lines);
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

            const width = container.node().getBoundingClientRect().width;
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

            const allNodeMap = new Map(allNodes.map(n => [n.id, {...n, children: []}]));

            // Build parent-child relationships
            const hasIncoming = new Set();
            allLinks.forEach(link => {
                const sourceId = typeof link.source === 'object' ? link.source.id : link.source;
                const targetId = typeof link.target === 'object' ? link.target.id : link.target;

                if (allNodeMap.has(sourceId) && allNodeMap.has(targetId)) {
                    hasIncoming.add(targetId);
                    const source = allNodeMap.get(sourceId);
                    const target = allNodeMap.get(targetId);
                    if (source && target && !source.children.find(c => c.id === target.id)) {
                        source.children.push(target);
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

            function countDescendants(node, depth = 0) {
                if (depth >= maxDepth || !node.children || node.children.length === 0) {
                    return 1;
                }
                return 1 + node.children.reduce((sum, child) => sum + countDescendants(child, depth + 1), 0);
            }

            function pruneTree(node, remainingNodes, depth = 0) {
                if (remainingNodes <= 0 || depth >= maxDepth) {
                    node._children = node.children;
                    node.children = null;
                    return 1;
                }

                if (!node.children || node.children.length === 0) {
                    return 1;
                }

                let used = 1;
                const keptChildren = [];

                for (const child of node.children) {
                    if (used >= remainingNodes) {
                        break;
                    }
                    const childCount = pruneTree(child, remainingNodes - used, depth + 1);
                    used += childCount;
                    keptChildren.push(child);
                }

                if (keptChildren.length < node.children.length) {
                    node._children = node.children.slice(keptChildren.length);
                }
                node.children = keptChildren;

                return used;
            }

            // Tree layout for each root
            const tree = d3.tree().size([height - 100, width - 200]);

            let yOffset = 50;
            let nodesPerRoot = Math.floor(maxNodes / roots.length);

            roots.forEach((root, idx) => {
                const rootNode = allNodeMap.get(root.id);
                if (!rootNode) return;

                // Prune to fit within maxNodes budget
                pruneTree(rootNode, nodesPerRoot, 0);

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
                        if (d._children) {
                            d.children = d._children;
                            d._children = null;
                        } else if (d.children) {
                            d._children = d.children;
                            d.children = null;
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
                files.sort((a,b) => b.total_time_ms - a.total_time_ms);
            } else {
                files.sort((a,b) => a.path.localeCompare(b.path));
            }

            files = files.slice(0, limit);

            const width = container.node().getBoundingClientRect().width;
            const barHeight = 20;
            const padding = 2;
            const height = Math.min(files.length * (barHeight + padding) + 60, 2000);

            const svg = container.append('svg')
                .attr('width', width)
                .attr('height', height);

            const maxTime = d3.max(files, d => d.total_time_ms);
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

                // Frontend bar
                g.append('rect')
                    .attr('class', 'timeline-bar')
                    .attr('x', 200)
                    .attr('y', y)
                    .attr('width', Math.max(1, xScale(file.frontend_time_ms) - 200))
                    .attr('height', barHeight)
                    .attr('fill', 'var(--accent-color)')
                    .on('mouseover', (event) => {
                        showTooltip(event, {
                            name: fileName,
                            fullPath: file.path,
                            time: file.frontend_time_ms,
                            phase: 'Frontend'
                        });
                    })
                    .on('mouseout', hideTooltip);

                // Backend bar
                g.append('rect')
                    .attr('class', 'timeline-bar')
                    .attr('x', xScale(file.frontend_time_ms))
                    .attr('y', y)
                    .attr('width', Math.max(1, xScale(file.total_time_ms) - xScale(file.frontend_time_ms)))
                    .attr('height', barHeight)
                    .attr('fill', 'var(--warning-color)')
                    .on('mouseover', (event) => {
                        showTooltip(event, {
                            name: fileName,
                            fullPath: file.path,
                            time: file.backend_time_ms,
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
            const metric = document.getElementById('treemap-metric').value;

            const width = container.node().getBoundingClientRect().width;
            const height = 600;

            const svg = container.append('svg')
                .attr('width', width)
                .attr('height', height);

            // Get top N files by time
            let files = analysisData.files.slice()
                .sort((a, b) => b.total_time_ms - a.total_time_ms)
                .slice(0, limit);

            // Prepare data
            const root = {
                name: 'root',
                children: files.map(f => ({
                    name: f.path.split('/').pop().split('\\\\').pop(),
                    fullPath: f.path,
                    value: metric === 'lines' ? (f.lines_of_code || 1) : f.total_time_ms,
                    time: f.total_time_ms,
                    lines: f.lines_of_code
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

            const maxTime = d3.max(files, f => f.total_time_ms);
            const colorScale = d3.scaleSequential(d3.interpolateRdYlGn)
                .domain([maxTime, 0]);

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

            const width = container.node().getBoundingClientRect().width;
            const height = 600;
            const radius = Math.min(width, height) / 2 - 40;

            const svg = container.append('svg')
                .attr('width', width)
                .attr('height', height);

            const g = svg.append('g')
                .attr('transform', `translate(${width / 2},${height / 2})`);

            // Get top N templates
            const templates = analysisData.templates.templates
                .slice()
                .sort((a, b) => b.time_ms - a.time_ms)
                .slice(0, limit);

            const root = {
                name: 'Templates',
                children: templates.map(t => ({
                    name: t.name.length > 40 ? t.name.substring(0, 37) + '...' : t.name,
                    fullName: t.name,
                    value: t.time_ms,
                    count: t.count,
                    percentage: t.time_percent
                }))
            };

            const hierarchy = d3.hierarchy(root)
                .sum(d => d.value)
                .sort((a, b) => b.value - a.value);

            const partition = d3.partition()
                .size([2 * Math.PI, radius]);

            partition(hierarchy);

            const arc = d3.arc()
                .startAngle(d => d.x0)
                .endAngle(d => d.x1)
                .innerRadius(d => d.y0)
                .outerRadius(d => d.y1);

            const color = d3.scaleOrdinal(d3.schemeCategory10);

            g.selectAll('path')
                .data(hierarchy.descendants().filter(d => d.depth > 0))
                .join('path')
                .attr('d', arc)
                .attr('fill', d => color(d.data.name))
                .attr('stroke', 'var(--bg-primary)')
                .attr('stroke-width', 2)
                .style('cursor', 'pointer')
                .style('opacity', 0.8)
                .on('mouseover', function(event, d) {
                    d3.select(this).style('opacity', 1);
                    showTooltip(event, {
                        name: d.data.fullName || d.data.name,
                        time: d.data.value,
                        count: d.data.count,
                        percentage: d.data.percentage
                    });
                })
                .on('mouseout', function() {
                    d3.select(this).style('opacity', 0.8);
                    hideTooltip();
                });

            // Center label
            g.append('text')
                .attr('text-anchor', 'middle')
                .attr('dy', '0.35em')
                .style('font-size', '16px')
                .style('font-weight', 'bold')
                .style('fill', 'var(--text-primary)')
                .text(`Top ${templates.length} Templates`);
        }

        function renderDependencyGraph() {
            const container = d3.select('#dependency-graph-container');
            const svg = d3.select('#dependency-graph');
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

            const width = container.node().getBoundingClientRect().width;
            const height = 600;

            svg.attr('width', width).attr('height', height);

            const g = svg.append('g');

            const zoom = d3.zoom()
                .scaleExtent([0.1, 10])
                .on('zoom', (event) => {
                    g.attr('transform', event.transform);
                    currentTransform = event.transform;
                });

            svg.call(zoom);

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

            svg.call(zoom.transform, d3.zoomIdentity.translate(width / 2, height / 2).scale(0.8).translate(-width / 2, -height / 2));
        }

        function resetZoom() {
            const svg = d3.select('#dependency-graph');
            const container = d3.select('#dependency-graph-container');
            const width = container.node().getBoundingClientRect().width;
            const height = 600;

            svg.transition()
                .duration(750)
                .call(d3.zoom().transform,
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
                html = `<strong>${data.name}</strong><br/>`;
            }
            if (data.fullPath) {
                html += `Path: ${data.fullPath}<br/>`;
            }
            if (data.time !== undefined) {
                html += `Time: ${data.time.toFixed(1)} ms<br/>`;
            }
            if (data.phase) {
                html += `Phase: ${data.phase}<br/>`;
            }
            if (data.lines !== undefined) {
                html += `Lines: ${data.lines}<br/>`;
            }
            if (data.count !== undefined) {
                html += `Instantiations: ${data.count}<br/>`;
            }
            if (data.percentage !== undefined) {
                html += `Percentage: ${data.percentage.toFixed(1)}%<br/>`;
            }
            if (data.type) {
                html += `Type: ${data.type}<br/>`;
            }
            if (data.connections !== undefined) {
                html += `Connections: ${data.connections}<br/>`;
            }
            if (data.depth !== undefined) {
                html += `Depth: ${data.depth}<br/>`;
            }
            if (data.children !== undefined && data.children > 0) {
                html += `Children: ${data.children}`;
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
