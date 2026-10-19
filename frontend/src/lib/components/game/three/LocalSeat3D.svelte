<!-- The local player's avatar, rendered as a 3D mesh directly on the table plane
     (y = -0.005) so it always renders under cards (y >= 0) rather than floating
     in an HTML overlay above the WebGL canvas. Animated from the baked avatar
     strip (see avatarTexture.ts). -->
<script lang="ts">
	import { T, useTask } from "@threlte/core";
	import { Color, type Texture } from "three";
	import type { GamePlayer } from "$stores/game.svelte";
	import type { BoardPlacement } from "../layout/boardPlacement";
	import { LOCAL_AVATAR_WORLD } from "../layout/boardPlacement";
	import { onDestroy, untrack } from "svelte";
	import { gsap } from "gsap";
	import { FRAME_COUNT, FRAME_MS } from "$lib/avatar/types";
	import { loadAvatarTexture } from "./avatarTexture";
	import { localAvatarTint } from "./avatarTint";
	import { turnRimTexture } from "./turnRimTexture";
	import { storeAnimation } from "$stores/animation.svelte";
	import { storeAudio } from "$stores/audio.svelte";
	import { storeTurnCue } from "$stores/turnCue.svelte";
	import { storeTurnSkip } from "$stores/turnSkip.svelte";
	import SkipMark3D from "./SkipMark3D.svelte";

	let {
		player,
		color,
		placement,
		avatarPx,
		dimmed = false,
		isTurn = false,
		isDebtTarget = false
	}: {
		player: GamePlayer;
		color: string;
		placement: BoardPlacement;
		/** Avatar box edge in px — kept for props compatibility. */
		avatarPx?: number;
		/** Darkens the avatar when it isn't this player's turn. */
		dimmed?: boolean;
		/** Holds the faint steady rim while it is this player's turn. */
		isTurn?: boolean;
		/** Highlights the avatar and rim when targeted by draw debt. */
		isDebtTarget?: boolean;
	} = $props();

	// Sits below cards (y >= 0) and above playmat/arrows (y <= -0.01)
	const AVATAR_Y = -0.005;
	const FRAME_DURATION = FRAME_MS / 1000;
	// 96 / 68 ratio matches AVATAR_SPRITE_FILL so the figure is LOCAL_AVATAR_WORLD tall
	const AVATAR_MESH_SIZE = LOCAL_AVATAR_WORLD * (96 / 68);

	const TURN_PULSE_SECONDS = 0.6;
	const TURN_PULSE_PEAK_OPACITY = 0.95;
	const TURN_PULSE_PEAK_SCALE = 1.25;
	const TURN_STEADY_OPACITY = 0.3;
	const TURN_RIM_SIZE = AVATAR_MESH_SIZE * 1.3;

	const rimTexture = turnRimTexture();
	let rimOpacity = $state(0);
	let rimScale = $state(1);
	let pulseTween: gsap.core.Tween | null = null;
	let lastCueToken = storeTurnCue.token;
	let lastSettleToken = storeTurnCue.settleToken;

	let avatarTexture = $state<Texture | null>(null);
	let currentFrame = 0;
	let frameElapsed = 0;

	// A spectator's POV can be a bot, which has its own full-colour sheet.
	let isBot = $derived(player.is_bot || player.username?.toLowerCase().includes("bot"));

	$effect(() => {
		let cancelled = false;
		const bot = isBot;
		// A spectator's POV can be a bot, which has its own full-colour sheet;
		// every human strip is baked from the loadout.
		loadAvatarTexture(player.loadout, bot)
			.then((texture) => {
				if (cancelled) {
					texture.dispose();
					return;
				}
				// Swap in the new strip and retire the one it replaces, so a
				// loadout change never leaks a CanvasTexture.
				avatarTexture?.dispose();
				avatarTexture = texture;
			})
			.catch(() => {
				// WARN: a failed load keeps the previous strip (or none).
			});
		return () => {
			cancelled = true;
		};
	});

	useTask((delta) => {
		if (!avatarTexture) return;
		frameElapsed += delta;
		if (frameElapsed >= FRAME_DURATION) {
			frameElapsed %= FRAME_DURATION;
			currentFrame = (currentFrame + 1) % FRAME_COUNT;
			avatarTexture.offset.x = currentFrame / FRAME_COUNT;
		}
	});

	const DANGER_COLOR = new Color("#ff4a3a");
	let baseColor = $derived(isBot ? new Color("#ffffff") : new Color(color));
	let effectiveColor = $derived(
		isDebtTarget ? DANGER_COLOR : dimmed ? baseColor.clone().multiplyScalar(0.45) : baseColor
	);
	// INFO: the baked strip has its own colors; the rim keeps the seat color.
	let avatarColor = $derived(localAvatarTint(dimmed, isDebtTarget));

	// Skip mark: the local player can lose a turn too (any seat can be skipped).
	let skipActive = $derived(storeTurnSkip.marks.includes(player.username));
	let skipToken = $derived(storeTurnSkip.token);
	let skipMarkPos = $derived<[number, number, number]>([
		0,
		LOCAL_AVATAR_WORLD * 0.7,
		placement.localAvatarZ
	]);
	let steadyOpacity = $derived(isDebtTarget ? 0.75 : isTurn ? TURN_STEADY_OPACITY : 0);

	$effect(() => {
		const token = storeTurnCue.token;
		if (token === lastCueToken) return;
		lastCueToken = token;
		pulseTween?.kill();
		pulseTween = null;
		if (!storeAnimation.enabled) return;

		if (!document.hidden) storeAudio.playSfx("sfx.turn.start");
		const duration = TURN_PULSE_SECONDS / Math.max(0.1, storeAnimation.speedMultiplier);
		const pulse = { progress: 0 };
		pulseTween = gsap.to(pulse, {
			progress: 1,
			duration,
			ease: "power2.out",
			onUpdate: () => {
				const fade = 1 - pulse.progress;
				rimOpacity = Math.max(steadyOpacity, TURN_PULSE_PEAK_OPACITY * fade);
				rimScale = 1 + (TURN_PULSE_PEAK_SCALE - 1) * pulse.progress;
			},
			onComplete: () => {
				pulseTween = null;
				rimOpacity = steadyOpacity;
				rimScale = 1;
			}
		});
	});

	// INFO: read steadyOpacity before the pulse guard; pulseTween is not
	// reactive, so an early return here would drop the only dependency.
	$effect(() => {
		const steady = steadyOpacity;
		if (pulseTween) return;
		rimOpacity = steady;
		rimScale = 1;
	});

	$effect(() => {
		const token = storeTurnCue.settleToken;
		if (token === lastSettleToken) return;
		lastSettleToken = token;
		pulseTween?.kill();
		pulseTween = null;
		untrack(() => {
			rimOpacity = steadyOpacity;
			rimScale = 1;
		});
	});

	onDestroy(() => {
		avatarTexture?.dispose();
		pulseTween?.kill();
		pulseTween = null;
	});

	let skipMarkSize = $derived(LOCAL_AVATAR_WORLD * 1.2);
