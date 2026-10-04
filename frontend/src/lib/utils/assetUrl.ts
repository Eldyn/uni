/**
 * @file assetUrl.ts
 * @brief Resolves a public asset path to its content-hashed URL.
 *
 * The build renames everything under public/assets/ to carry a content hash
 * (see hashPublicAssetsPlugin in vite.config.js) so the server can cache it
 * forever. Literal "/assets/..." strings are rewritten at build time; paths
 * assembled at runtime must go through here. Outside a production build the
 * manifest is empty and paths pass through untouched.
 */

declare const __PUBLIC_ASSET_MANIFEST__: Record<string, string>;

export const assetUrl = (path: string): string => __PUBLIC_ASSET_MANIFEST__[path] ?? path;
