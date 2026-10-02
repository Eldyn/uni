import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";
import { paraglideVitePlugin } from "@inlang/paraglide-js";
import tailwindcss from "@tailwindcss/vite";
import fs from "node:fs";
import path from "node:path";

const watchPublicDirPlugin = {
	name: "watch-public-dir",
	buildStart() {
		const publicDir = path.resolve("public");
		if (fs.existsSync(publicDir)) {
			const addFilesRecursively = (dir) => {
				const entries = fs.readdirSync(dir, { withFileTypes: true });
				for (const entry of entries) {
					const fullPath = path.resolve(dir, entry.name);
					if (entry.isDirectory()) {
						addFilesRecursively(fullPath);
					} else {
						// Tells the bundler to watch this specific file for modifications
						this.addWatchFile(fullPath);
					}
				}
			};
			addFilesRecursively(publicDir);
		}
	}
};

// `vite build --watch` only runs emptyOutDir on the first build, so every
// subsequent rebuild leaves the previous run's content-hashed chunks behind.
// Prune stale JS/CSS chunks that no longer belong to the current bundle.
const pruneStaleChunksPlugin = {
	name: "prune-stale-chunks",
	writeBundle(options, bundle) {
		const outDir = options.dir ?? path.resolve("../public");
		const assetsDir = path.join(outDir, "assets");
		if (!fs.existsSync(assetsDir)) return;

		const currentFiles = new Set(Object.keys(bundle).map((fileName) => path.basename(fileName)));

		for (const entry of fs.readdirSync(assetsDir)) {
			if (!/\.(js|css)$/.test(entry)) continue;
			if (!currentFiles.has(entry)) {
				fs.unlinkSync(path.join(assetsDir, entry));
			}
		}
	}
};

// Overridable so the perf harness can build into an isolated dir and serve it
// without clobbering the production bundle a running server is serving.
const OUT_DIR = process.env.UNI_OUT_DIR || "../public";

// The server ships a ".gz" sidecar whenever the client accepts gzip, so the
// three.js/Threlte bundle goes over the wire compressed without costing any
// per-request CPU. Runs in closeBundle, after pruneStaleChunksPlugin has
// removed the chunks whose sidecars would otherwise be left orphaned.
const gzipAssetsPlugin = {
	name: "gzip-assets",
	async closeBundle() {
		const { gzipAssets, formatGzipSummary } = await import("./scripts/gzip-assets.js");
		this.info(formatGzipSummary(gzipAssets(path.resolve(OUT_DIR))));
	}
};

const appVersion = fs.readFileSync(path.resolve("../VERSION"), "utf8").trim();

export default defineConfig(({ mode }) => {
	const isDev = mode === "development";
	// Keyed on the flag rather than the mode, so a development build can also be
	// run one-shot (`vite build --mode development`) to produce a dev bundle for
	// the screenshot harness without leaving a watcher behind.
	const watch = process.argv.includes("--watch") ? { watch: {} } : {};

	return {
		// Only run the watch plugin when running in development/watch mode
		plugins: [
			paraglideVitePlugin({
				project: "./project.inlang",
				outdir: "./src/lib/paraglide",
				// Cookie stays first (canonical default order keeps baseLocale as
				// the final fallback); localStorage slots in right after so a
				// locale chosen when cookies are blocked still survives reloads,
				// without changing precedence for browsers that support both.
				strategy: ["cookie", "localStorage", "globalVariable", "baseLocale"]
			}),
			tailwindcss(),
			svelte(),
			isDev && watchPublicDirPlugin,
			isDev && pruneStaleChunksPlugin,
			gzipAssetsPlugin
		].filter(Boolean),
		resolve: {
			alias: {
				$lib: path.resolve("./src/lib"),
				$components: path.resolve("./src/lib/components"),
				$stores: path.resolve("./src/lib/stores"),
				$utils: path.resolve("./src/lib/utils"),
				$data: path.resolve("./src/lib/data")
			},
			// Svelte's package.json exports a separate server build behind the
			// default/node condition; under vitest's Node runtime that resolves
			// ahead of "browser" unless forced, so a component test that mounts a
			// component (via @testing-library/svelte) fails with "mount(...) is
			// not available on the server". Vitest runs this config with
			// mode "test" (never "development"/plain build), so this only ever
			// affects test resolution, never the production/watch build.
			conditions: mode === "test" ? ["browser"] : undefined
		},
		define: {
			__APP_VERSION__: JSON.stringify(appVersion),
			// Gate for the local screenshot harness (src/lib/dev). A literal
			// `false` in a production build, so the bundler drops the guarded
			// dynamic import and the harness never ships. `import.meta.env.DEV`
			// cannot be used here: it is false for *any* `vite build`, including
			// `--mode development`.
			__DEV_HARNESS__: JSON.stringify(isDev),
			// Dev-only content (e.g. the freestyle deck entry) is kept out of the
			// production bundle but stays available in dev and under test.
			__DEV_CONTENT__: JSON.stringify(isDev || mode === "test")
		},
		base: "/",
		build: {
			outDir: OUT_DIR,
			emptyOutDir: true,
			minify: isDev ? false : "esbuild",
			sourcemap: isDev,
			...watch
		},
		test: {
			environment: "jsdom",
			// Components' scoped <style> blocks are emitted as separate CSS
			// modules; without this, Vitest stubs those imports to empty and
			// no component styles ever reach jsdom's getComputedStyle.
			css: true,
			globals: true,
			setupFiles: ["./src/lib/__tests__/setup.ts"],
			include: ["src/**/*.{test,spec}.{ts,svelte.ts}"]
		}
	};
});
