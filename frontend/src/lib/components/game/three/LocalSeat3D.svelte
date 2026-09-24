<!-- The local player's avatar, rendered as a 3D mesh directly on the table plane
     (y = -0.005) so it always renders under cards (y >= 0) rather than floating
     in an HTML overlay above the WebGL canvas. Animated with base_player_strip.png. -->
<script lang="ts">
	import { T, useTask } from "@threlte/core";
	import { Color, type Texture } from "three";
	import type { GamePlayer } from "$stores/game.svelte";
	import type { BoardPlacement } from "../layout/boardPlacement";
	import { LOCAL_AVATAR_WORLD } from "../layout/boardPlacement";
	import { loadSilhouette } from "./textures";
	import { storeTurnSkip } from "$stores/turnSkip.svelte";
	import SkipMark3D from "./SkipMark3D.svelte";

	let {
		player,
		color,
		placement,
		avatarPx,
		dimmed = false
	}: {
		player: GamePlayer;
		color: string;
		placement: BoardPlacement;
		/** Avatar box edge in px — kept for props compatibility. */
		avatarPx?: number;
		/** Darkens the avatar when it isn't this player's turn. */
		dimmed?: boolean;
	} = $props();

	// Sits below cards (y >= 0) and above playmat/arrows (y <= -0.01)
	const AVATAR_Y = -0.005;
	const FRAME_COUNT = 5;
	const FRAME_DURATION = 0.12;
	// 96 / 68 ratio matches AVATAR_SPRITE_FILL so the figure is LOCAL_AVATAR_WORLD tall
	const AVATAR_MESH_SIZE = LOCAL_AVATAR_WORLD * (96 / 68);

	let avatarTexture = $state<Texture | null>(null);
	let currentFrame = 0;
	let frameElapsed = 0;

	$effect(() => {
		let cancelled = false;
		loadSilhouette("/assets/base_player_strip.png").then((t) => {
			if (cancelled) return;
			const tex = t.clone();
			tex.repeat.set(1 / FRAME_COUNT, 1);
			tex.offset.x = 0;
			avatarTexture = tex;
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

	let baseColor = $derived(new Color(color));
	let effectiveColor = $derived(dimmed ? baseColor.clone().multiplyScalar(0.45) : baseColor);

	// Skip mark: the local player can lose a turn too (any seat can be skipped).
	let skipActive = $derived(storeTurnSkip.marks.includes(player.username));
	let skipToken = $derived(storeTurnSkip.token);
	let skipMarkPos = $derived<[number, number, number]>([
		0,
		LOCAL_AVATAR_WORLD * 0.7,
		placement.localAvatarZ
	]);
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
			color={effectiveColor}
			transparent
			alphaTest={0.05}
			depthWrite={false}
			toneMapped={false}
		/>
	</T.Mesh>
{/if}

<SkipMark3D position={skipMarkPos} size={skipMarkSize} active={skipActive} token={skipToken} />