</script>

{#if avatarTexture}
	<T.Mesh
		position.x={0}
		position.y={AVATAR_Y}
		position.z={placement.localAvatarZ}
		rotation.x={-Math.PI / 2}
	>
		<T.PlaneGeometry args={[AVATAR_MESH_SIZE, AVATAR_MESH_SIZE]} />
		<T.MeshBasicMaterial
			map={avatarTexture}
			color={avatarColor}
			transparent
			alphaTest={0.05}
			depthWrite={false}
			toneMapped={false}
		/>
	</T.Mesh>
{/if}

{#if rimTexture && rimOpacity > 0.01}
	<T.Mesh
		position.x={0}
		position.y={AVATAR_Y - 0.002}
		position.z={placement.localAvatarZ}
		rotation.x={-Math.PI / 2}
		scale={rimScale}
		data-testid="turn-rim"
	>
		<T.PlaneGeometry args={[TURN_RIM_SIZE, TURN_RIM_SIZE]} />
		<T.MeshBasicMaterial
			map={rimTexture}
			color={effectiveColor}
			transparent
			opacity={rimOpacity}
			depthWrite={false}
			toneMapped={false}
		/>
	</T.Mesh>
{/if}

<SkipMark3D position={skipMarkPos} size={skipMarkSize} active={skipActive} token={skipToken} />
