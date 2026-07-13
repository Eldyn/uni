import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";
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

		const currentFiles = new Set(
			Object.keys(bundle).map((fileName) => path.basename(fileName))
		);

		for (const entry of fs.readdirSync(assetsDir)) {
			if (!/\.(js|css)$/.test(entry)) continue;
			if (!currentFiles.has(entry)) {
				fs.unlinkSync(path.join(assetsDir, entry));
			}
		}
	}
};

const appVersion = fs.readFileSync(path.resolve("../VERSION"), "utf8").trim();

export default defineConfig(({ mode }) => {
	const isDev = mode === "development";
	const watch = isDev ? { watch: {} } : {};

	return {
		// Only run the watch plugin when running in development/watch mode
		plugins: [
				tailwindcss(),
				svelte(),
				isDev && watchPublicDirPlugin,
				isDev && pruneStaleChunksPlugin
			].filter(Boolean),
		resolve: {
			alias: {
				$lib: path.resolve("./src/lib"),
				$components: path.resolve("./src/lib/components"),
				$stores: path.resolve("./src/lib/stores"),
				$utils: path.resolve("./src/lib/utils"),
				$data: path.resolve("./src/lib/data")
			}
		},
		define: {
			__APP_VERSION__: JSON.stringify(appVersion)
		},
		base: "./",
		build: {
			outDir: "../public",
			emptyOutDir: true,
			minify: isDev ? false : "esbuild",
			sourcemap: isDev,
			...watch
		},
		test: {
			environment: "jsdom",
			globals: true,
			setupFiles: ["./src/lib/__tests__/setup.ts"],
			include: ["src/**/*.{test,spec}.{ts,svelte.ts}"]
		}
	};
});
