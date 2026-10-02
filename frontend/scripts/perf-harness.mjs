#!/usr/bin/env node
/**
 * @file perf-harness.mjs
 * @brief Deterministic frame-time harness for the 3D game board.
 *
 * Drives the dev-match fixture (`?dev=match&players=16&intro=1&perf=1`) through
 * agent-browser (Chromium over CDP — no Playwright dependency), reads the
 * in-app `window.__uniPerf` probe after each run, and reports medians of the
 * `deal` cinematic's frame stats plus the renderer's draw-call workload.
 *
 * The workload is fixed by the fixture's seeded PRNG, so runs are comparable;
 * the absolute milliseconds are only meaningful run-to-run on the same machine
 * (headless Chromium may fall back to a software rasterizer). The deterministic
 * cross-machine signal is `renderer.info` (`calls`, `triangles`, `programs`).
 *
 * Usage (from `frontend/`):
 *   node scripts/perf-harness.mjs                    # build + serve + 3 runs
 *   node scripts/perf-harness.mjs --runs 5 --headed
 *   node scripts/perf-harness.mjs --no-build --url http://localhost:4173
 *   node scripts/perf-harness.mjs --out perf.json --json
 *
 * The probe only exists in a development-mode build (`__DEV_HARNESS__`), so this
 * script builds with `vite build --mode development` by default.
 */
