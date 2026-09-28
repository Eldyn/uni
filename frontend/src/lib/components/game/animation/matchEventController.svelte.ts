/**
 * @file matchEventController.svelte.ts
 * @brief Packet-driven animation beat controller — the replacement for the
 * state-diff watcher in baseBeats.svelte.ts.
 *
 * The server's `match_event` stream is the animation trigger: beats are
 * buffered by the store and drained after the next snapshot is applied
 * so `handle` always reads an already-reconciled
 * `storeGame.state`. The controller handles play, draw and reshuffle beats
 * (`turn`/`toast` beats are no-ops here). It does not subscribe to `ws` itself
 * — it is fed through `subscribeBeats`.
 */

import { storeGame, type CardType } from "$stores/game.svelte";
import { CARD_COLOR_MAP } from "$lib/palette";
import { storeSpectator } from "$stores/spectator.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeRenderSettings } from "$stores/renderSettings.svelte";
import { storeTableSpin } from "$stores/tableSpin.svelte";
import { storeTurnSkip, SKIP_MARK_DURATION_MS } from "$stores/turnSkip.svelte";
import { storeMatchIntro } from "$stores/matchIntro.svelte";
import type { CardBus } from "../card-bus.svelte";
import { cardMetaFrom, type CardRegistry } from "./cardRegistry.svelte";
import { anchorWithBoardRotation } from "./cardBoardPose";
import { handSlotPose } from "../layout/handSlotPose";
import type { BoardPlacement } from "../layout/boardPlacement";
import { resolvePovPlayer } from "../layout/spectatorPov";
import { isChoosePlayerTarget } from "../prompts/promptTargets";
import { drawPileTopPose, MAX_DRAW_PILE_STACK, PILE_BASE_HEIGHT } from "../layout/drawPile";
import {
	DISCARD_CAP,
	discardCardOffset,
	discardStackZ,
	previewDiscardLanding,
	type DiscardEntry
} from "../layout/discardPile";
import {
	buildDrawBeats,
	buildPlayBeat,
	buildReshuffleBeat,
	DRAW_HOVER_LIFT,
	localHandSlotAnchor,
	opponentSeatAnchor,
	opponentSlotAnchorKey,
	type MatchEventBeat
} from "./baseBeats.svelte";
import type { AnimationBeat } from "./types";
import { storeMatRipple, MAT_INITIAL_COLOR } from "../three/ripple/matRipple.svelte";
import type { MatUv } from "../three/ripple/matRipple.svelte";
import { originToMatUv, maxRadiusUv } from "../three/ripple/ripplePlan";

type HandSnapshot = { orderIds: number[]; scrollEm: number; maxHalfSpanEm: number };

