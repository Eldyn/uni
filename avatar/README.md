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

## Exporting art

Author each item as a `.aseprite` source and export it with:

```
node frontend/scripts/avatar-export.mjs <src-dir> frontend/public/assets/avatar/<slot>
```

The script wraps `aseprite -b file.aseprite --sheet strip.png --sheet-type
horizontal`, checks every output is 480x96 and exits non-zero on a problem.
`--check-only <dir>` validates the PNGs already there without running Aseprite.
