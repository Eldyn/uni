# Avatar catalogue

`avatar/catalogue/` holds the avatar catalogue: one JSON file for each element.
The client bundles the folder at build time through the Vite module
`virtual:avatar-catalogue`. The server reads the same folder at start. The env
var `UNI_AVATAR_CATALOGUE` names the folder; the default is `avatar/catalogue`.
`frontend/src/lib/__tests__/avatar` validates the merged document in CI
(`npm test`), including ids, asset files and PNG sizes.

To find an element, run fzf over the folder:
`find avatar/catalogue -name '*.json' | fzf`.

## Layout

| Path | Content |
| --- | --- |
| `meta.json` | `version` and `featured` (the shop shelf). |
| `sockets/<stem>.json` | One shape socket table: `shape`, `eyes`, `mouth`, `head_top` and `body_center`. |
| `palettes/<stem>.json` | One palette: `id`, `order`, `name`, `price`, `starter` and `swatches`. |
| `items/<slot>/<stem>.json` | One item: `id`, `order`, `slot` and the item fields. |

`<stem>` is the id with each `:` replaced by `_`: `uni:set_astronaut` becomes
`uni_set_astronaut.json`. A file name never holds a colon. The id inside the
file is the authority.

The slots are code, not files: `shape`, `eyes`, `mouth`, `head_top`, `mask`,
`bodywear`, `set` and `motif`. The folder name of an item must equal its
`slot`.

## Merge rules

The server (`Catalogue::MergeDirectory`) and the client build
(`frontend/scripts/lib/avatarCatalogue.mjs`) merge the folder with the same
nine rules. Change both together.

1. Read every `*.json` file under the folder, recursively. Ignore other files.
   Sort the relative paths as plain byte strings.
2. `meta.json` must exist once. It must be an object with only `version` and
   `featured`.
3. A file must sit at `meta.json`, `sockets/<file>.json`,
   `palettes/<file>.json` or `items/<slot>/<file>.json`. Any other path is an
   error: `unexpected file '<path>'`.
4. Every file must be a JSON object. A socket file needs `shape`. A palette or
   item file needs `id` and an integer `order`.
5. The file stem must equal the id with `:` replaced by `_` (`shape` for a
   socket file).
6. An item file in folder `<slot>` must have `"slot": "<slot>"`, and the slot
   must be one of the eight known slots.
7. A duplicate id (item, palette or shape socket table) is an error that names
   both file paths. A duplicate swatch id across palettes is an error from the
   parser.
8. Palettes sort by `order`. Items sort by slot rank in the fixed slot list,
   then by `order`. `order` must be unique per slot for items and unique over
   all palettes. The first starter of a slot (lowest `order`) is the slot
   default. Use steps of 10 (10, 20, 30) so an item can go between two others.
9. The merge removes `order` from each item and palette, and `shape` from each
   socket table. `shape_sockets` is keyed by the shape id.

## Add an item

1. Create `avatar/catalogue/items/<slot>/<stem>.json` with `id`, `order`,
   `slot`, `name`, `asset`, `regions`, `rarity` and `price`. Add the optional
   fields the item needs (`starter`, `default_colors`, `tint`, `masks` and so
   on).
2. Put the art under `frontend/public/assets/avatar/<slot>/`.
3. Run `node frontend/scripts/avatar-placeholders.mjs` to write the missing
   placeholders.
4. Run the avatar tests from `frontend/`:
   `npx vitest run src/lib/__tests__/avatar`.

## Tint regions

An item has at most 6 tint regions. The art file (`asset.path`) carries
regions 1-3 in its R, G and B channels. For regions 4-6 list ONE extra mask
file in `asset.masks`, for example `"masks": ["motif/uni_x_b.png"]`, and set
`regions` to 4, 5 or 6. The extra file has the same size as the art file. Its
R, G and B channels mark regions 4, 5 and 6. Use the same pixel rules as the
art file: alpha 0 is clear, one non-zero channel is a region (the value is the
shade), anything else is invalid. The extra file only adds regions; the shape
comes from the art file, and grey pixels in the extra file are ignored. The
tests reject `regions` above `3 * (1 + masks.length)`.

## Spelling

JSON keys and values use US English: `body_center`, `color`.

## Exporting art

Author each item as a `.aseprite` source and export it with:

```
node frontend/scripts/avatar-export.mjs <src-dir> frontend/public/assets/avatar/<slot>
```

The script wraps `aseprite -b file.aseprite --sheet strip.png --sheet-type
horizontal`, checks every output is 480x96 and exits non-zero on a problem.
`--check-only <dir>` validates the PNGs already there without running Aseprite.
