<script lang="ts">
	import { storeLobby } from "$stores/lobby.svelte";
	import { storeAuth } from "$stores/auth.svelte";

	import LobbySettings from "./LobbySettings.svelte";
	import LobbySave from "./LobbySave.svelte";
	import Modal from "$components/common/Modal.svelte";
	import TintedSprite from "$components/common/TintedSprite.svelte";
	import TextEffects from "$components/common/TextEffects.svelte";

	let isHost = $derived(storeAuth.username === storeLobby.current?.host);
	let startable = $derived((storeLobby.current?.members.length ?? 0) >= 2);

	let isEditingName = $state(false);
	let editedName = $state("");
	let showInviteCode = $state(false);
	let settingsOpen = $state(false);
	let activeMenu = $state<string | null>(null);

	// Last pointer kind to touch a seat card. Right-click opens the seat menu
	// on desktop; tapping does the same job on touch, so both need distinct
	// triggers instead of a single always-visible kebab button.
	let pointerKind = $state<string>("mouse");

	// The four seat colours ARE the four UNO card colours, members are dealt
	// their seat as an actual card face, not a generic avatar chip.
	const SEAT_COLORS = ["var(--blueCard)", "var(--greenCard)", "var(--redCard)", "var(--yellowCard)"];
	const TABLE_SEATS = SEAT_COLORS.length;

	let emptySeats = $derived(Math.max(0, TABLE_SEATS - (storeLobby.current?.members.length ?? 0)));

	function handleSeatMenu(member: { username: string; is_host: boolean }, event: Event) {
		if (!isHost || member.is_host) return;
		event.preventDefault();
		event.stopPropagation();
		activeMenu = activeMenu === member.username ? null : member.username;
	}

	$effect(() => {
		const closeMenu = () => (activeMenu = null);
		window.addEventListener("click", closeMenu);
		return () => window.removeEventListener("click", closeMenu);
	});

	function startEditing() {
		if (!isHost) return;
		editedName = storeLobby.current?.name ?? "";
		isEditingName = true;
	}

	function focusOnMount(node: HTMLElement) {
		node.focus();
	}

	function saveName() {
		isEditingName = false;
		const trimmed = editedName.trim();
		if (trimmed && trimmed !== storeLobby.current?.name && trimmed.length <= 22) {
			storeLobby.updateSettings({ name: trimmed });
		}
	}

	// Anchors the pulse's phase to wall-clock time (negative delay) so seats
	// that mount at different moments (e.g. a player leaving mid-cycle) still
	// blink in sync instead of each restarting its own 3s cycle from zero.
	const PULSE_PERIOD_MS = 3000;
	function syncPulse(node: HTMLElement) {
		node.style.animationDelay = `${-(Date.now() % PULSE_PERIOD_MS)}ms`;
	}
</script>

<div
	class="fixed inset-0 flex flex-col overflow-hidden bg-cover bg-center"
	style="background-image: url('/assets/bg_full.png'); image-rendering: pixelated;"