import { spawn, spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const FRONTEND_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const VITE_BIN = path.join(FRONTEND_DIR, "node_modules", ".bin", "vite");
const SESSION = "uni-perf";
/** Harness builds into an isolated dir (via `UNI_OUT_DIR`) and serves it, so a
 *  running dev server's production bundle in `../public` is never clobbered. */
const PERF_OUT_DIR = ".perf-bundle";

const args = parseArgs(process.argv.slice(2));
const players = num(args.players, 16);
const hand = num(args.hand, 7);
const seed = num(args.seed, 1);
const runs = num(args.runs, 3);
const port = num(args.port, 4173);
const settleMs = num(args.settle, 1500);
const headed = flag(args.headed);
const json = flag(args.json);
const uncapped = flag(args.uncapped);

// `--uncapped` removes the display/vsync frame cap so rAF reflects real frame
// cost instead of a locked 60Hz; `--args` forwards arbitrary Chromium flags.
// Both go through AGENT_BROWSER_ARGS, which every agent-browser call inherits.
const extraArgs = [];
if (uncapped) extraArgs.push("--disable-frame-rate-limit", "--disable-gpu-vsync");
if (typeof args.args === "string" && args.args.length) extraArgs.push(...args.args.split(","));
if (extraArgs.length > 0) {
	process.env.AGENT_BROWSER_ARGS = [process.env.AGENT_BROWSER_ARGS, ...extraArgs]
		.filter(Boolean)
		.join(",");
}

let server = null;
let baseUrl = args.url ?? null;

try {
	if (!baseUrl) {
		if (!flag(args["no-build"])) build();
		server = serve(port);
		await waitForServer(`http://127.0.0.1:${port}/`);
		baseUrl = `http://127.0.0.1:${port}`;
	}

	const results = [];
	for (let i = 0; i < runs; i++) {
		process.stderr.write(`run ${i + 1}/${runs}…\n`);
		results.push(collect(baseUrl, i));
	}

	const report = summarize(results);
	if (json) process.stdout.write(JSON.stringify(report, null, 2) + "\n");
	else print(report);
	if (args.out) {
		const { writeFileSync } = await import("node:fs");
		writeFileSync(args.out, JSON.stringify(report, null, 2));
		process.stderr.write(`wrote ${args.out}\n`);
	}
} finally {
	if (server) server.kill("SIGTERM");
	// Leave the tab closed so repeat invocations start from a clean page.
	spawnSync("agent-browser", ["--session", SESSION, "close"], { stdio: "ignore" });
}

// ─── measurement ──────────────────────────────────────────────────────────────

function collect(base, index) {
	const url =
		`${base}/?dev=match&players=${players}&hand=${hand}&seed=${seed}` +
		`&intro=1&pointer=mouse&perf=1&run=${index}`;
	ab(["set", "viewport", "1280", "720"]);
	ab(["open", ...(headed ? ["--headed"] : []), url]);
	ab(
		[
			"wait",
			"--fn",
			"window.__uniPerf && window.__uniPerf.captures && !!window.__uniPerf.captures.deal"
		],
		{ allowFail: true, timeout: 45000 }
	);
	ab(["wait", String(settleMs)], { allowFail: true });
	return parseEval(ab(["eval", "window.__uniPerf"]));
}

function summarize(results) {
	const deal = results.map((r) => r.captures?.deal ?? null);
	const pick = (fn) => deal.map((d) => (d ? fn(d) : null)).filter((v) => v !== null);
	const stats = {
		frames: pick((d) => d.stats.frames),
		durationMs: pick((d) => d.stats.durationMs),
		avgMs: pick((d) => d.stats.avgMs),
		p50Ms: pick((d) => d.stats.p50Ms),
		p95Ms: pick((d) => d.stats.p95Ms),
		p99Ms: pick((d) => d.stats.p99Ms),
		maxMs: pick((d) => d.stats.maxMs),
		overBudget: pick((d) => d.stats.overBudget),
		fps: pick((d) => d.stats.fps),
		calls: pick((d) => d.render?.calls ?? null),
		triangles: pick((d) => d.render?.triangles ?? null),
		programs: pick((d) => d.render?.programs ?? null)
	};
	return {
		generatedAt: new Date().toISOString(),
		config: { players, hand, seed, runs, settleMs, headed, uncapped, url: baseUrl },
		ui: results[0]?.ui ?? null,
		stalls: results
			.flatMap((r) => r.longFrames ?? [])
			.sort((a, b) => b.durationMs - a.durationMs)
			.slice(0, 5),
		startup: results[0]?.startup ?? [],
		deal: {
			runs: results.map((r, i) => ({
				run: i,
				...(deal[i] ? describe(deal[i]) : { missing: true })
			})),
			median: medianOf(stats)
		},
		steadyFpsMedian: median(results.map((r) => r.steady?.fps ?? null).filter((v) => v !== null)),
		longTasksMedian: median(
			results.map((r) => r.longTasks?.count ?? null).filter((v) => v !== null)
		)
	};
}

function describe(capture) {
	return {
		p50Ms: capture.stats.p50Ms,
		p95Ms: capture.stats.p95Ms,
		p99Ms: capture.stats.p99Ms,
		maxMs: capture.stats.maxMs,
		avgMs: capture.stats.avgMs,
		fps: capture.stats.fps,
		overBudget: capture.stats.overBudget,
		frames: capture.stats.frames,
		durationMs: capture.stats.durationMs,
		calls: capture.render?.calls ?? null,
		triangles: capture.render?.triangles ?? null,
		programs: capture.render?.programs ?? null,
		longTasks: capture.longTasks
	};
}

// ─── output ───────────────────────────────────────────────────────────────────

function print(report) {
	const { config, ui, deal } = report;
	const lines = [];
	lines.push(
		`UNI perf harness — ${config.runs} run(s), ${config.players} players, hand ${config.hand}, seed ${config.seed}`
	);
	if (ui) {
		lines.push(
			`render config: viewport ${ui.viewport[0]}x${ui.viewport[1]} @${ui.dpr}x, tier=${ui.deviceTier}, ` +
				`hw=${ui.hardwareAccelerated}, reduced-motion=${ui.reducedMotion}, mode=${ui.cardRenderMode}, ` +
				`ripple=${ui.matRipple}, dust=${ui.ambientDust}, pile=${ui.pileThickness}`
		);
	}
	lines.push("");
	lines.push("deal cinematic (auto-bracketed window):");
	lines.push(
		"  run   p50    p95    p99    max    avg    fps    >16.7ms  frames  calls  tris    progs"
	);
	for (const r of deal.runs) {
		if (r.missing) {
			lines.push(`  ${String(r.run).padEnd(5)} (no deal capture — intro never ran?)`);
			continue;
		}
		lines.push(
			`  ${String(r.run).padEnd(5)} ${f(r.p50Ms).padStart(6)} ${f(r.p95Ms).padStart(6)} ` +
				`${f(r.p99Ms).padStart(6)} ${f(r.maxMs).padStart(6)} ${f(r.avgMs).padStart(6)} ` +
				`${f(r.fps).padStart(6)} ${String(r.overBudget).padStart(8)} ${String(r.frames).padStart(7)} ` +
				`${String(r.calls ?? "-").padStart(6)} ${String(r.triangles ?? "-").padStart(7)} ${String(r.programs ?? "-").padStart(6)}`
		);
	}
	const m = deal.median;
	lines.push(
		`  MED   ${f(m.p50Ms).padStart(6)} ${f(m.p95Ms).padStart(6)} ${f(m.p99Ms).padStart(6)} ` +
			`${f(m.maxMs).padStart(6)} ${f(m.avgMs).padStart(6)} ${f(m.fps).padStart(6)} ` +
			`${String(m.overBudget ?? "-").padStart(8)} ${String(m.frames ?? "-").padStart(7)} ` +
			`${String(m.calls ?? "-").padStart(6)} ${String(m.triangles ?? "-").padStart(7)} ${String(m.programs ?? "-").padStart(6)}`
	);
	lines.push("");
	lines.push(
		`steady-state fps (median): ${f(report.steadyFpsMedian)}   long tasks (median count): ${report.longTasksMedian ?? "-"}`
	);
	if (report.stalls?.length) {
		lines.push("");
		lines.push("longest stalls (LoAF: script / render / layout attribution):");
		for (const s of report.stalls.slice(0, 3)) {
			const top = (s.scripts ?? [])
				.slice(0, 3)
				.map(
					(x) =>
						`${x.functionName || x.name || "?"}@${x.source.split("/").pop() || "?"}:${x.char} ${x.durationMs}ms`
				)
				.join("; ");
			lines.push(
				`  ${f(s.durationMs)}ms  block ${f(s.blockingMs)}  render ${f(s.renderMs)}  layout ${f(s.styleLayoutMs)}  |  ${top || "(no script attribution)"}`
			);
		}
	}
	const slowStart = (report.startup ?? []).filter((s) => s.dtMs > 40);
	if (slowStart.length) {
		lines.push("");
		lines.push("slow startup frames (frame idx, dt, shader programs, textures):");
		for (const s of slowStart.slice(0, 10)) {
			lines.push(
				`  #${String(s.index).padStart(3)}  ${String(s.dtMs).padStart(7)}ms  programs ${String(s.programs).padStart(2)}  textures ${s.textures}`
			);
		}
	}
	process.stdout.write(lines.join("\n") + "\n");
}

function f(v) {
	return v === null || v === undefined ? "-" : Number(v).toFixed(2);
}

// ─── helpers ──────────────────────────────────────────────────────────────────

function ab(cmdArgs, { allowFail = false, timeout = 60000 } = {}) {
	const res = spawnSync("agent-browser", ["--session", SESSION, ...cmdArgs], {
		encoding: "utf8",
		timeout,
		maxBuffer: 64 * 1024 * 1024
	});
	if (res.error) {
		if (allowFail) return "";
		throw res.error;
	}
	if (res.status !== 0 && !allowFail) {
		throw new Error(
			`agent-browser ${cmdArgs.join(" ")} failed (${res.status}):\n${res.stderr || res.stdout}`
		);
	}
	return (res.stdout || "").trim();
}

function parseEval(raw) {
	const start = raw.indexOf("{");
	const end = raw.lastIndexOf("}");
	if (start === -1 || end === -1) {
		throw new Error(`no JSON object in eval output:\n${raw.slice(0, 300)}`);
	}
	return JSON.parse(raw.slice(start, end + 1));
}

function build() {
	// The build writes to ../public, which is what a running uni server serves.
	// This replaces that output with an unminified, harness-included dev bundle.
	process.stderr.write(
		"building dev bundle (vite build --mode development) -> ../public " +
			"(overwrites the served production bundle; re-run `npm run build` to restore)\n"
	);
	const res = spawnSync(VITE_BIN, ["build", "--mode", "development"], {
		cwd: FRONTEND_DIR,
		stdio: "inherit",
		env: { ...process.env, UNI_OUT_DIR: PERF_OUT_DIR }
	});
	if (res.status !== 0) throw new Error(`vite build failed (${res.status})`);
}

function serve(p) {
	if (!existsSync(VITE_BIN)) throw new Error(`vite not found at ${VITE_BIN}; run npm install`);
	const child = spawn(
		VITE_BIN,
		[
			"preview",
			"--outDir",
			PERF_OUT_DIR,
			"--port",
			String(p),
			"--strictPort",
			"--host",
			"127.0.0.1"
		],
		{
			cwd: FRONTEND_DIR,
			stdio: ["ignore", "inherit", "inherit"]
		}
	);
	return child;
}

async function waitForServer(url) {
	const deadline = Date.now() + 30000;
	for (;;) {
		try {
			const res = await fetch(url, { method: "HEAD" });
			if (res.ok || res.status === 404) return;
		} catch {
			// not up yet
		}
		if (Date.now() > deadline) throw new Error(`preview server did not come up at ${url}`);
		await new Promise((r) => setTimeout(r, 250));
	}
}

function median(values) {
	if (values.length === 0) return null;
	const sorted = [...values].sort((a, b) => a - b);
	const mid = Math.floor(sorted.length / 2);
	return sorted.length % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
}

function medianOf(byMetric) {
	const out = {};
	for (const [key, values] of Object.entries(byMetric)) out[key] = median(values);
	return out;
}

function parseArgs(argv) {
	const out = {};
	for (let i = 0; i < argv.length; i++) {
		const token = argv[i];
		if (!token.startsWith("--")) continue;
		const key = token.slice(2);
		const next = argv[i + 1];
		if (next === undefined || next.startsWith("--")) out[key] = true;
		else {
			out[key] = next;
			i++;
		}
	}
	return out;
}

function num(v, fallback) {
	const n = Number.parseInt(v, 10);
	return Number.isNaN(n) ? fallback : n;
}

function flag(v) {
	return v === true || v === "true" || v === "1";
}
