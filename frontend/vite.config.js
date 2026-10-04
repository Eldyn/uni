import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";
import { paraglideVitePlugin } from "@inlang/paraglide-js";
import tailwindcss from "@tailwindcss/vite";
import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";

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

// Files under public/assets/ are copied verbatim, so their names never change
// while the server marks everything under /assets/ as immutable for a year.
// Each one is renamed to carry its content hash (like Vite's own chunks) so an
// edited file becomes a new URL. Files referenced from outside the bundle
// (the web manifest, link-preview meta tags) keep a stable name.
const PUBLIC_ASSETS_DIR = path.resolve("public/assets");
const STABLE_PUBLIC_ASSETS = new Set(["logo.png", "link_image.png"]);
const CONTENT_HASH_LENGTH = 8;

function buildPublicAssetManifest() {
	const manifest = {};
	const walk = (directory) => {
		if (!fs.existsSync(directory)) return;
		for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
			const fullPath = path.join(directory, entry.name);
			if (entry.isDirectory()) {
				walk(fullPath);
				continue;
			}
			const relativePath = path.relative(PUBLIC_ASSETS_DIR, fullPath).split(path.sep).join("/");
			if (entry.name.endsWith(".ase") || STABLE_PUBLIC_ASSETS.has(relativePath)) continue;

			const contentHash = createHash("sha256")
				.update(fs.readFileSync(fullPath))
				.digest("hex")
				.slice(0, CONTENT_HASH_LENGTH);
			const { dir, name, ext } = path.posix.parse(relativePath);
			const hashedName = `${name}-${contentHash}${ext}`;
			manifest[`/assets/${relativePath}`] = `/assets/${dir ? `${dir}/` : ""}${hashedName}`;
		}
	};
	walk(PUBLIC_ASSETS_DIR);
	return manifest;
}

