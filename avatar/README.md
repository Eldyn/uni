# Avatar catalogue

`catalogue.json` is the single source of truth for avatar items, palettes and
the featured shop shelf. The client reads it through the `$avatar` alias; the
server loads the same file at start. `frontend/src/lib/__tests__/avatar`
validates it in CI (`npm test`), including ids, asset files and PNG sizes.

To add an item, drop the art under `frontend/public/assets/avatar/<slot>/` and
add an entry to `items` with a namespaced `id`, its `slot`, an `asset` (`strip`
is 480x96; `still` is 96x96 plus a five-entry `bob`), `regions`, `rarity` and
`price`, then run the tests. Missing placeholders are written by
`node frontend/scripts/avatar-placeholders.mjs`.

An item has at most 6 tint regions. The art file (`asset.path`) carries
regions 1-3 in its R, G and B channels. For regions 4-6 list ONE extra mask
file in `asset.masks`, for example `"masks": ["motif/uni_x_b.png"]`, and set
`regions` to 4, 5 or 6. The extra file has the same size as the art file. Its
R, G and B channels mark regions 4, 5 and 6. Use the same pixel rules as the
art file: alpha 0 is clear, one non-zero channel is a region (the value is the
shade), anything else is invalid. The extra file only adds regions; the shape
comes from the art file, and grey pixels in the extra file are ignored. The
tests reject `regions` above `3 * (1 + masks.length)`.

## Exporting art

Author each item as a `.aseprite` source and export it with:

```
node frontend/scripts/avatar-export.mjs <src-dir> frontend/public/assets/avatar/<slot>
```

The script wraps `aseprite -b file.aseprite --sheet strip.png --sheet-type
horizontal`, checks every output is 480x96 and exits non-zero on a problem.
`--check-only <dir>` validates the PNGs already there without running Aseprite.