export function createMatchEventBeatController(deps: {
	bus: CardBus;
	cardRegistry: CardRegistry;
	getPlacement: () => BoardPlacement;
	getOpponentSeatAnchor: (username: string) => [number, number, number];
	getOpponentCardScale?: () => number;
	getOpponentSeatRotationDeg?: (username: string) => number;
	getOpponentCardPose?: (
		username: string,
		cardCount: number,
		slotIndex: number
	) => { position: [number, number, number]; spinDeg: number };
	getOpponentFrontPose?: (username: string) => {
		position: [number, number, number];
		spinDeg: number;
	};
	/** storeGame.onMatchEventBeat — the store buffers beats and drains them
	 *  after the next snapshot is applied (the client). */
	subscribeBeats: (cb: (beat: MatchEventBeat) => void) => () => void;
}): { dispose: () => void; syncState: () => void; resetPendingWildRipple: () => void } {
	let lastLandingBaseDeg = 0;
	// A wild landing whose colour choice hasn't arrived yet (active_type still
	// reads "white" — Playmat3D's own comment on the same field): the ripple's
	// geometry is fixed at landing, but it can't start until syncState sees the
	// real colour land in a later snapshot.
	let pendingWildRipple: { originUv: MatUv; maxRadius: number } | null = null;

	/** Drops any stale pending wild ripple without firing it. Callers must
	 *  invoke this on a fresh match_start (see GameBoard.svelte's
	 *  matchIntroPending-consuming effect): the controller is created once and
	 *  reused across matches on the same mounted GameBoard, and a wild whose
	 *  colour never resolved before the match ended would otherwise fire a
	 *  ripple with the previous match's geometry once syncState next observes
	 *  a real active_type. */
	function resetPendingWildRipple(): void {
		pendingWildRipple = null;
	}
	// Tracks storeGame.isActionPending across snapshots to detect a play
	// request's round-trip finishing (success OR server rejection) — see the
	// drag-play recovery check in syncState.
	let prevIsActionPending = false;
	// Monotonic id source for synthetic opponent-draw cards. The wire packet
	// only carries ids for the owner, so non-owner viewers get a
	// client-local id and never learn the real card.
	let drawIdCounter = 0;
	const pendingOpponentSlots = new Map<string, [number, number, number]>();
	// Ids of local-hand cards currently mid-flight in a multi-card draw, used
	// to live-refresh their `dimmed` decoration as the turn changes.
	const localDrawFlightIds = new Set<string>();
	// Synthetic opponent-draw card id -> owning username, refreshed live by
	// syncState so a turn/prompt change mid-flight re-dims correctly.
	const opponentDrawFlightOwners = new Map<string, string>();
	// Previous snapshot's draw-pile size. The reshuffle packet carries only the
	// new size, so the amount swept back is derived from this. syncState
	// updates it AFTER the store drains the current snapshot's beats
	//so `handle` still sees the pre-reshuffle value.
	let prevDrawPileSize: number | null = null;

	/** The POV player's username. "local" means the player whose POV is
	 *  rendered (the viewed player while spectating), matching Scene3D and the
	 *  watcher. */
	function resolveLocalUsername(state: NonNullable<typeof storeGame.state>): string | undefined {
		return resolvePovPlayer(
			storeGame.localPlayer,
			storeGame.isSpectator,
			state.players ?? [],
			storeSpectator.viewedUsername,
			state.current_turn
		)?.username;
	}

	/** Cards LocalHand3D is showing for `username` right now — the same
	 *  pending-draw/play filter its row applies. */
	function shownLocalCardCount(username: string): number {
		const hand = storeGame.state?.players?.find((p) => p.username === username)?.hand ?? [];
		return hand.filter(
			(c) => c.id !== deps.bus.pendingLocalPlayDrawnId && !deps.bus.pendingLocalDrawIds.has(c.id)
		).length;
	}

	/** One live resolver per beat, shared by every enqueue() call — a function
	 *  evaluated when each step renderer asks for the anchor, not a value
	 *  snapshotted at enqueue() time. */
	function resolveCardTarget(name: string): [number, number, number] {
		const state = storeGame.state;
		if (!state) throw new Error(`matchEventController: state cleared for anchor "${name}"`);
		const placement = deps.getPlacement();
		// INFO: table-bound anchors (piles, seats) are measured in the board's
		//       canonical, unrotated frame. A spectator spin turns the rendered
		//       board, so a flight landing on one of these must be folded through
		//       the same yaw. Hand-row anchors are the viewer's own UI and stay
		//       unrotated.
		const yaw = storeTableSpin.boardRotationY;
		if (name === "discard-pile") {
			const { entry, z } = previewDiscardLanding(
				deps.bus.discardHistory,
				state.top_card!,
				DISCARD_CAP,
				lastLandingBaseDeg
			);
			const offset = discardCardOffset(
				placement.discardX,
				placement.discardZ,
				entry,
				placement.centerScale
			);
			return anchorWithBoardRotation([offset.x, z, offset.z], yaw);
		}
		if (name === "draw-pile") {
			return anchorWithBoardRotation(
				[placement.drawPileX, PILE_BASE_HEIGHT, placement.drawPileZ],
				yaw
			);
		}
		if (name.startsWith("seat:")) {
			return anchorWithBoardRotation(deps.getOpponentSeatAnchor(name.slice("seat:".length)), yaw);
		}
		if (name.startsWith("opponent-front:")) {
			const username = name.slice("opponent-front:".length);
			const front = deps.getOpponentFrontPose
				? deps.getOpponentFrontPose(username).position
				: deps.getOpponentSeatAnchor(username);
			return anchorWithBoardRotation(front, yaw);
		}
		if (name.startsWith("opponent-slot:")) {
			return anchorWithBoardRotation(pendingOpponentSlots.get(name) ?? [0, 0, 0], yaw);
		}
		throw new Error(`matchEventController: no resolver for anchor "${name}"`);
	}

	/** Ports the watcher's `processPlay` (baseBeats.svelte.ts:478-633): the
	 *  beat's player replaces `last_play`, and post-event state (R
	 *  guarantees the snapshot is applied) supplies the top card. */
	function handlePlay(beat: Extract<MatchEventBeat, { kind: "play" }>): void {
		const state = storeGame.state;
		if (!state) return;
		const top = state.top_card;
		if (!top) return;
		if (String(top.id) !== String(beat.cardId)) return;

		const placement = deps.getPlacement();
		const localUsername = resolveLocalUsername(state);

		const playedByMe = beat.player === localUsername;
		const landingBaseDeg = playedByMe
			? 0
			: (deps.getOpponentSeatRotationDeg?.(beat.player) ?? 0) + 180;

		const { entry: landingEntry } = previewDiscardLanding(
			deps.bus.discardHistory,
			top,
			DISCARD_CAP,
			landingBaseDeg
		);

		deps.cardRegistry.registerCardMeta(String(top.id), {
			...cardMetaFrom(top),
			wildColor: (state.active_type && state.active_type !== "white"
				? state.active_type
				: undefined) as CardType | undefined
		});

		if (playedByMe) {
			// INFO: consume any drop-play seed on the first local play, matching
			//       or not — a seed only ever describes the very next local play,
			//       so a stale one must not mis-seed a later card that recycles
			//       the same id. A matching seed means the play was committed by
			//       dropping the card on the pile, so fly from where it was
			//       released rather than snapping back to its old hand slot.
			const pendingDrag = deps.bus.pendingLocalDragPlay;
			if (pendingDrag) deps.bus.setPendingLocalDragPlay(null);
			const dragSeed =
				pendingDrag && String(pendingDrag.id) === String(top.id) ? pendingDrag : null;
			deps.cardRegistry.clearDecoration(String(top.id));
			if (dragSeed) {
				deps.cardRegistry.seedPose(String(top.id), {
					x: dragSeed.x,
					y: dragSeed.y,
					z: dragSeed.z,
					spinDeg: dragSeed.spinDeg,
					flipDeg: dragSeed.flipDeg,
					scale: dragSeed.scale,
					turned: dragSeed.turned,
					opacity: 1,
					dragT: 0
				});
			} else {
				const [sx, sy, sz] = localCardAnchor(
					top.id,
					placement,
					deps.bus.localHandSnapshot,
					deps.bus.previousLocalHandSnapshot
				);
				deps.cardRegistry.seedPose(String(top.id), {
					x: sx,
					y: sy,
					z: sz,
					spinDeg: 0,
					flipDeg: 0,
					scale: placement.handScale,
					turned: false,
					opacity: 1
				});
			}
		} else {
			const player = state.players?.find((p) => p.username === beat.player);
			// INFO: from_zone_ordinal is a 0-based index into the PRE-play hand
			// (the backend computes hand_index before MoveCardToZone), so the
			// arc must be sized for the pre-play count = post-play card_count + 1
			// — mirroring the watcher's prevCardCounts-based preCount.
			const prePlayCount = Math.max(1, (player?.card_count ?? 0) + 1);
			const slotIndex = beat.fromZoneOrdinal ?? 0;
			const opponentPose = deps.getOpponentCardPose?.(beat.player, prePlayCount, slotIndex);
			const [sx, sy, sz] = opponentPose
				? opponentPose.position
				: deps.getOpponentSeatAnchor(beat.player);
			const seedSpin = opponentPose
				? opponentPose.spinDeg
				: (deps.getOpponentSeatRotationDeg?.(beat.player) ?? 0);

			deps.cardRegistry.clearDecoration(String(top.id));
			deps.cardRegistry.seedPose(String(top.id), {
				x: sx,
				y: sy,
				z: sz,
				spinDeg: seedSpin,
				flipDeg: 0,
				scale: deps.getOpponentCardScale?.() ?? placement.centerScale,
				turned: true,
				opacity: 1
			});
		}

		lastLandingBaseDeg = landingBaseDeg;

		const originUv = originToMatUv(placement.discardX, placement.discardZ, placement.mat);
		const maxRadius = maxRadiusUv(originUv, placement.mat.size[1] / placement.mat.size[0]);
		const isWild = top.type === "white";
		const knownWildColor =
			state.active_type && state.active_type !== "white" ? state.active_type : undefined;

		// A new landing always supersedes whatever the previous landing was
		// still waiting on — an unresolved wild from an earlier play must never
		// fire against this landing's origin.
		pendingWildRipple = null;

		let ripple: Parameters<typeof buildPlayBeat>[0]["ripple"];
		if (!isWild) {
			ripple = {
				cardColour: CARD_COLOR_MAP[top.type] ?? MAT_INITIAL_COLOR,
				strength: "normal",
				originUv,
				maxRadius
			};
		} else if (knownWildColor) {
			ripple = {
				cardColour: CARD_COLOR_MAP[knownWildColor] ?? MAT_INITIAL_COLOR,
				strength: "wild",
				originUv,
				maxRadius
			};
		} else {
			// Wild landed but the colour pick hasn't arrived yet — syncState()
			// resolves this once active_type stops reading "white".
			pendingWildRipple = { originUv, maxRadius };
		}

		const playBeat = buildPlayBeat({
			cardId: String(top.id),
			playedByMe,
			placement,
			localHandSnapshot: deps.bus.localHandSnapshot,
			landingSpinDeg: landingEntry.rotationDeg,
			ripple
		});
		const shakeBeat: AnimationBeat = [{ op: "shake", target: String(top.id), payload: {} }];

		if (!playedByMe) {
			deps.bus.addInFlightPlay(beat.player);
		}
		deps.cardRegistry.enqueue([playBeat, shakeBeat], resolveCardTarget).then(() => {
			deps.bus.setDiscardTop(top, landingBaseDeg);
			if (!playedByMe) {
				deps.bus.removeInFlightPlay(beat.player);
			}
		});
	}

	/** Ports the watcher's draw branch (baseBeats.svelte.ts:795-1077). The
	 *  owner receives real card ids, so the local branch reads them
	 *  straight from the beat; every other viewer gets a count only and the
	 *  opponent branch synthesizes ids. */
	function handleDraw(beat: Extract<MatchEventBeat, { kind: "draw" }>): void {
		const state = storeGame.state;
		if (!state) return;

		if (beat.player !== resolveLocalUsername(state)) {
			handleOpponentDraw(beat, state);
			return;
		}

		const newIds = beat.cardIds;
		if (newIds.length === 0) return;

		const placement = deps.getPlacement();
		const localHand = state.players?.find((p) => p.username === beat.player)?.hand ?? [];

		// Each landed card rejoins the row at once, so card i lands in the row
		// as it will stand at that moment: the cards already showing plus the
		// i drawn ahead of it. Counted from the live state when the beat starts,
		// not from this snapshot — a queued draw can start after later
		// snapshots have already reshaped the hand.
		const slotAnchorKeys = newIds.map((cardId) => `local-draw-slot:${cardId}`);
		const resolveDrawTarget = (name: string): [number, number, number] => {
			const drawIndex = slotAnchorKeys.indexOf(name);
			if (drawIndex === -1) return resolveCardTarget(name);
			const slotIndex = shownLocalCardCount(beat.player) + drawIndex;
			return localHandSlotAnchor(
				slotIndex + 1,
				slotIndex,
				deps.getPlacement(),
				deps.bus.localHandSnapshot
			);
		};

		const isLocalTurn = state.current_turn === beat.player;
		// INFO: the snapshot is post-draw, so add the drawn count back for the
		// PRE-draw pile height the card visually left from. A same-batch
		// reshuffle can make this approximate.
		const preDrawSize = (state.draw_pile_size ?? 0) + beat.count;
		for (const cardId of newIds) {
			const card = localHand.find((c) => c.id === cardId);
			if (!card) continue;
			const idString = String(cardId);
			localDrawFlightIds.add(idString);
			deps.cardRegistry.registerCardMeta(idString, cardMetaFrom(card));
			const [px, py, pz] = drawPileTopPose(
				placement,
				preDrawSize,
				storeRenderSettings.drawPileThickness,
				deps.bus.getDrawPileHoverDipZ?.() ?? 0
			);
			deps.cardRegistry.clearDecoration(idString);
			deps.cardRegistry.seedPose(idString, {
				x: px,
				y: py + DRAW_HOVER_LIFT,
				z: pz,
				spinDeg: 0,
				flipDeg: 0,
				scale: placement.drawPileScale,
				turned: true,
				opacity: 1
			});
			deps.cardRegistry.setDecoration(idString, {
				dimmed: !isLocalTurn
			});
		}
		for (const id of newIds) deps.bus.addPendingLocalDraw(id);
		const cardIds = newIds.map(String);
		const remainingLocalDraws = new Set(newIds);
		deps.cardRegistry
			.enqueue(
				buildDrawBeats({
					cardIds,
					forLocalPlayer: true,
					placement,
					slotAnchorKeys,
					onCardComplete: (index) => {
						const id = newIds[index];
						if (id !== undefined && remainingLocalDraws.has(id)) {
							remainingLocalDraws.delete(id);
							deps.bus.removePendingLocalDraw(id);
							localDrawFlightIds.delete(String(id));
							deps.cardRegistry.releaseLanded(String(id));
						}
					}
				}),
				resolveDrawTarget
			)
			.finally(() => {
				for (const id of remainingLocalDraws) {
					deps.bus.removePendingLocalDraw(id);
					localDrawFlightIds.delete(String(id));
				}
				remainingLocalDraws.clear();
			});
	}

	/** Ports the watcher's plain multi-draw opponent branch
	 *  (baseBeats.svelte.ts:988-1077). Non-owner viewers get `count` only, so
	 *  each card is seeded at the draw-pile top under a synthetic
	 *  `draw:<player>:<n>` id and flies into its own slot of the opponent's
	 *  card arc. The `isOpponentPlayableDraw` branch is intentionally dropped
	 *  (PlayDrawn legacy, same ruling as A1/A2). */
	function handleOpponentDraw(
		beat: Extract<MatchEventBeat, { kind: "draw" }>,
		state: NonNullable<typeof storeGame.state>
	): void {
		const drawnCount = beat.count;
		if (drawnCount <= 0) return;
		const player = state.players?.find((p) => p.username === beat.player);
		if (!player) return;

		const placement = deps.getPlacement();
		const opponentCardScale = deps.getOpponentCardScale?.() ?? placement.centerScale;
		// INFO: the snapshot is post-draw, so the drawn count is added back for
		// the PRE-draw hand layout and pile height the cards visually left from.
		const preCount = Math.max(0, player.card_count - drawnCount);
		const preDrawSize = (state.draw_pile_size ?? 0) + drawnCount;

		const isTurn = state.current_turn === beat.player;
		const isValidTarget = isChoosePlayerTarget(beat.player);
		const isDimmed = !isTurn && !isValidTarget;

		const cardIds: string[] = [];
		const slotAnchorKeys: string[] = [];
		const slotSpinDegs: number[] = [];
		for (let i = 0; i < drawnCount; i++) {
			const cardId = `draw:${beat.player}:${drawIdCounter++}`;
			cardIds.push(cardId);
			opponentDrawFlightOwners.set(cardId, beat.player);
			const targetSlotIndex = preCount + i;
			const currentStepCardCount = preCount + i + 1;
			const slotKey = opponentSlotAnchorKey(beat.player, targetSlotIndex, drawIdCounter);
			const targetPose = deps.getOpponentCardPose?.(
				beat.player,
				currentStepCardCount,
				targetSlotIndex
			);

			if (targetPose) {
				pendingOpponentSlots.set(slotKey, targetPose.position);
				slotAnchorKeys.push(slotKey);
				slotSpinDegs.push(targetPose.spinDeg);
			} else {
				slotAnchorKeys.push(opponentSeatAnchor(beat.player));
				slotSpinDegs.push(deps.getOpponentSeatRotationDeg?.(beat.player) ?? 0);
			}

			const [px, py, pz] = drawPileTopPose(
				placement,
				preDrawSize,
				storeRenderSettings.drawPileThickness,
				deps.bus.getDrawPileHoverDipZ?.() ?? 0
			);
			deps.cardRegistry.clearDecoration(cardId);
			deps.cardRegistry.seedPose(cardId, {
				x: px,
				y: py + DRAW_HOVER_LIFT,
				z: pz,
				spinDeg: 0,
				flipDeg: 0,
				scale: placement.drawPileScale,
				turned: true,
				opacity: 1
			});
			deps.cardRegistry.setDecoration(cardId, { dimmed: isDimmed });
		}

		deps.bus.addInFlightDraw(beat.player, drawnCount);
		let remainingOpponentDraws = drawnCount;
		deps.cardRegistry
			.enqueue(
				buildDrawBeats({
					cardIds,
					forLocalPlayer: false,
					opponentUsername: beat.player,
					placement,
					opponentCardScale,
					slotAnchorKeys,
					slotSpinDegs,
					onCardComplete: (index) => {
						if (remainingOpponentDraws > 0) {
							remainingOpponentDraws--;
							deps.bus.removeInFlightDraw(beat.player, 1);
						}
						const cardId = cardIds[index];
						if (cardId) {
							deps.cardRegistry.removeEntry(cardId);
							opponentDrawFlightOwners.delete(cardId);
						}
					}
				}),
				resolveCardTarget
			)
			.finally(() => {
				if (remainingOpponentDraws > 0) {
					deps.bus.removeInFlightDraw(beat.player, remainingOpponentDraws);
					remainingOpponentDraws = 0;
				}
				for (const cardId of cardIds) {
					deps.cardRegistry.removeEntry(cardId);
					opponentDrawFlightOwners.delete(cardId);
				}
			});
	}

	/** Ports the watcher's `processReshuffle` (baseBeats.svelte.ts:1083-1160).
	 *  The watcher derived the amount from a draw-pile diff; the packet gives
	 *  `drawSize`, so the pre-reshuffle size is tracked from the previous
	 *  snapshot. `discardSize` is unused — the synthetic backfill below covers
	 *  any client-side discard-history shortfall. */
	function handleReshuffle(beat: Extract<MatchEventBeat, { kind: "reshuffle" }>): void {
		if (prevDrawPileSize === null) return;
		const amountToReshuffle = beat.drawSize - prevDrawPileSize;
		if (amountToReshuffle <= 0) return;

		const placement = deps.getPlacement();
		// INFO: the animated pile must never read as taller than the render cap:
		//       seed the reshuffle at the previous (already-capped) size so a
		//       jump in draw_pile_size cannot flash the full stack before the
		//       bus takes over the count.
		const preSize = Math.min(prevDrawPileSize, MAX_DRAW_PILE_STACK);
		deps.bus.reshuffleDrawPileSize = preSize;

		const existingToReshuffle = deps.bus.discardHistory.slice(0, -1);
		// If client-side discardHistory has fewer cards than amountToReshuffle
		// (e.g. after a page refresh, where only the top card exists client-side),
		// synthesize the missing cards so the animation and height progression reflect
		// the actual amount reshuffled rather than client-side discard state.
		const missingCount = Math.max(0, amountToReshuffle - existingToReshuffle.length);
		const syntheticEntries: DiscardEntry[] = [];
		for (let i = 0; i < missingCount; i++) {
			const angle = ((i * 37) % 70) - 35;
			const jx = (((i * 17) % 20) - 10) * 0.05;
			const jy = (((i * 23) % 20) - 10) * 0.05;
			syntheticEntries.push({
				card: { id: -1000 - i, type: "wild", value: "0" },
				seq: -1000 - i,
				jitter: [jx, jy],
				rotationDeg: angle
			});
		}
		const slicedExisting = existingToReshuffle.slice(
			0,
			amountToReshuffle - syntheticEntries.length
		);
		const toReshuffle = [...syntheticEntries, ...slicedExisting];

		for (const [i, entry] of toReshuffle.entries()) {
			const idString = String(entry.card.id);
			deps.cardRegistry.registerCardMeta(idString, entry.card);
			deps.cardRegistry.clearDecoration(idString);
			const offset = discardCardOffset(
				placement.discardX,
				placement.discardZ,
				entry,
				placement.centerScale
			);
			deps.cardRegistry.seedPose(idString, {
				x: offset.x,
				y: discardStackZ(i, toReshuffle.length),
				z: offset.z,
				spinDeg: entry.rotationDeg,
				flipDeg: 0,
				scale: placement.centerScale,
				turned: false,
				opacity: 1
			});
		}
		const beats = buildReshuffleBeat(toReshuffle, {
			placement,
			isAlreadySliced: true,
			onCardArrive: (cardIndex, totalCards) => {
				const newSize = Math.min(preSize + cardIndex + 1, MAX_DRAW_PILE_STACK);
				deps.bus.reshuffleDrawPileSize = newSize;
				deps.bus.onReshuffleCardLanding = {
					index: cardIndex,
					total: totalCards,
					timestamp: Date.now()
				};
			}
		});
		if (beats.length > 0) {
			deps.cardRegistry
				.enqueue(beats, resolveCardTarget)
				.then(() => {
					deps.bus.retainTopDiscard(slicedExisting.length);
					for (const entry of toReshuffle) {
						deps.cardRegistry.removeEntry(String(entry.card.id));
					}
				})
				.finally(() => {
					deps.bus.reshuffleDrawPileSize = null;
					deps.bus.onReshuffleCardLanding = null;
				});
		} else {
			deps.bus.reshuffleDrawPileSize = null;
			deps.bus.onReshuffleCardLanding = null;
		}
	}

	/** Presents the skipped-seat X and gates the incoming turn's highlight.
	 *  The engine silently consumes one-shot skips and names the skipped seats
	 *  on the `turn_advance` payload; the store holds the outgoing player's
	 *  highlight until the marks clear (click-to-skip ends it early). A no-op
	 *  when animations are disabled, so the turn simply moves on. */
	function handleTurn(beat: Extract<MatchEventBeat, { kind: "turn" }>): void {
		if (!storeAnimation.enabled) return;
		if (beat.skipped.length === 0) return;
		const durationMs = SKIP_MARK_DURATION_MS / Math.max(0.1, storeAnimation.speedMultiplier);
		storeTurnSkip.present(beat.from, beat.skipped, durationMs);
	}

	function handle(beat: MatchEventBeat): void {
		switch (beat.kind) {
			case "play":
				handlePlay(beat);
				return;
			case "draw":
				handleDraw(beat);
				return;
			case "reshuffle":
				handleReshuffle(beat);
				return;
			case "turn":
				handleTurn(beat);
				return;
			case "toast":
				// Toast state lives in the store; map-and-drop.
				return;
		}
	}

	/** Called by GameBoard on each `storeGame.state` change (a plain $effect,
	 *  NOT a diff source). Also live-refreshes every in-flight draw card's
	 *  dimmed state, since a turn or a target prompt can change mid-flight
	 *  during a staggered multi-card draw. */
	function syncState(): void {
		const state = storeGame.state;
		if (!state) return;

		// INFO: the watcher used to seed the discard pile on first observation;
		// without it the first render shows an empty pile. Seed only when
		// nothing is there yet so a live discard history is never clobbered.
		if (state.top_card && deps.bus.discardHistory.length === 0 && !storeMatchIntro.active) {
			deps.bus.setDiscardTop(state.top_card);
		}

		deps.bus.setActiveType(state.active_type as CardType);

		if (pendingWildRipple && state.active_type && state.active_type !== "white") {
			const { originUv, maxRadius } = pendingWildRipple;
			pendingWildRipple = null;
			storeMatRipple.startMatRipple(
				CARD_COLOR_MAP[state.active_type] ?? MAT_INITIAL_COLOR,
				"wild",
				originUv,
				maxRadius
			);
		}

		for (const [flightCardId, flightUsername] of opponentDrawFlightOwners) {
			const flightIsTurn = state.current_turn === flightUsername;
			const flightIsValidTarget = isChoosePlayerTarget(flightUsername);
			deps.cardRegistry.setDecoration(flightCardId, {
				dimmed: !flightIsTurn && !flightIsValidTarget
			});
		}

		const isLocalTurn = state.current_turn === resolveLocalUsername(state);
		for (const flightCardId of localDrawFlightIds) {
			deps.cardRegistry.setDecoration(flightCardId, {
				dimmed: !isLocalTurn
			});
		}

		// INFO: last so it holds the previous snapshot's value while the store
		// drains this snapshot's beats — handleReshuffle needs the
		// pre-reshuffle size to derive the swept amount.
		prevDrawPileSize = state.draw_pile_size;

		// INFO: recovers a drag-drop play the server rejected. LocalHand3D marks
		//       the card in-transit and stashes a pendingLocalDragPlay seed on
		//       drop; handlePlay's playedByMe branch normally clears both, but a
		//       rejection never changes top_card so that branch never runs and the
		//       card was stuck in-transit. isActionPending flipping back to false
		//       (success OR rejection, plus the store's server-silence net) is
		//       the only signal; a successful play already consumed the seed, so
		//       this is a no-op then.
		const isActionPendingNow = storeGame.isActionPending;
		if (prevIsActionPending && !isActionPendingNow) {
			const stuck = deps.bus.pendingLocalDragPlay;
			if (stuck) {
				deps.bus.setPendingLocalDragPlay(null);
				deps.cardRegistry.markInTransit(String(stuck.id), false);
				deps.cardRegistry.applyIdlePoseIfNotInTransit(String(stuck.id));
			}
		}
		prevIsActionPending = isActionPendingNow;
	}

	const dispose = deps.subscribeBeats(handle);
	return { dispose, syncState, resetPendingWildRipple };
}

/** Computes the local hand's card-N slot as a world-space anchor — ported
 *  verbatim from the watcher, including the previous-snapshot fallback for
 *  the $effect-run-order race. */
function localCardAnchor(
	cardId: number,
	placement: BoardPlacement,
	snapshot: HandSnapshot,
	previousSnapshot: HandSnapshot | null = null
): [number, number, number] {
	for (const candidate of [snapshot, previousSnapshot]) {
		if (!candidate) continue;
		const index = candidate.orderIds.indexOf(cardId);
		if (index === -1) continue;
		return handSlotPose(index, candidate.orderIds.length, candidate, placement);
	}

	console.warn(
		`matchEventController: card ${cardId} not found in current or previous local hand snapshot — falling back to hand-center anchor.`
	);
	return [0, 0, placement.localSeatZ];
}
