'use strict';

const $ = (id) => document.getElementById(id);
const TAU = Math.PI * 2;
const PALETTE = [
  '#5eead4', '#f3b562', '#a78bfa', '#60a5fa', '#4ade80',
  '#f472b6', '#fb923c', '#38bdf8', '#c084fc', '#facc15',
];

const state = {
  meta: null,
  commits: [],
  index: 0,
  graph: null,
  cache: new Map(),
  positions: new Map(),
  cam: { x: 0, y: 0, z: 1 },
  width: 0,
  height: 0,
  dpr: 1,
  ctx: null,
  selected: null,
  hover: null,
  highlight: null,
  drag: null,
  playing: false,
  timer: null,
  token: 0,
  layerColors: new Map(),
  cycleEdges: new Set(),
  appliedAt: 0,
  toastTimer: null,
  needsFit: true,
};

/* ---------- small utilities ---------- */

const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

function hash(value) {
  let h = 2166136261 >>> 0;
  for (let i = 0; i < value.length; i++) {
    h ^= value.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return h >>> 0;
}

function rand(seed) {
  let t = (seed + 0x6d2b79f5) >>> 0;
  t = Math.imul(t ^ (t >>> 15), t | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}

function esc(value) {
  return String(value).replace(/[&<>"']/g, (c) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  }[c]));
}

function fmt(value) {
  if (value >= 100000) return Math.round(value / 1000) + 'k';
  if (value >= 1000) return (value / 1000).toFixed(1).replace(/\.0$/, '') + 'k';
  return String(value);
}

function date(seconds) {
  if (!seconds) return '—';
  return new Date(seconds * 1000).toISOString().slice(0, 10);
}

function pos(id) {
  return state.positions.get(id);
}

function radius(node) {
  return clamp(5 + Math.sqrt(Math.max(0, node.loc || 0)) * 0.38, 6, 21);
}

function radiusOfId(id) {
  const node = state.nodeIndex && state.nodeIndex.get(id);
  return node ? radius(node) : 8;
}

function colorFor(node) {
  if (node && node.layer && state.layerColors.has(node.layer)) {
    return state.layerColors.get(node.layer);
  }
  return PALETTE[hash(node && node.id ? node.id : '') % PALETTE.length];
}

function degreeOf(id) {
  return (state.degree && state.degree.get(id)) || 0;
}

function neighbors(id) {
  const out = new Set([id]);
  if (!state.graph) return out;
  for (const edge of state.graph.edges) {
    if (edge.from === id) out.add(edge.to);
    if (edge.to === id) out.add(edge.from);
  }
  return out;
}

function updateHighlight() {
  if (state.hover && state.hover.type === 'node') {
    state.highlight = neighbors(state.hover.id);
  } else if (state.hover && state.hover.type === 'edge') {
    state.highlight = new Set([state.hover.from, state.hover.to]);
  } else if (state.selected && state.selected.type === 'node') {
    state.highlight = neighbors(state.selected.id);
  } else if (state.selected && state.selected.type === 'edge') {
    state.highlight = new Set([state.selected.from, state.selected.to]);
  } else {
    state.highlight = null;
  }
}

/* ---------- boot ---------- */

async function boot() {
  try {
    const response = await fetch('/api/meta');
    if (!response.ok) throw new Error(await response.text());
    const meta = await response.json();
    state.meta = meta;
    state.commits = meta.commits || [];
    $('repo').textContent = meta.repo.name;
    document.title = 'strata · ' + meta.repo.name;

    for (const [i, layer] of (meta.config.layers || []).entries()) {
      state.layerColors.set(layer.name, PALETTE[i % PALETTE.length]);
    }
    buildLegend(meta.config.layers || []);

    $('timeline').max = String(Math.max(0, state.commits.length - 1));

    let index = state.commits.length - 1;
    const wanted = location.hash.replace('#', '');
    if (wanted) {
      const found = state.commits.findIndex((c) => c.sha.startsWith(wanted));
      if (found >= 0) index = found;
    }

    resize();
    drawSpark();
    await load(index, { initial: true });
    requestAnimationFrame(frame);
  } catch (error) {
    $('repo').textContent = 'unavailable';
    toast('cannot reach the strata server: ' + error.message, true);
  }
}

function buildLegend(layers) {
  const legend = $('legend');
  if (!layers.length) {
    legend.innerHTML = '<span class="item">colour = stable by module path</span>';
    return;
  }
  legend.innerHTML = layers
    .map((layer) => {
      const color = state.layerColors.get(layer.name) || '#7f8da3';
      return '<span class="item"><span class="sw" style="background:' + color +
        '"></span>' + esc(layer.name) + '</span>';
    })
    .join('');
}

/* ---------- graph loading ---------- */

async function load(index, options = {}) {
  if (!state.commits.length) return;
  index = clamp(index, 0, state.commits.length - 1);
  const commit = state.commits[index];
  const token = ++state.token;

  const cached = state.cache.get(commit.sha);
  if (cached) {
    apply(cached, index, options);
    return;
  }

  $('loading').classList.add('on');
  try {
    const response = await fetch('/api/graph?sha=' + encodeURIComponent(commit.sha));
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || 'graph request failed');
    remember(commit.sha, data);
    if (token === state.token) apply(data, index, options);
  } catch (error) {
    if (token === state.token) toast('cannot load ' + commit.short + ': ' + error.message, true);
  } finally {
    if (token === state.token) $('loading').classList.remove('on');
  }
}

function remember(sha, data) {
  state.cache.set(sha, data);
  if (state.cache.size > 60) {
    const first = state.cache.keys().next().value;
    state.cache.delete(first);
  }
}

function prefetch(index) {
  for (let offset = 1; offset <= 3; offset++) {
    const commit = state.commits[index + offset];
    if (!commit || state.cache.has(commit.sha)) continue;
    fetch('/api/graph?sha=' + encodeURIComponent(commit.sha))
      .then((response) => (response.ok ? response.json() : null))
      .then((data) => { if (data) remember(commit.sha, data); })
      .catch(() => {});
  }
}

function apply(data, index, options) {
  state.index = index;
  state.graph = data;
  state.cycleEdges = new Set(data.edges.filter((e) => e.cycle).map((e) => e.from + '\u0000' + e.to));
  state.edgeSet = new Set(data.edges.map((e) => e.from + '\u0000' + e.to));
  state.nodeIndex = new Map(data.nodes.map((n) => [n.id, n]));
  state.degree = new Map();
  for (const edge of data.edges) {
    state.degree.set(edge.from, (state.degree.get(edge.from) || 0) + 1);
    state.degree.set(edge.to, (state.degree.get(edge.to) || 0) + 1);
  }

  seedPositions(data);
  simulate(data, 280);
  state.appliedAt = performance.now();

  $('timeline').value = String(index);
  $('sha').textContent = data.short;
  $('subject').textContent = data.subject;
  $('seq').textContent = 'commit ' + (index + 1) + ' / ' + state.commits.length;

  if (state.selected) {
    const stillThere = state.selected.type === 'node'
      ? data.nodes.some((n) => n.id === state.selected.id)
      : data.edges.some((e) => e.from === state.selected.from && e.to === state.selected.to);
    if (!stillThere) clearSelection();
    else renderPanel();
  }

  if (!state.selected && options.initial) selectBusiest();
  if (options.initial || state.needsFit) {
    fit();
    state.needsFit = false;
  }

  updateHighlight();
  renderMetrics();
  drawSpark();
  toastDiff(data, index, options);
  history.replaceState(null, '', '#' + data.short);
  prefetch(index);
}

function toastDiff(data, index, options) {
  if (options.initial || index === 0) return;
  const previous = state.commits[index - 1];
  const newCycle = data.stats.cycles > previous.cycles;
  if (newCycle) {
    toast('cycle introduced · ' + data.short, true);
  } else if (!state.playing && data.diff.addedNodes.length) {
    toast('+' + data.diff.addedNodes.length + ' module' +
      (data.diff.addedNodes.length === 1 ? '' : 's') + ' · ' + data.short);
  } else if (!state.playing && data.diff.removedNodes.length) {
    toast('-' + data.diff.removedNodes.length + ' module' +
      (data.diff.removedNodes.length === 1 ? '' : 's') + ' · ' + data.short);
  }
}

/* ---------- layout ---------- */

function seedPositions(data) {
  const live = new Set(data.nodes.map((n) => n.id));
  for (const id of Array.from(state.positions.keys())) {
    if (!live.has(id)) state.positions.delete(id);
  }

  data.nodes.forEach((node, index) => {
    if (state.positions.has(node.id)) return;

    let sumX = 0;
    let sumY = 0;
    let count = 0;
    for (const edge of data.edges) {
      const other = edge.from === node.id ? edge.to : edge.to === node.id ? edge.from : null;
      if (!other) continue;
      const p = state.positions.get(other);
      if (p) { sumX += p.x; sumY += p.y; count += 1; }
    }

    let x;
    let y;
    if (count) {
      x = sumX / count + (rand(hash(node.id) + 1) - 0.5) * 50;
      y = sumY / count + (rand(hash(node.id) + 2) - 0.5) * 50;
    } else {
      const angle = (index / Math.max(1, data.nodes.length)) * TAU +
        rand(hash(node.id)) * 0.8;
      const r = 130 + rand(hash(node.id) + 3) * 190;
      x = Math.cos(angle) * r;
      y = Math.sin(angle) * r;
    }
    state.positions.set(node.id, { x, y, vx: 0, vy: 0, fixed: false });
  });
}

function simulate(data, ticks) {
  const nodes = data.nodes
    .map((node) => ({ id: node.id, p: state.positions.get(node.id), r: radius(node) }))
    .filter((node) => node.p);
  if (!nodes.length) return;

  const lookup = new Map(nodes.map((node) => [node.id, node]));
  const links = data.edges
    .map((edge) => ({ a: lookup.get(edge.from), b: lookup.get(edge.to), w: edge.weight }))
    .filter((edge) => edge.a && edge.b);

  for (let step = 0; step < ticks; step++) {
    const cool = 1 - step / ticks;
    const alpha = 0.1 + 0.9 * cool * cool;

    for (let i = 0; i < nodes.length; i++) {
      const a = nodes[i].p;
      for (let j = i + 1; j < nodes.length; j++) {
        const b = nodes[j].p;
        let dx = b.x - a.x;
        let dy = b.y - a.y;
        let d2 = dx * dx + dy * dy;
        if (d2 < 0.01) { dx = 0.6; dy = 0.6; d2 = 0.72; }
        const d = Math.sqrt(d2);
        const force = (1500 / d2) * alpha;
        const fx = (dx / d) * force;
        const fy = (dy / d) * force;
        a.vx -= fx; a.vy -= fy;
        b.vx += fx; b.vy += fy;

        const min = nodes[i].r + nodes[j].r + 18;
        if (d < min) {
          const push = ((min - d) / d) * 0.16 * alpha;
          a.vx -= dx * push; a.vy -= dy * push;
          b.vx += dx * push; b.vy += dy * push;
        }
      }
    }

    for (const link of links) {
      const a = link.a.p;
      const b = link.b.p;
      const dx = b.x - a.x;
      const dy = b.y - a.y;
      const d = Math.hypot(dx, dy) || 0.01;
      const target = 104 + 24 * Math.log2(1 + (link.w || 1));
      const force = ((d - target) / d) * 0.012 * alpha;
      const fx = dx * force;
      const fy = dy * force;
      a.vx += fx; a.vy += fy;
      b.vx -= fx; b.vy -= fy;
    }

    for (const node of nodes) {
      const p = node.p;
      p.vx -= p.x * 0.0017 * alpha;
      p.vy -= p.y * 0.0017 * alpha;
      if (p.fixed) { p.vx = 0; p.vy = 0; continue; }
      p.vx *= 0.85;
      p.vy *= 0.85;
      const speed = Math.hypot(p.vx, p.vy);
      if (speed > 24) {
        p.vx = (p.vx / speed) * 24;
        p.vy = (p.vy / speed) * 24;
      }
      p.x += p.vx;
      p.y += p.vy;
    }
  }
}

function fit() {
  if (!state.graph) return;
  const points = state.graph.nodes.map((node) => pos(node.id)).filter(Boolean);
  if (!points.length) return;

  const minX = Math.min(...points.map((p) => p.x));
  const maxX = Math.max(...points.map((p) => p.x));
  const minY = Math.min(...points.map((p) => p.y));
  const maxY = Math.max(...points.map((p) => p.y));
  const width = Math.max(1, maxX - minX);
  const height = Math.max(1, maxY - minY);
  const padding = 150;

  const panelOpen = !$('panel').classList.contains('hidden');
  const inset = panelOpen ? Math.min(400, state.width * 0.48) : 0;
  const availableWidth = Math.max(240, state.width - inset - 80);
  const availableHeight = Math.max(240, state.height - 70);

  const zoom = clamp(Math.min(availableWidth / width, availableHeight / height), 0.22, 2.3);
  state.cam.z = zoom;
  state.cam.x = 24 + availableWidth / 2 - ((minX + maxX) / 2) * zoom;
  state.cam.y = state.height / 2 - ((minY + maxY) / 2) * zoom;
}

/* ---------- drawing ---------- */

function resize() {
  const wrap = $('stage-wrap');
  const width = wrap.clientWidth;
  const height = wrap.clientHeight;
  const dpr = window.devicePixelRatio || 1;

  if (state.width && state.cam.x) {
    state.cam.x += (width - state.width) / 2;
    state.cam.y += (height - state.height) / 2;
  } else {
    state.cam.x = width / 2;
    state.cam.y = height / 2;
  }

  state.width = width;
  state.height = height;
  state.dpr = dpr;

  const canvas = $('stage');
  canvas.width = Math.round(width * dpr);
  canvas.height = Math.round(height * dpr);
  state.ctx = canvas.getContext('2d');
  drawSpark();
}

function frame() {
  draw();
  requestAnimationFrame(frame);
}

function draw() {
  const ctx = state.ctx;
  if (!ctx || !state.graph) return;

  ctx.setTransform(state.dpr, 0, 0, state.dpr, 0, 0);
  ctx.clearRect(0, 0, state.width, state.height);

  ctx.save();
  ctx.translate(state.cam.x, state.cam.y);
  ctx.scale(state.cam.z, state.cam.z);

  const focus = state.highlight;
  const hoverEdge = state.hover && state.hover.type === 'edge' ? state.hover : null;
  const now = performance.now();

  for (const edge of state.graph.edges) {
    const a = pos(edge.from);
    const b = pos(edge.to);
    if (!a || !b) continue;

    const key = edge.from + '\u0000' + edge.to;
    const isCycle = state.cycleEdges.has(key);
    const isHover = hoverEdge && hoverEdge.from === edge.from && hoverEdge.to === edge.to;
    const dim = focus && !focus.has(edge.from) && !focus.has(edge.to);

    const ra = radiusOfId(edge.from);
    const rb = radiusOfId(edge.to);
    const dx = b.x - a.x;
    const dy = b.y - a.y;
    const distance = Math.hypot(dx, dy) || 0.01;
    const ux = dx / distance;
    const uy = dy / distance;
    const x1 = a.x + ux * ra;
    const y1 = a.y + uy * ra;
    const x2 = b.x - ux * (rb + 2);
    const y2 = b.y - uy * (rb + 2);

    const reverse = state.edgeSet && state.edgeSet.has(edge.to + '\u0000' + edge.from);
    let controlX = 0;
    let controlY = 0;
    if (reverse) {
      const bend = 26;
      controlX = (x1 + x2) / 2 + (-dy / distance) * bend;
      controlY = (y1 + y2) / 2 + (dx / distance) * bend;
    }

    let color;
    if (isCycle) color = 'rgba(255,107,120,' + (dim ? 0.14 : 0.62) + ')';
    else if (isHover) color = 'rgba(243,181,98,0.9)';
    else color = 'rgba(126,146,176,' + (dim ? 0.07 : 0.22) + ')';

    ctx.strokeStyle = color;
    ctx.lineWidth = (isCycle ? 1.7 : isHover ? 1.8 : 0.9) / state.cam.z;
    ctx.beginPath();
    ctx.moveTo(x1, y1);
    if (reverse) ctx.quadraticCurveTo(controlX, controlY, x2, y2);
    else ctx.lineTo(x2, y2);
    ctx.stroke();

    if (!dim) {
      let tx = ux;
      let ty = uy;
      if (reverse) {
        const tdx = x2 - controlX;
        const tdy = y2 - controlY;
        const td = Math.hypot(tdx, tdy) || 1;
        tx = tdx / td;
        ty = tdy / td;
      }
      const size = (isHover ? 6 : 4.4) / state.cam.z;
      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.moveTo(x2, y2);
      ctx.lineTo(x2 - tx * size + ty * size * 0.55, y2 - ty * size - tx * size * 0.55);
      ctx.lineTo(x2 - tx * size - ty * size * 0.55, y2 - ty * size + tx * size * 0.55);
      ctx.closePath();
      ctx.fill();
    }
  }

  for (const node of state.graph.nodes) {
    const p = pos(node.id);
    if (!p) continue;

    const selected = state.selected && state.selected.type === 'node' &&
      state.selected.id === node.id;
    const hovered = state.hover && state.hover.type === 'node' &&
      state.hover.id === node.id;
    const dim = focus && !focus.has(node.id);
    const r = radius(node);

    ctx.globalAlpha = dim ? 0.38 : 1;

    const gradient = ctx.createRadialGradient(p.x - r * 0.35, p.y - r * 0.35, r * 0.15,
                                              p.x, p.y, r);
    const color = colorFor(node);
    gradient.addColorStop(0, color);
    gradient.addColorStop(1, color + 'aa');
    ctx.fillStyle = gradient;
    ctx.beginPath();
    ctx.arc(p.x, p.y, r, 0, TAU);
    ctx.fill();

    if (node.cycle) {
      ctx.strokeStyle = '#ff6b78';
      ctx.lineWidth = 1.8 / state.cam.z;
      ctx.beginPath();
      ctx.arc(p.x, p.y, r + 2.5 / state.cam.z, 0, TAU);
      ctx.stroke();
    }

    if (node.added && now - state.appliedAt < 1700) {
      const progress = (now - state.appliedAt) / 1700;
      ctx.strokeStyle = 'rgba(255,255,255,' + (0.55 * (1 - progress)) + ')';
      ctx.lineWidth = 1.4 / state.cam.z;
      ctx.beginPath();
      ctx.arc(p.x, p.y, r + (8 + 10 * progress) / state.cam.z, 0, TAU);
      ctx.stroke();
    }

    if (selected) {
      ctx.strokeStyle = '#ffffff';
      ctx.lineWidth = 1.7 / state.cam.z;
      ctx.beginPath();
      ctx.arc(p.x, p.y, r + 4 / state.cam.z, 0, TAU);
      ctx.stroke();
    } else if (hovered) {
      ctx.strokeStyle = 'rgba(255,255,255,0.55)';
      ctx.lineWidth = 1.2 / state.cam.z;
      ctx.beginPath();
      ctx.arc(p.x, p.y, r + 3 / state.cam.z, 0, TAU);
      ctx.stroke();
    }

    ctx.globalAlpha = dim ? 0.28 : 1;
    const showLabel = selected || hovered || state.cam.z > 0.82 || degreeOf(node.id) >= 3;
    if (showLabel) {
      const size = 10.5 / state.cam.z;
      ctx.font = '500 ' + size + 'px ' +
        'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace';
      ctx.textBaseline = 'middle';
      ctx.lineWidth = 3 / state.cam.z;
      ctx.strokeStyle = 'rgba(7,10,16,0.82)';
      ctx.strokeText(node.label, p.x + r + 5 / state.cam.z, p.y);
      ctx.fillStyle = 'rgba(217,227,241,0.86)';
      ctx.fillText(node.label, p.x + r + 5 / state.cam.z, p.y);
    }
    ctx.globalAlpha = 1;
  }

  ctx.restore();
}

/* ---------- picking ---------- */

function pick(x, y) {
  if (!state.graph) return null;
  const camera = state.cam;

  for (let i = state.graph.nodes.length - 1; i >= 0; i--) {
    const node = state.graph.nodes[i];
    const p = pos(node.id);
    if (!p) continue;
    const sx = p.x * camera.z + camera.x;
    const sy = p.y * camera.z + camera.y;
    const r = radius(node) * camera.z + 4;
    if ((x - sx) ** 2 + (y - sy) ** 2 <= r * r) {
      return { type: 'node', id: node.id };
    }
  }

  for (const edge of state.graph.edges) {
    const a = pos(edge.from);
    const b = pos(edge.to);
    if (!a || !b) continue;
    const ax = a.x * camera.z + camera.x;
    const ay = a.y * camera.z + camera.y;
    const bx = b.x * camera.z + camera.x;
    const by = b.y * camera.z + camera.y;

    let hitDistance;
    if (state.edgeSet && state.edgeSet.has(edge.to + '\u0000' + edge.from)) {
      const ddx = bx - ax;
      const ddy = by - ay;
      const dd = Math.hypot(ddx, ddy) || 1;
      const cxp = (ax + bx) / 2 + (-ddy / dd) * 26;
      const cyp = (ay + by) / 2 + (ddx / dd) * 26;
      let previousX = ax;
      let previousY = ay;
      hitDistance = Infinity;
      for (let step = 1; step <= 10; step++) {
        const t = step / 10;
        const inv = 1 - t;
        const sx = inv * inv * ax + 2 * inv * t * cxp + t * t * bx;
        const sy = inv * inv * ay + 2 * inv * t * cyp + t * t * by;
        hitDistance = Math.min(hitDistance,
          distanceToSegment(x, y, previousX, previousY, sx, sy));
        previousX = sx;
        previousY = sy;
      }
    } else {
      hitDistance = distanceToSegment(x, y, ax, ay, bx, by);
    }
    if (hitDistance < 6) {
      return { type: 'edge', from: edge.from, to: edge.to };
    }
  }
  return null;
}

function distanceToSegment(px, py, x1, y1, x2, y2) {
  const dx = x2 - x1;
  const dy = y2 - y1;
  const length2 = dx * dx + dy * dy;
  if (length2 === 0) return Math.hypot(px - x1, py - y1);
  const t = clamp(((px - x1) * dx + (py - y1) * dy) / length2, 0, 1);
  return Math.hypot(px - (x1 + t * dx), py - (y1 + t * dy));
}

/* ---------- selection and panel ---------- */

function select(item) {
  state.selected = item;
  updateHighlight();
  renderPanel();
}

function clearSelection() {
  state.selected = null;
  state.hover = null;
  updateHighlight();
  $('panel').classList.add('hidden');
}

function selectBusiest() {
  if (!state.graph || !state.graph.nodes.length) return;
  let best = null;
  let score = -1;
  for (const node of state.graph.nodes) {
    const value = degreeOf(node.id) * 1000 + node.loc;
    if (value > score) { score = value; best = node; }
  }
  if (best) select({ type: 'node', id: best.id });
}

function renderPanel() {
  if (!state.graph || !state.selected) return;
  const body = $('panel-body');
  const panel = $('panel');

  if (state.selected.type === 'node') {
    const node = state.graph.nodes.find((n) => n.id === state.selected.id);
    if (!node) { clearSelection(); return; }
    const out = state.graph.edges.filter((e) => e.from === node.id)
      .sort((a, b) => b.weight - a.weight);
    const incoming = state.graph.edges.filter((e) => e.to === node.id)
      .sort((a, b) => b.weight - a.weight);
    const first = state.commits[node.since] || state.commits[0];

    body.innerHTML =
      '<div class="eyebrow">module</div>' +
      '<h2>' + esc(node.id) + '</h2>' +
      '<div class="badges">' +
        (node.layer ? '<span class="badge layer">' + esc(node.layer) + '</span>' : '') +
        (node.lang ? '<span class="badge">' + esc(node.lang) + '</span>' : '') +
        (node.cycle ? '<span class="badge cycle">in a cycle</span>' : '') +
        (node.added ? '<span class="badge">new</span>' : '') +
      '</div>' +
      '<div class="grid">' +
        '<div><b>' + fmt(node.loc) + '</b><span>lines</span></div>' +
        '<div><b>' + node.files + '</b><span>files</span></div>' +
        '<div><b>' + node.churn + '</b><span>commits</span></div>' +
        '<div><b>' + (out.length + incoming.length) + '</b><span>edges</span></div>' +
      '</div>' +
      edgeSection('depends on', out, 'to') +
      edgeSection('used by', incoming, 'from') +
      '<div class="section"><h3>first seen</h3><p class="meta-line"><b>#' +
        (node.since + 1) + '</b> · ' + esc(first.short) + ' · ' + date(first.time) +
      '</p></div>';
  } else {
    const edge = state.selected;
    const weight = state.graph.edges.find((e) => e.from === edge.from && e.to === edge.to);
    if (!weight) { clearSelection(); return; }
    body.innerHTML =
      '<div class="eyebrow">dependency</div>' +
      '<h2>' + esc(edge.from) + '<br><span style="color:var(--dim)">↓</span> ' +
        esc(edge.to) + '</h2>' +
      '<div class="badges">' +
        (weight.cycle ? '<span class="badge cycle">part of a cycle</span>' : '') +
      '</div>' +
      '<div class="grid">' +
        '<div><b>' + weight.weight + '</b><span>imports</span></div>' +
        '<div><b>#' + (weight.since + 1) + '</b><span>since</span></div>' +
      '</div>' +
      '<div class="section"><h3>direction</h3><p class="meta-line">' +
        esc(edge.from) + ' imports ' + esc(edge.to) + '</p></div>';
  }

  panel.classList.remove('hidden');
}

function edgeSection(title, edges, direction) {
  if (!edges.length) {
    return '<div class="section"><h3>' + title +
      '</h3><p class="meta-line">none</p></div>';
  }
  const rows = edges.map((edge) => {
    const other = direction === 'to' ? edge.to : edge.from;
    const node = state.graph.nodes.find((n) => n.id === other) || { id: other };
    return '<button class="dep" data-node="' + esc(other) + '">' +
      '<span class="dot" style="background:' + colorFor(node) + '"></span>' +
      '<span>' + esc(other) + '</span>' +
      '<span class="n">' + edge.weight + '</span>' +
      '</button>';
  }).join('');
  return '<div class="section"><h3>' + title + '</h3>' + rows + '</div>';
}

/* ---------- metrics, timeline, toasts ---------- */

function renderMetrics() {
  const graph = state.graph;
  if (!graph) return;
  const previous = state.index > 0 ? state.commits[state.index - 1] : null;
  const items = [];

  const push = (label, value, delta, bad) => {
    let change = '';
    if (delta !== null && delta !== undefined && delta !== 0) {
      change = '<span class="d ' + (bad || delta > 0 ? 'up' : 'down') + '">' +
        (delta > 0 ? '+' : '') + delta + '</span>';
    }
    items.push('<div class="metric' + (bad && value !== '0' ? ' bad' : '') + '">' +
      '<span class="v">' + value + '</span>' +
      '<span class="l">' + label + '</span>' + change + '</div>');
  };

  push('modules', graph.stats.nodes, previous ? graph.stats.nodes - previous.nodes : null, false);
  push('edges', graph.stats.edges, previous ? graph.stats.edges - previous.edges : null, false);
  push('cycles', graph.stats.cycles, previous ? graph.stats.cycles - previous.cycles : null,
    graph.stats.cycles > 0);
  push('lines', fmt(graph.stats.loc), previous ? graph.stats.loc - previous.loc : null, false);
  if (graph.stats.violations > 0) {
    items.push('<div class="metric bad"><span class="v">' + graph.stats.violations +
      '</span><span class="l">violations</span></div>');
  }
  $('metrics').innerHTML = items.join('');
}

function drawSpark() {
  const canvas = $('spark');
  if (!canvas || !state.commits.length) return;
  const dpr = window.devicePixelRatio || 1;
  const width = canvas.clientWidth;
  const height = 26;
  canvas.width = Math.max(1, Math.round(width * dpr));
  canvas.height = Math.round(height * dpr);

  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);

  const commits = state.commits;
  const values = commits.map((c) => c.nodes);
  const min = Math.min(...values);
  const max = Math.max(...values);
  const span = max - min || 1;
  const x = (i) => (commits.length === 1 ? width / 2 : (i / (commits.length - 1)) * width);
  const y = (value) => height - 5 - ((value - min) / span) * (height - 10);

  ctx.beginPath();
  commits.forEach((commit, i) => {
    const px = x(i);
    const py = y(commit.nodes);
    if (i === 0) ctx.moveTo(px, py);
    else ctx.lineTo(px, py);
  });

  const gradient = ctx.createLinearGradient(0, 0, 0, height);
  gradient.addColorStop(0, 'rgba(94,234,212,0.16)');
  gradient.addColorStop(1, 'rgba(94,234,212,0)');
  ctx.lineTo(width, height);
  ctx.lineTo(0, height);
  ctx.closePath();
  ctx.fillStyle = gradient;
  ctx.fill();

  ctx.beginPath();
  commits.forEach((commit, i) => {
    const px = x(i);
    const py = y(commit.nodes);
    if (i === 0) ctx.moveTo(px, py);
    else ctx.lineTo(px, py);
  });
  ctx.strokeStyle = 'rgba(94,234,212,0.55)';
  ctx.lineWidth = 1;
  ctx.stroke();

  commits.forEach((commit, i) => {
    if (commit.cycles > 0) {
      ctx.fillStyle = 'rgba(255,107,120,0.8)';
      ctx.fillRect(x(i) - 0.5, height - 3, 1.4, 3);
    }
  });

  if (state.graph) {
    const px = x(state.index);
    ctx.fillStyle = 'rgba(243,181,98,0.9)';
    ctx.fillRect(px - 0.5, 0, 1, height);
    const py = y(state.commits[state.index].nodes);
    ctx.beginPath();
    ctx.arc(px, py, 2.1, 0, TAU);
    ctx.fill();
  }
}

function toast(message, bad) {
  const element = $('toast');
  element.textContent = message;
  element.classList.toggle('bad', Boolean(bad));
  element.classList.remove('hidden');
  clearTimeout(state.toastTimer);
  state.toastTimer = setTimeout(() => element.classList.add('hidden'), 2600);
}

/* ---------- play ---------- */

function togglePlay() {
  state.playing = !state.playing;
  $('play').classList.toggle('playing', state.playing);
  clearInterval(state.timer);

  if (!state.playing) return;
  if (state.index >= state.commits.length - 1) load(0);
  state.timer = setInterval(() => {
    if (state.index >= state.commits.length - 1) {
      togglePlay();
      return;
    }
    load(state.index + 1);
  }, 210);
}

/* ---------- events ---------- */

const canvas = $('stage');

canvas.addEventListener('mousedown', (event) => {
  const hit = pick(event.offsetX, event.offsetY);
  if (hit && hit.type === 'node') {
    select(hit);
    const p = pos(hit.id);
    const world = screenToWorld(event.offsetX, event.offsetY);
    state.drag = { type: 'node', id: hit.id, dx: p.x - world.x, dy: p.y - world.y };
    p.fixed = true;
    canvas.classList.add('dragging');
    return;
  }
  if (hit && hit.type === 'edge') {
    select(hit);
    return;
  }
  state.drag = {
    type: 'pan',
    sx: event.clientX,
    sy: event.clientY,
    cx: state.cam.x,
    cy: state.cam.y,
    moved: false,
  };
  canvas.classList.add('dragging');
});

canvas.addEventListener('mousemove', (event) => {
  if (state.drag && state.drag.type === 'pan') {
    const dx = event.clientX - state.drag.sx;
    const dy = event.clientY - state.drag.sy;
    if (Math.abs(dx) + Math.abs(dy) > 3) state.drag.moved = true;
    state.cam.x = state.drag.cx + dx;
    state.cam.y = state.drag.cy + dy;
    return;
  }

  if (state.drag && state.drag.type === 'node') {
    const world = screenToWorld(event.offsetX, event.offsetY);
    const p = pos(state.drag.id);
    if (p) {
      p.x = world.x + state.drag.dx;
      p.y = world.y + state.drag.dy;
    }
    return;
  }

  const hit = pick(event.offsetX, event.offsetY);
  state.hover = hit;
  canvas.classList.toggle('hot', Boolean(hit));
  updateHighlight();
});

canvas.addEventListener('mouseup', finishDrag);
window.addEventListener('mouseup', (event) => {
  if (event.target !== canvas) finishDrag();
});

function finishDrag() {
  if (state.drag && state.drag.type === 'node') {
    const p = pos(state.drag.id);
    if (p) p.fixed = false;
    simulate(state.graph, 40);
  } else if (state.drag && state.drag.type === 'pan' && !state.drag.moved) {
    clearSelection();
  }
  state.drag = null;
  canvas.classList.remove('dragging');
}

canvas.addEventListener('mouseleave', () => {
  state.hover = null;
  updateHighlight();
  if (state.drag) {
    const p = pos(state.drag.id);
    if (p) p.fixed = false;
    state.drag = null;
    canvas.classList.remove('dragging');
  }
});

canvas.addEventListener('wheel', (event) => {
  event.preventDefault();
  const factor = Math.exp(-event.deltaY * 0.0016);
  const zoom = clamp(state.cam.z * factor, 0.18, 4.5);
  const world = screenToWorld(event.offsetX, event.offsetY);
  state.cam.z = zoom;
  state.cam.x = event.offsetX - world.x * zoom;
  state.cam.y = event.offsetY - world.y * zoom;
}, { passive: false });

canvas.addEventListener('dblclick', () => fit());

function screenToWorld(x, y) {
  return {
    x: (x - state.cam.x) / state.cam.z,
    y: (y - state.cam.y) / state.cam.z,
  };
}

$('timeline').addEventListener('input', (event) => {
  if (state.playing) togglePlay();
  load(Number(event.target.value));
});

$('play').addEventListener('click', togglePlay);
$('help').addEventListener('click', () => $('modal').classList.remove('hidden'));
$('modal-close').addEventListener('click', () => $('modal').classList.add('hidden'));
$('modal').addEventListener('click', (event) => {
  if (event.target === $('modal')) $('modal').classList.add('hidden');
});
$('close').addEventListener('click', clearSelection);

$('panel-body').addEventListener('click', (event) => {
  const button = event.target.closest('[data-node]');
  if (button) select({ type: 'node', id: button.dataset.node });
});

window.addEventListener('keydown', (event) => {
  if (event.key === 'Escape') {
    $('modal').classList.add('hidden');
    clearSelection();
    return;
  }
  if (event.target.tagName === 'INPUT') return;

  if (event.key === 'ArrowLeft') {
    if (state.playing) togglePlay();
    load(state.index - 1);
  } else if (event.key === 'ArrowRight') {
    if (state.playing) togglePlay();
    load(state.index + 1);
  } else if (event.key === ' ') {
    event.preventDefault();
    togglePlay();
  } else if (event.key === 'f') {
    fit();
  } else if (event.key === '?') {
    $('modal').classList.remove('hidden');
  }
});

window.addEventListener('hashchange', () => {
  const wanted = location.hash.replace('#', '');
  if (!wanted) return;
  const index = state.commits.findIndex((c) => c.sha.startsWith(wanted));
  if (index >= 0 && index !== state.index) load(index);
});

let resizeTimer = 0;
window.addEventListener('resize', () => {
  clearTimeout(resizeTimer);
  resizeTimer = setTimeout(() => { resize(); }, 80);
});

boot();