>
	<!-- Header: title (editable by host) + invite code + leave ------------- -->
	<header
		class="flex flex-wrap items-center justify-between gap-3 border-b-2 border-border bg-bg px-4 py-2 sm:px-6 sm:py-3 lg:px-10"
	>
		<div class="min-w-0 flex-1">
			{#if isEditingName && isHost}
				<input
					class="title-screen w-full max-w-full truncate border-none bg-transparent p-0 text-3xl tracking-[-1.68px] outline-none [clip-path:none!important] sm:text-4xl lg:text-[56px] max-lg:text-2xl! max-lg:landscape:text-xl!"
					bind:value={editedName}
					onblur={saveName}
					onkeydown={(e) => e.key === "Enter" && saveName()}
					maxlength="22"
					use:focusOnMount
				/>
			{:else if isHost}
				<button
					type="button"
					class="title-screen block max-w-full truncate border-none bg-transparent p-0 text-left text-3xl tracking-[-1.68px] [clip-path:none!important] sm:text-4xl lg:text-[56px] max-lg:text-2xl! max-lg:landscape:text-xl!"
					onclick={startEditing}
				>
					{storeLobby.current?.name}
				</button>
			{:else}
				<h1
					class="title-screen max-w-full truncate text-3xl tracking-[-1.68px] sm:text-4xl lg:text-[56px] max-lg:text-2xl! max-lg:landscape:text-xl!"
				>
					{storeLobby.current?.name}
				</h1>
			{/if}
		</div>

		<div class="flex shrink-0 items-center gap-2">
			<div class="flex items-center gap-1 bg-black px-3 py-2">
				<span class="w-20 text-center font-mono text-sm text-text sm:text-base">
					{showInviteCode ? storeLobby.current?.invite_code : "••••••"}
				</span>
				<button
					class="flex items-center leading-none text-white"
					onclick={() => (showInviteCode = !showInviteCode)}
					title={showInviteCode ? "Hide code" : "Show code"}
				>
					<i class="hn pix {showInviteCode ? 'hn-eye' : 'hn-eye-cross'} text-lg"></i>
				</button>
			</div>

			<button
				class="btn pixel-corners bg-danger px-3 py-2 sm:px-4"
				onclick={() => storeLobby.leave()}
				title="Exit Lobby"
			>
				<img src="/assets/exit.png" alt="Exit" class="h-5 w-5" />
			</button>
		</div>
	</header>

	<!-- Saved matches + settings ----------------------------------------------- -->
	<div class="flex items-center justify-between gap-3 border-b border-border bg-bg px-4 py-2 sm:px-6 lg:px-10">
		<details class="relative w-fit select-none">
			<summary
				class="cursor-pointer list-none border border-white/10 bg-bg px-4 py-2 font-tiny text-xs uppercase [&::-webkit-details-marker]:hidden"
			>
				{storeLobby.savedMatches?.length ?? 0} Saves
			</summary>
			<ul
				class="scrollbar-accent absolute left-0 top-[calc(100%+10px)] z-50 max-h-96 w-80 max-w-[90vw] list-none overflow-y-auto border-2 border-border bg-bg p-2 shadow-lg"
			>
				{#each storeLobby.savedMatches ?? [] as save}
					<LobbySave {save} />
				{/each}
				{#if (storeLobby.savedMatches?.length ?? 0) === 0}
					<li class="p-2.5 text-xs text-text">No saved matches</li>
				{/if}
			</ul>
		</details>

		<button
			class="btn pixel-corners px-3 py-2 sm:px-4"
			onclick={() => (settingsOpen = true)}
			title="Lobby settings"
		>
			<i class="hn pix hn-cog"></i>
			<span class="ml-2 hidden uppercase sm:inline">Settings</span>
		</button>
	</div>

	<!-- Table: dealt seats + controls ----------------------------------------- -->
	<div class="scrollbar-accent flex-1 overflow-y-auto">
		<div
			class="mx-auto flex w-full max-w-330 flex-col items-center gap-10 px-4 py-6 sm:px-6 lg:px-10"
		>
			<ul
				class="hand grid list-none grid-cols-2 justify-items-center gap-4 p-0 sm:gap-6 lg:grid-cols-4 lg:gap-8"
			>
				{#each storeLobby.current?.members ?? [] as member, i}
					{@const color = member.is_bot ? "var(--blackCard)" : SEAT_COLORS[i % SEAT_COLORS.length]}
					<!-- svelte-ignore a11y_no_noninteractive_tabindex -- seat stays an <li> for
						list semantics but is host-interactive (kick/promote) when tabindex is set -->
					<li
						class="seat-card group relative w-32 shrink-0 sm:w-36 lg:w-44"
						class:cursor-context-menu={isHost && !member.is_host}
						style="aspect-ratio: 1 / 1.5357; --card-color: {color};"
						role={isHost && !member.is_host ? "button" : undefined}
						tabindex={isHost && !member.is_host ? 0 : undefined}
						onpointerdown={(e) => (pointerKind = e.pointerType)}
						oncontextmenu={(e) => handleSeatMenu(member, e)}
						onclick={(e) => {
							if (pointerKind === "touch") handleSeatMenu(member, e);
						}}
						onkeydown={(e) => {
							if (e.key === "Enter" || e.key === " ") handleSeatMenu(member, e);
						}}
					>
						<div class="absolute inset-0 overflow-hidden rounded-[0.8em] shadow-[var(--lowShadow)]">
							<img
								src="/assets/cards/background.png"
								alt=""
								class="absolute inset-0 h-full w-full object-fill"
							/>
							<div class="absolute inset-[14%]">
								{#if member.is_bot}
									<img
										src="/assets/bot_animated.gif"
										alt=""
										class="h-full w-full object-contain"
									/>
								{:else}
									<TintedSprite src="/assets/base_player.gif" {color} fit="contain" />
								{/if}

								<!-- Deco layer: sits on the seat's face art, sized to match it
								     exactly. Host's crown lives here so future cosmetics can
								     stack the same way. -->
								{#if member.is_host}
									<img
										src="/assets/crown_host.gif"
										alt="Host"
										class="pointer-events-none absolute inset-0 h-full w-full object-contain"
									/>
								{/if}
							</div>
							<div class="pointer-events-none absolute inset-0">
								<TintedSprite src="/assets/cards/border.png" {color} fit="100% 100%" />
							</div>
						</div>

						<p
							class="absolute inset-x-0 -bottom-5 flex items-center justify-center gap-1 text-center font-tiny text-[10px] uppercase text-white sm:text-xs"
						>
							{#if member.is_bot}
								<i class="hn pix hn-robot" style="color: lightblue"></i>
							{/if}
							<span class="truncate">{member.username}</span>
						</p>

						{#if activeMenu === member.username}
							<div
								class="absolute right-1 top-1 z-30 min-w-[140px] border-2 border-border bg-bg"
							>
								<button
									class="w-full px-3 py-2.5 text-left text-sm font-bold transition-[background,filter] hover:bg-white/10 hover:shadow-[inset_4px_0_0_var(--accent)]"
									style="color: lightgoldenrodyellow"
									onclick={() => {
										storeLobby.promote(member.username);
										activeMenu = null;
									}}
								>
									Promote
								</button>
								<button
									class="w-full px-3 py-2.5 text-left text-sm font-bold transition-[background,filter] hover:bg-white/10 hover:shadow-[inset_4px_0_0_var(--accent)]"
									style="color: lightsalmon"
									onclick={() => {
										storeLobby.kick(member.username);
										activeMenu = null;
									}}
								>
									Kick
								</button>
							</div>
						{/if}
					</li>
				{/each}

				{#each Array(emptySeats) as _, idx}
					{@const seatIndex = (storeLobby.current?.members.length ?? 0) + idx}
					{@const color = SEAT_COLORS[seatIndex % SEAT_COLORS.length]}
					<li
						class="seat-card relative w-32 shrink-0 sm:w-36 lg:w-44"
						style="aspect-ratio: 1 / 1.5357; --card-color: {color};"
					>
						<div
							class="seat-empty absolute inset-0 overflow-hidden rounded-[0.8em] shadow-[var(--lowShadow)]"
							style="filter: grayscale(0.55) brightness(0.85);"
							use:syncPulse
						>
							<img
								src="/assets/cards/background.png"
								alt=""
								class="absolute inset-0 h-full w-full object-fill"
							/>
							<div class="pointer-events-none absolute inset-0">
								<TintedSprite src="/assets/cards/border.png" {color} fit="100% 100%" />
							</div>
						</div>
						<p
							class="absolute inset-x-0 -bottom-5 text-center font-tiny text-[10px] uppercase text-text sm:text-xs"
						>
							Waiting…
						</p>
					</li>
				{/each}
			</ul>

			<div class="flex items-center justify-center">
				<button
					class="start-button flex items-center justify-center border-none bg-transparent p-0"
					onclick={() => storeLobby.startMatch()}
					disabled={!isHost || !startable || storeLobby.isLoadingStart}
				>
					<div
						class="flex gap-1 text-4xl tracking-[6px] text-white [-webkit-text-stroke:1.5px_var(--pixel-shadow)] [font-family:'FatPixel'] [text-shadow:2px_2px_0_var(--pixel-shadow)] sm:text-5xl"
					>
						<TextEffects
							text="START!"
							effect="undulate"
							class="start-letters"
							font="FatPixel"
							amplitude={15}
							speed={1}
							frequency={0.1}
						/>
					</div>
				</button>
			</div>
		</div>
	</div>
</div>

{#if settingsOpen}
	<Modal
		bind:open={settingsOpen}
		ariaLabel="Lobby settings"
		contentClass="pixel-corners relative flex max-h-[85vh] w-full max-w-xl flex-col overflow-y-auto p-5 sm:p-7"
	>
		<button
			class="absolute right-3 top-3 text-2xl text-text hover:text-text-h"
			title="Close"
			aria-label="Close"
			onclick={() => (settingsOpen = false)}><i class="hn pix hn-times"></i></button
		>
		<LobbySettings />
	</Modal>
{/if}

<style>
	.start-button:disabled {
		cursor: not-allowed;
	}
	.start-button:disabled :global(.start-letters) {
		color: #888;
	}
	.start-button:disabled :global(.start-letters .char) {
		animation: none;
		transform: translateY(0);
	}

	/* Seats read as a fanned, dealt hand on desktop only, mobile keeps them
	   upright so names/menus stay legible at small widths. */
	.hand li:nth-child(1) {
		--seat-tilt: -6deg;
	}
	.hand li:nth-child(2) {
		--seat-tilt: -2deg;
	}
	.hand li:nth-child(3) {
		--seat-tilt: 3deg;
	}
	.hand li:nth-child(4) {
		--seat-tilt: 7deg;
	}

	@media (min-width: 1024px) {
		.seat-card {
			transform: rotate(var(--seat-tilt, 0deg));
			transition: transform 0.15s ease;
		}
		.seat-card:hover {
			transform: rotate(0deg) translateY(-8px);
		}
	}

	.seat-empty {
		animation: seat-wait 3s ease-in-out infinite;
	}
	@keyframes seat-wait {
		0%,
		100% {
			opacity: 0.45;
		}
		50% {
			opacity: 0.9;
		}
	}

	@media (prefers-reduced-motion: reduce) {
		.seat-card {
			transition: none;
		}
		.seat-empty {
			animation: none;
			opacity: 0.7;
		}
	}
</style>