// Quoted or url()-wrapped literals are rewritten at transform time; paths built
// at runtime go through assetUrl() (src/lib/utils/assetUrl.ts) instead.
function hashPublicAssetsPlugin(manifest) {
	const literalPath = /(?<=["'`(])\/assets\/[^"'`)\s$]+(?=["'`)])/g;
	return {
		name: "hash-public-assets",
		enforce: "pre",
		transform(code, id) {
			if (id.includes("node_modules") || !code.includes("/assets/")) return null;
			const rewritten = code.replace(literalPath, (match) => manifest[match] ?? match);
			return rewritten === code ? null : { code: rewritten, map: null };
		},
		closeBundle() {
			const outDir = path.resolve(OUT_DIR);
			for (const [original, hashed] of Object.entries(manifest)) {
				const source = path.join(outDir, original);
				if (fs.existsSync(source)) fs.renameSync(source, path.join(outDir, hashed));
			}
		}
	};
}

// Aseprite sources live beside the exported art in public/ (several MB), so
// the build copies them into the served bundle and the Docker image where
// nothing ever requests them. Dropped from the output only; sources stay put.
const SOURCE_ART_EXTENSION = ".ase";

const pruneSourceArtPlugin = {
	name: "prune-source-art",
	closeBundle() {
		const pruneDirectory = (directory) => {
			if (!fs.existsSync(directory)) return;
			for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
				const fullPath = path.join(directory, entry.name);
				if (entry.isDirectory()) pruneDirectory(fullPath);
				else if (entry.name.endsWith(SOURCE_ART_EXTENSION)) fs.unlinkSync(fullPath);
			}
		};
		pruneDirectory(path.resolve(OUT_DIR));
	}
};

// three's DRACOLoader and KTX2Loader reference their ~1 MB wasm decoders with
// top-level `new URL(..., import.meta.url)`, which makes the bundler emit the
// files even though @threlte/extras only reaches the loaders through its
// barrel and nothing here ever loads a glTF model or compressed texture. The
// URLs are blanked so the decoders are never bundled. If a glTF/KTX2 loader is
// ever used, remove this plugin.
const THREE_DECODER_URL =
	/new URL\(\s*'\.\.\/libs\/(?:draco|basis)\/[^']*',\s*import\.meta\.url\s*\)\.toString\(\)/g;

const skipThreeDecodersPlugin = {
	name: "skip-three-decoders",
	enforce: "pre",
	transform(code, id) {
		if (!/three\/examples\/jsm\/loaders\/(DRACO|KTX2)Loader\.js$/.test(id)) return null;
		const rewritten = code.replace(THREE_DECODER_URL, '""');
		return rewritten === code ? null : { code: rewritten, map: null };
	}
};

// pixelarticons' stylesheet lists eot, woff, ttf and svg fallbacks next to the
// woff2, and the bundler emits every one of them (about 1.3 MB) although every
// current browser takes the woff2 and never requests the rest. The fallbacks
// are cut from the @font-face so only the woff2 is built.
const ICON_FONT_STYLESHEET = /pixelarticons\/fonts\/pixelart-icons-font\.css$/;

const woff2OnlyIconFontPlugin = {
	name: "woff2-only-icon-font",
	enforce: "pre",
	transform(code, id) {
		if (!ICON_FONT_STYLESHEET.test(id)) return null;
		const fontFace = /@font-face\s*\{[^}]*\}/;
		const woff2Source = code.match(/url\('[^']*\.woff2[^']*'\)\s*format\('woff2'\)/)?.[0];
		if (!woff2Source) return null;
		return {
			code: code.replace(
				fontFace,
				`@font-face {\n  font-family: "pixelart-icons-font";\n  src: ${woff2Source};\n}`
			),
			map: null
		};
	}
};

// The server ships a ".br"/".gz" sidecar whenever the client accepts one, so the
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

const baseLocale = JSON.parse(
	fs.readFileSync(path.resolve("project.inlang/settings.json"), "utf8")
).baseLocale;

// Paraglide's locale-modules index statically imports every locale, which
// bundles all translations into the entry. Rewrites each non-base import into
// a mutable binding that starts as the base locale and is swapped for the real
// module by loadLocale(), so a locale's chunk is only fetched when it's used.
// Anything rendered before its locale loads falls back to the base language.
const lazyLocalesPlugin = {
	name: "lazy-locales",
	enforce: "pre",
	transform(code, id) {
		if (!id.endsWith("paraglide/messages/_index.js")) return null;

		const localeImport = /^import \* as (__\w+) from "\.\/([\w-]+)\.js"$/gm;
		const lazyLocales = [];
		let baseBinding = "";

		const rewritten = code.replace(localeImport, (statement, binding, locale) => {
			if (locale === baseLocale) {
				baseBinding = binding;
				return statement;
			}
			lazyLocales.push({ binding, locale });
			return "";
		});

		const bindings = lazyLocales.map(({ binding }) => `let ${binding} = ${baseBinding}`).join("\n");
		const loaders = lazyLocales
			.map(
				({ binding, locale }) =>
					`\t${JSON.stringify(locale)}: async () => { ${binding} = await import("./${locale}.js") }`
			)
			.join(",\n");

		return `${rewritten}
${bindings}

const localeLoaders = {
${loaders}
}
const loadedLocales = new Set([${JSON.stringify(baseLocale)}])

export const isLocaleLoaded = (locale) => loadedLocales.has(locale)

export async function loadLocale(locale) {
	if (loadedLocales.has(locale)) return
	await localeLoaders[locale]()
	loadedLocales.add(locale)
}
`;
	}
};

const appVersion = fs.readFileSync(path.resolve("../VERSION"), "utf8").trim();

export default defineConfig(({ mode, command }) => {
	const isDev = mode === "development";
	// Only real builds rename files; tests and the dev server keep plain paths.
	const publicAssetManifest =
		command === "build" && mode !== "test" ? buildPublicAssetManifest() : {};
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
				// One module per locale instead of one per message, so lazyLocalesPlugin
				// can split each locale into its own on-demand chunk.
				outputStructure: "locale-modules",
				// Cookie stays first (canonical default order keeps baseLocale as
				// the final fallback); localStorage slots in right after so a
				// locale chosen when cookies are blocked still survives reloads,
				// without changing precedence for browsers that support both.
				strategy: ["cookie", "localStorage", "globalVariable", "baseLocale"]
			}),
			lazyLocalesPlugin,
			hashPublicAssetsPlugin(publicAssetManifest),
			tailwindcss(),
			svelte(),
			isDev && watchPublicDirPlugin,
			isDev && pruneStaleChunksPlugin,
			woff2OnlyIconFontPlugin,
			skipThreeDecodersPlugin,
			pruneSourceArtPlugin,
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
			__PUBLIC_ASSET_MANIFEST__: JSON.stringify(publicAssetManifest),
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
