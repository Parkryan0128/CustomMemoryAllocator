#!/usr/bin/env python3
"""Generate index.html — unified benchmark + lifecycle dashboard."""

from __future__ import annotations

import json

from load_data import (
    BENCHMARK_LABELS,
    BENCHMARK_ORDER,
    LIFECYCLE_LABELS,
    LIFECYCLE_ORDER,
    ROOT,
    load_benchmark_rows,
    load_lifecycle_traces,
)

OUT_PATH = ROOT / "index.html"


def build_html(benchmark_rows: list[dict], traces: dict[str, dict]) -> str:
    benchmark_payload = json.dumps(benchmark_rows, separators=(",", ":"))
    trace_payload = json.dumps(traces, separators=(",", ":"))
    benchmark_labels = json.dumps(BENCHMARK_LABELS)
    benchmark_order = json.dumps(BENCHMARK_ORDER)
    lifecycle_labels = json.dumps(LIFECYCLE_LABELS)
    lifecycle_order = json.dumps([w for w in LIFECYCLE_ORDER if w in traces])
    has_traces = bool(traces)

    if has_traces:
        lifecycle_body = """
    <div class="controls">
      <div>
        <label for="workload-select">Workload</label><br />
        <select id="workload-select"></select>
      </div>
    </div>

    <div class="playback">
      <button id="play-btn" type="button" class="action">▶ Play</button>
      <button id="reset-btn" type="button" class="action secondary">Reset</button>
      <input id="scrub" type="range" min="0" max="0" value="0" step="1" />
      <span class="playback-label" id="scrub-label">—</span>
    </div>

    <div class="cards">
      <div class="card">
        <h3>Live bytes (now)</h3>
        <div class="metric" id="card-live">—</div>
        <div class="metric-sub" id="card-live-sub">peak: —</div>
      </div>
      <div class="card">
        <h3>Active pages (now)</h3>
        <div class="metric" id="card-pages">—</div>
        <div class="metric-sub" id="card-pages-sub">peak: —</div>
      </div>
      <div class="card">
        <h3>Mapped bytes (now)</h3>
        <div class="metric" id="card-mapped">—</div>
        <div class="metric-sub" id="card-mapped-sub">peak: —</div>
      </div>
      <div class="card">
        <h3>Operation</h3>
        <div class="metric" id="card-op">—</div>
        <div class="metric-sub" id="card-op-sub">—</div>
      </div>
      <div class="card">
        <h3>Trace run time</h3>
        <div class="metric" id="card-elapsed">—</div>
        <div class="metric-sub" id="card-trace-sub">—</div>
      </div>
    </div>

    <div class="panel">
      <h2>Live bytes over operations</h2>
      <div class="chart-wrap chart-wrap-lifecycle">
        <canvas id="live-chart"></canvas>
      </div>
    </div>

    <div class="panel">
      <h2>Active pages over operations</h2>
      <div class="chart-wrap chart-wrap-lifecycle">
        <canvas id="pages-chart"></canvas>
      </div>
    </div>

    <div class="panel">
      <h2>Mapped bytes over operations</h2>
      <div class="chart-wrap chart-wrap-lifecycle">
        <canvas id="mapped-chart"></canvas>
      </div>
    </div>"""
    else:
        lifecycle_body = """
    <div class="panel empty-state">
      <p>No lifecycle trace data. Run <code>make dashboard</code>.</p>
    </div>"""

    lifecycle_script = ""
    if has_traces:
        lifecycle_script = """
    const HAS_TRACES = true;
    let lifecycleInitialized = false;
    let playTimer = null;
    let liveChart = null;
    let pagesChart = null;
    let mappedChart = null;
    let meta = null;
    let samples = [];
    let traceLabels = [];
    let fullLive = [];
    let fullPages = [];
    let fullMapped = [];
    let peakLive = 0;
    let peakMapped = 0;
    let peakPages = 0;

    function formatBytes(n) {
      if (n >= 1024 * 1024) return (n / (1024 * 1024)).toFixed(2) + " MB";
      if (n >= 1024) return (n / 1024).toFixed(1) + " KB";
      return n + " B";
    }

    function sampleLabel(sample) {
      const event = sample.event && sample.event !== "step" ? " · " + sample.event : "";
      return formatOps(sample.op) + event;
    }

    function computePeaks() {
      peakLive = 0;
      peakMapped = 0;
      peakPages = 0;
      for (const sample of samples) {
        peakLive = Math.max(peakLive, sample.live_bytes);
        peakMapped = Math.max(peakMapped, sample.mapped_bytes);
        peakPages = Math.max(peakPages, sample.active_pages);
      }
    }

    function updateStaticCards() {
      const workloadSelect = document.getElementById("workload-select");
      document.getElementById("card-elapsed").textContent =
        Number(meta.elapsed_ms).toFixed(1) + " ms";
      document.getElementById("card-trace-sub").textContent =
        formatOps(meta.ops) + " ops · " + samples.length + " samples · every " +
        formatOps(meta.sample_interval);
      document.getElementById("header-subtitle").textContent =
        meta.block_size + " B blocks · " +
        (LIFECYCLE_LABELS[workloadSelect.value] || workloadSelect.value) +
        " · scrub or play to walk the trace";
    }

    function updateLifecycleCards(index) {
      const sample = samples[index];
      document.getElementById("card-live").textContent = formatBytes(sample.live_bytes);
      document.getElementById("card-live-sub").textContent = "peak: " + formatBytes(peakLive);
      document.getElementById("card-pages").textContent = String(sample.active_pages);
      document.getElementById("card-pages-sub").textContent = "peak: " + String(peakPages);
      document.getElementById("card-mapped").textContent = formatBytes(sample.mapped_bytes);
      document.getElementById("card-mapped-sub").textContent = "peak: " + formatBytes(peakMapped);
      document.getElementById("card-op").textContent = formatOps(sample.op);
      document.getElementById("card-op-sub").textContent =
        "sample " + (index + 1) + "/" + samples.length +
        (sample.event ? " · " + sample.event : "") +
        " · " + Number(sample.elapsed_ms).toFixed(2) + " ms";
      document.getElementById("scrub-label").textContent = sampleLabel(sample);
    }

    function updateLifecycleCharts(index) {
      const visibleLabels = traceLabels.slice(0, index + 1);
      liveChart.data.labels = visibleLabels;
      liveChart.data.datasets[0].data = fullLive.slice(0, index + 1);
      liveChart.update("none");

      pagesChart.data.labels = visibleLabels;
      pagesChart.data.datasets[0].data = fullPages.slice(0, index + 1);
      pagesChart.update("none");

      mappedChart.data.labels = visibleLabels;
      mappedChart.data.datasets[0].data = fullMapped.slice(0, index + 1);
      mappedChart.update("none");
    }

    function setLifecycleIndex(index) {
      const scrub = document.getElementById("scrub");
      const clamped = Math.max(0, Math.min(index, samples.length - 1));
      scrub.value = String(clamped);
      updateLifecycleCards(clamped);
      updateLifecycleCharts(clamped);
    }

    function stopPlay() {
      if (playTimer) {
        clearInterval(playTimer);
        playTimer = null;
      }
      document.getElementById("play-btn").textContent = "▶ Play";
    }

    const chartDefaults = {
      responsive: true,
      maintainAspectRatio: false,
      animation: false,
      plugins: {
        legend: { labels: { color: "#e6edf3" } },
      },
      scales: {
        x: {
          title: { display: true, text: "Sample (operation · event)", color: "#8b949e" },
          ticks: { color: "#8b949e", maxTicksLimit: 10 },
          grid: { color: "#2a3544" },
        },
        y: {
          ticks: { color: "#8b949e" },
          grid: { color: "#2a3544" },
          beginAtZero: true,
        },
      },
    };

    function buildLifecycleCharts() {
      const pointRadius = samples.length > 120 ? 0 : 2;
      const firstLabel = traceLabels.length > 0 ? [traceLabels[0]] : [];
      const firstLive = fullLive.length > 0 ? [fullLive[0]] : [];
      const firstPages = fullPages.length > 0 ? [fullPages[0]] : [];
      const firstMapped = fullMapped.length > 0 ? [fullMapped[0]] : [];

      if (liveChart) liveChart.destroy();
      if (pagesChart) pagesChart.destroy();
      if (mappedChart) mappedChart.destroy();

      liveChart = new Chart(document.getElementById("live-chart"), {
        type: "line",
        data: {
          labels: firstLabel,
          datasets: [{
            label: "Live bytes",
            data: firstLive,
            borderColor: "#3fb950",
            backgroundColor: "rgba(63, 185, 80, 0.12)",
            tension: 0.15,
            pointRadius,
            fill: true,
          }],
        },
        options: {
          ...chartDefaults,
          scales: {
            ...chartDefaults.scales,
            y: {
              ...chartDefaults.scales.y,
              title: { display: true, text: "Bytes in use", color: "#8b949e" },
              ticks: {
                color: "#8b949e",
                callback: (value) => formatBytes(value),
              },
            },
          },
        },
      });

      pagesChart = new Chart(document.getElementById("pages-chart"), {
        type: "line",
        data: {
          labels: firstLabel,
          datasets: [{
            label: "Active pages",
            data: firstPages,
            borderColor: "#d2a8ff",
            backgroundColor: "rgba(210, 168, 255, 0.12)",
            stepped: true,
            pointRadius,
            fill: true,
          }],
        },
        options: {
          ...chartDefaults,
          scales: {
            ...chartDefaults.scales,
            y: {
              ...chartDefaults.scales.y,
              title: { display: true, text: "Pages", color: "#8b949e" },
              ticks: { color: "#8b949e", stepSize: 1 },
            },
          },
        },
      });

      mappedChart = new Chart(document.getElementById("mapped-chart"), {
        type: "line",
        data: {
          labels: firstLabel,
          datasets: [{
            label: "Mapped bytes",
            data: firstMapped,
            borderColor: "#58a6ff",
            backgroundColor: "rgba(88, 166, 255, 0.12)",
            stepped: true,
            pointRadius,
            fill: true,
          }],
        },
        options: {
          ...chartDefaults,
          scales: {
            ...chartDefaults.scales,
            y: {
              ...chartDefaults.scales.y,
              title: { display: true, text: "Reserved from OS", color: "#8b949e" },
              ticks: {
                color: "#8b949e",
                callback: (value) => formatBytes(value),
              },
            },
          },
        },
      });
    }

    function loadWorkload(key) {
      stopPlay();
      const trace = TRACE_DATA[key];
      meta = trace.meta;
      samples = trace.samples;
      traceLabels = samples.map((s) => sampleLabel(s));
      fullLive = samples.map((s) => s.live_bytes);
      fullPages = samples.map((s) => s.active_pages);
      fullMapped = samples.map((s) => s.mapped_bytes);
      computePeaks();
      document.getElementById("scrub").max = String(Math.max(samples.length - 1, 0));
      updateStaticCards();
      buildLifecycleCharts();
      setLifecycleIndex(0);
    }

    function resizeLifecycleCharts() {
      if (liveChart) liveChart.resize();
      if (pagesChart) pagesChart.resize();
      if (mappedChart) mappedChart.resize();
    }

    function initLifecycle() {
      const workloadSelect = document.getElementById("workload-select");
      LIFECYCLE_ORDER.forEach((key) => {
        const opt = document.createElement("option");
        opt.value = key;
        opt.textContent = LIFECYCLE_LABELS[key] || key;
        workloadSelect.appendChild(opt);
      });

      workloadSelect.addEventListener("change", () => loadWorkload(workloadSelect.value));

      const scrub = document.getElementById("scrub");
      scrub.addEventListener("input", () => {
        stopPlay();
        setLifecycleIndex(Number(scrub.value));
      });

      document.getElementById("play-btn").addEventListener("click", () => {
        if (playTimer) {
          stopPlay();
          return;
        }
        document.getElementById("play-btn").textContent = "⏸ Pause";
        playTimer = setInterval(() => {
          let next = Number(scrub.value) + 1;
          if (next >= samples.length) {
            stopPlay();
            return;
          }
          setLifecycleIndex(next);
        }, 120);
      });

      document.getElementById("reset-btn").addEventListener("click", () => {
        stopPlay();
        setLifecycleIndex(0);
      });

      loadWorkload(LIFECYCLE_ORDER[0]);
      lifecycleInitialized = true;
    }"""
    else:
        lifecycle_script = """
    const HAS_TRACES = false;

    function initLifecycle() {}
    function resizeLifecycleCharts() {}"""

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Fixed-Block Allocator Dashboard</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.6/dist/chart.umd.min.js"></script>
  <style>
    :root {{
      --bg: #0f1419;
      --panel: #1a2332;
      --panel-border: #2a3544;
      --text: #e6edf3;
      --muted: #8b949e;
      --custom: #3fb950;
      --system: #f0883e;
      --accent: #58a6ff;
      --win: #3fb950;
      --lose: #f85149;
      --live: #3fb950;
      --mapped: #58a6ff;
      --pages: #d2a8ff;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      background: var(--bg);
      color: var(--text);
      line-height: 1.5;
    }}
    header {{
      padding: 2rem 1.5rem 1rem;
      border-bottom: 1px solid var(--panel-border);
      background: linear-gradient(180deg, #161b22 0%, var(--bg) 100%);
    }}
    header h1 {{ margin: 0 0 0.35rem; font-size: 1.5rem; font-weight: 600; }}
    header p {{ margin: 0 0 1rem; color: var(--muted); font-size: 0.95rem; }}
    .tabs {{
      display: flex;
      gap: 0.5rem;
    }}
    .tab {{
      background: transparent;
      color: var(--muted);
      border: 1px solid var(--panel-border);
      border-radius: 8px;
      padding: 0.45rem 1rem;
      font-weight: 600;
      cursor: pointer;
      font-size: 0.9rem;
    }}
    .tab:hover {{ color: var(--text); border-color: var(--muted); }}
    .tab.active {{
      background: var(--accent);
      color: #0d1117;
      border-color: var(--accent);
    }}
    main {{ max-width: 1100px; margin: 0 auto; padding: 1.5rem; }}
    .view {{ display: none; }}
    .view.active {{ display: block; }}
    .controls {{
      display: flex;
      flex-wrap: wrap;
      gap: 1rem;
      align-items: center;
      margin-bottom: 1.25rem;
    }}
    label {{ color: var(--muted); font-size: 0.85rem; }}
    select {{
      background: var(--panel);
      color: var(--text);
      border: 1px solid var(--panel-border);
      border-radius: 8px;
      padding: 0.5rem 0.75rem;
      font-size: 0.95rem;
    }}
    .cards {{
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 1rem;
      margin-bottom: 1.5rem;
    }}
    .card {{
      background: var(--panel);
      border: 1px solid var(--panel-border);
      border-radius: 12px;
      padding: 1rem 1.1rem;
    }}
    .card h3 {{
      margin: 0 0 0.75rem;
      font-size: 0.8rem;
      font-weight: 600;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: 0.04em;
    }}
    .metric {{ font-size: 1.75rem; font-weight: 700; }}
    .metric-sub {{ font-size: 0.85rem; color: var(--muted); margin-top: 0.25rem; }}
    .speedup-win {{ color: var(--win); }}
    .speedup-lose {{ color: var(--lose); }}
    .panel {{
      background: var(--panel);
      border: 1px solid var(--panel-border);
      border-radius: 12px;
      padding: 1.25rem;
      margin-bottom: 1.5rem;
    }}
    .panel h2 {{ margin: 0 0 1rem; font-size: 1rem; font-weight: 600; }}
    .empty-state p {{ margin: 0; color: var(--muted); }}
    .chart-wrap {{ position: relative; height: 340px; }}
    .chart-wrap-lifecycle {{ height: 320px; }}
    table {{
      width: 100%;
      border-collapse: collapse;
      font-size: 0.9rem;
    }}
    th, td {{
      text-align: left;
      padding: 0.55rem 0.65rem;
      border-bottom: 1px solid var(--panel-border);
    }}
    th {{ color: var(--muted); font-weight: 600; font-size: 0.8rem; text-transform: uppercase; }}
    td.num {{ font-variant-numeric: tabular-nums; }}
    .playback {{
      display: flex;
      flex-wrap: wrap;
      gap: 1rem;
      align-items: center;
      margin-bottom: 1.25rem;
      padding: 1rem 1.1rem;
      background: var(--panel);
      border: 1px solid var(--panel-border);
      border-radius: 12px;
    }}
    input[type="range"] {{ flex: 1; min-width: 180px; accent-color: var(--mapped); }}
    button.action {{
      background: var(--mapped);
      color: #0d1117;
      border: none;
      border-radius: 8px;
      padding: 0.45rem 1rem;
      font-weight: 600;
      cursor: pointer;
      font-size: 0.9rem;
    }}
    button.action:hover {{ filter: brightness(1.08); }}
    button.action.secondary {{
      background: transparent;
      color: var(--text);
      border: 1px solid var(--panel-border);
    }}
    .playback-label {{ color: var(--muted); font-size: 0.9rem; min-width: 140px; }}
    footer {{
      text-align: center;
      color: var(--muted);
      font-size: 0.8rem;
      padding: 2rem 1rem;
    }}
  </style>
</head>
<body>
  <header>
    <h1>Fixed-Block Allocator Dashboard</h1>
    <p id="header-subtitle">32-byte blocks · custom pool vs system malloc · lifecycle traces</p>
    <div class="tabs">
      <button type="button" class="tab active" data-view="benchmark">Benchmark</button>
      <button type="button" class="tab" data-view="lifecycle">Lifecycle</button>
    </div>
  </header>

  <main>
    <div id="view-benchmark" class="view active">
      <div class="controls">
        <div>
          <label for="bench-select">Scenario</label><br />
          <select id="bench-select"></select>
        </div>
      </div>

      <div class="cards">
        <div class="card">
          <h3>Custom allocator</h3>
          <div class="metric" id="card-custom">—</div>
          <div class="metric-sub" id="card-custom-sub">at peak scale</div>
        </div>
        <div class="card">
          <h3>System malloc</h3>
          <div class="metric" id="card-system">—</div>
          <div class="metric-sub" id="card-system-sub">at peak scale</div>
        </div>
        <div class="card">
          <h3>Speedup</h3>
          <div class="metric" id="card-speedup">—</div>
          <div class="metric-sub" id="card-speedup-sub">custom / system (&lt;1 is faster)</div>
        </div>
        <div class="card">
          <h3>Operations</h3>
          <div class="metric" id="card-ops">—</div>
          <div class="metric-sub">allocation count</div>
        </div>
      </div>

      <div class="panel">
        <h2>Time vs allocation count</h2>
        <div class="chart-wrap">
          <canvas id="line-chart"></canvas>
        </div>
      </div>

      <div class="panel">
        <h2>All scenarios at peak scale</h2>
        <table>
          <thead>
            <tr>
              <th>Scenario</th>
              <th>Custom (ms)</th>
              <th>System (ms)</th>
              <th>Ratio</th>
              <th>Winner</th>
            </tr>
          </thead>
          <tbody id="summary-body"></tbody>
        </table>
      </div>
    </div>

    <div id="view-lifecycle" class="view">{lifecycle_body}
    </div>
  </main>

  <footer>
    Regenerate with: <code>make dashboard</code>
  </footer>

  <script>
    const BENCHMARK_DATA = {benchmark_payload};
    const TRACE_DATA = {trace_payload};
    const BENCHMARK_LABELS = {benchmark_labels};
    const BENCHMARK_ORDER = {benchmark_order};
    const LIFECYCLE_LABELS = {lifecycle_labels};
    const LIFECYCLE_ORDER = {lifecycle_order};

    const DEFAULT_SUBTITLE =
      "32-byte blocks · custom pool vs system malloc · lifecycle traces";

    function showView(name) {{
      document.querySelectorAll(".view").forEach((el) => el.classList.remove("active"));
      document.querySelectorAll(".tab").forEach((el) => el.classList.remove("active"));
      document.getElementById("view-" + name).classList.add("active");
      document.querySelector('.tab[data-view="' + name + '"]').classList.add("active");

      if (name === "benchmark") {{
        document.getElementById("header-subtitle").textContent = DEFAULT_SUBTITLE;
        if (lineChart) lineChart.resize();
      }} else if (name === "lifecycle") {{
        if (HAS_TRACES && !lifecycleInitialized) initLifecycle();
        if (HAS_TRACES) resizeLifecycleCharts();
      }}
    }}

    document.querySelectorAll(".tab").forEach((btn) => {{
      btn.addEventListener("click", () => showView(btn.dataset.view));
    }});

    function formatOps(n) {{
      if (n >= 1_000_000) return (n / 1_000_000).toFixed(n % 1_000_000 === 0 ? 0 : 1) + "M";
      if (n >= 1_000) return (n / 1_000).toFixed(n % 1_000 === 0 ? 0 : 1) + "K";
      return String(n);
    }}

    function groupByBenchmark(rows) {{
      const map = {{}};
      for (const row of rows) {{
        if (!map[row.benchmark_type]) map[row.benchmark_type] = {{ custom: [], system: [] }};
        map[row.benchmark_type][row.allocator_type].push(row);
      }}
      for (const key of Object.keys(map)) {{
        map[key].custom.sort((a, b) => a.num_allocations - b.num_allocations);
        map[key].system.sort((a, b) => a.num_allocations - b.num_allocations);
      }}
      return map;
    }}

    function ratio(custom, system) {{
      if (system <= 0) return custom <= 0 ? 1 : Infinity;
      return custom / system;
    }}

    const grouped = groupByBenchmark(BENCHMARK_DATA);
    const benchKeys = BENCHMARK_ORDER.filter((key) => key in grouped);
    const benchSelect = document.getElementById("bench-select");
    benchKeys.forEach((key) => {{
      const opt = document.createElement("option");
      opt.value = key;
      opt.textContent = BENCHMARK_LABELS[key] || key.replace(/_/g, " ");
      benchSelect.appendChild(opt);
    }});

    let lineChart = null;

    function currentSeries() {{
      return grouped[benchSelect.value];
    }}

    function peakIndex(series) {{
      return series.custom.length - 1;
    }}

    function updateCards() {{
      const series = currentSeries();
      const index = peakIndex(series);
      const custom = series.custom[index];
      const system = series.system[index];
      const r = ratio(custom.time_ms, system.time_ms);

      document.getElementById("card-custom").textContent = custom.time_ms + " ms";
      document.getElementById("card-system").textContent = system.time_ms + " ms";
      document.getElementById("card-ops").textContent = formatOps(custom.num_allocations);

      const speedupEl = document.getElementById("card-speedup");
      if (!Number.isFinite(r)) {{
        speedupEl.textContent = "—";
        speedupEl.className = "metric";
      }} else {{
        speedupEl.textContent = r.toFixed(2) + "×";
        speedupEl.className = "metric " + (r < 1 ? "speedup-win" : "speedup-lose");
      }}

      const winner = r < 1 ? "Custom wins" : r > 1 ? "System wins" : "Tie";
      document.getElementById("card-speedup-sub").textContent = winner + " · ratio custom/system";
      document.getElementById("card-custom-sub").textContent = formatOps(custom.num_allocations) + " operations";
      document.getElementById("card-system-sub").textContent = formatOps(system.num_allocations) + " operations";
    }}

    function buildLineChart() {{
      const ctx = document.getElementById("line-chart");
      const series = currentSeries();
      const labels = series.custom.map((r) => formatOps(r.num_allocations));

      if (lineChart) lineChart.destroy();
      lineChart = new Chart(ctx, {{
        type: "line",
        data: {{
          labels,
          datasets: [
            {{
              label: "Custom",
              data: series.custom.map((r) => r.time_ms),
              borderColor: "#3fb950",
              backgroundColor: "rgba(63, 185, 80, 0.12)",
              tension: 0.2,
              pointRadius: 4,
            }},
            {{
              label: "System malloc",
              data: series.system.map((r) => r.time_ms),
              borderColor: "#f0883e",
              backgroundColor: "rgba(240, 136, 62, 0.12)",
              tension: 0.2,
              pointRadius: 4,
            }},
          ],
        }},
        options: {{
          responsive: true,
          maintainAspectRatio: false,
          plugins: {{
            legend: {{ labels: {{ color: "#e6edf3" }} }},
          }},
          scales: {{
            x: {{
              title: {{ display: true, text: "Allocations", color: "#8b949e" }},
              ticks: {{ color: "#8b949e" }},
              grid: {{ color: "#2a3544" }},
            }},
            y: {{
              title: {{ display: true, text: "Time (ms)", color: "#8b949e" }},
              ticks: {{ color: "#8b949e" }},
              grid: {{ color: "#2a3544" }},
              beginAtZero: true,
            }},
          }},
        }},
      }});
    }}

    function buildSummaryTable() {{
      const tbody = document.getElementById("summary-body");
      tbody.innerHTML = "";
      const maxOps = Math.max(...BENCHMARK_DATA.map((r) => r.num_allocations));

      benchKeys.forEach((key) => {{
        const custom = grouped[key].custom.find((r) => r.num_allocations === maxOps);
        const system = grouped[key].system.find((r) => r.num_allocations === maxOps);
        if (!custom || !system) return;
        const r = ratio(custom.time_ms, system.time_ms);
        const tr = document.createElement("tr");
        tr.innerHTML = `
          <td>${{BENCHMARK_LABELS[key] || key}}</td>
          <td class="num">${{custom.time_ms}}</td>
          <td class="num">${{system.time_ms}}</td>
          <td class="num">${{Number.isFinite(r) ? r.toFixed(2) + "×" : "—"}}</td>
          <td>${{r < 1 ? '<span class="speedup-win">Custom</span>' : r > 1 ? '<span class="speedup-lose">System</span>' : "Tie"}}</td>`;
        tbody.appendChild(tr);
      }});
    }}

    benchSelect.addEventListener("change", () => {{
      buildLineChart();
      updateCards();
    }});

    buildSummaryTable();
    buildLineChart();
    updateCards();
{lifecycle_script}
  </script>
</body>
</html>
"""


def main() -> None:
    rows = load_benchmark_rows()
    traces = load_lifecycle_traces()
    OUT_PATH.write_text(build_html(rows, traces), encoding="utf-8")

    parts = [f"{len(rows)} benchmark rows"]
    if traces:
        trace_parts = [
            f"{LIFECYCLE_LABELS[key]} ({len(traces[key]['samples'])} samples)"
            for key in LIFECYCLE_ORDER
            if key in traces
        ]
        parts.append("traces: " + ", ".join(trace_parts))
    else:
        parts.append("no lifecycle traces")

    print(f"Wrote {OUT_PATH} — {', '.join(parts)}")


if __name__ == "__main__":
    main()
