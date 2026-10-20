import { blit, clipToAlpha, createImage, type PixelImage } from "./pixels";
import type { ResolvedLayer } from "./slotRules";
import { frameOffset } from "./sockets";
import { tintImage } from "./tint";
import { FRAME_COUNT, FRAME_SIZE, STRIP_WIDTH } from "./types";

export type ImageLookup = (assetPath: string) => PixelImage | undefined;

/**
 * Bake resolved layers into one 480x96 strip. Each frame redraws every layer
 * back to front: a strip layer contributes only that frame, a still layer is
 * reused for all frames at its socket plus bob. A missing image skips its
 * layer. The motif layer is clipped to the shape layer's alpha, so a full-body
 * motif texture only shows on the body. INFO: every layer is clipped to its own
 * frame cell, so art shifted past a frame edge never bleeds into the
 * neighbouring frame.
 */
export function bakeLoadoutPixels(
	layers: ResolvedLayer[],
	shapeId: string,
	images: ImageLookup
): PixelImage {
	const out = createImage(STRIP_WIDTH, FRAME_SIZE);
	const shapeFrames = new Map<number, PixelImage>();

	for (const layer of layers) {
		const source = images(layer.item.asset.path);
		if (!source) continue;

		const extraMasks = (layer.item.asset.masks ?? []).map((path) => images(path));
		const tinted = tintImage(source, layer.colours, extraMasks, layer.item.tint ?? "mask");
		const strip = layer.item.asset.kind === "strip";

		for (let frame = 0; frame < FRAME_COUNT; frame++) {
			const [offsetX, offsetY] = frameOffset(shapeId, layer.item, frame);
			const srcFrame = strip
				? { x: frame * FRAME_SIZE, y: 0, w: FRAME_SIZE, h: FRAME_SIZE }
				: { x: 0, y: 0, w: FRAME_SIZE, h: FRAME_SIZE };

			const cell = createImage(FRAME_SIZE, FRAME_SIZE);
			blit(cell, tinted, srcFrame, offsetX, offsetY);

			if (layer.item.slot === "shape") {
				shapeFrames.set(frame, {
					width: cell.width,
					height: cell.height,
					data: new Uint8ClampedArray(cell.data)
				});
			}

			if (layer.item.slot === "motif") {
				const shapeFrame = shapeFrames.get(frame);
				if (shapeFrame) clipToAlpha(cell, shapeFrame);
			}

			blit(out, cell, { x: 0, y: 0, w: FRAME_SIZE, h: FRAME_SIZE }, frame * FRAME_SIZE, 0);
		}
	}

	return out;
}
