<script lang="ts">
	import { storeLobby } from "$stores/lobby.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeToast } from "$stores/toast.svelte";
	import { storeTopbarContent } from "$stores/topbarContent.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	import LobbySettings from "./LobbySettings.svelte";
	import Modal from "$components/common/Modal.svelte";
	import TintedSprite from "$components/common/TintedSprite.svelte";
	import TextEffects from "$components/common/TextEffects.svelte";

	let isHost = $derived(storeAuth.username === storeLobby.current?.host);

	let isEditingName = $state(false);
	let editedName = $state("");
	let showInviteCode = $state(false);
	let settingsOpen = $state(false);
	let activeMenu = $state<string | null>(null);
	let isAddingBot = $state(false);

	async function handleAddBot() {
		if (!isHost || isAddingBot) return;
		const current = storeLobby.current;
		if (!current) return;
		const maxPlayers = current.settings.max_players;
		if (current.members.length >= maxPlayers) return;

		const currentBotCount = current.settings.bot_count ?? 0;
		const humanCount = current.members.filter((m) => !m.is_bot).length;
		const maxBots = maxPlayers - humanCount;
		if (currentBotCount >= maxBots) return;

		isAddingBot = true;
		try {
			await storeLobby.updateSettings({ bot_count: currentBotCount + 1 });
		} finally {
			isAddingBot = false;
		}
	}

	// Last pointer kind to touch a seat card. Right-click opens the seat menu
	// on desktop; tapping does the same job on touch, so both need distinct
	// triggers instead of a single always-visible kebab button.
	let pointerKind = $state<string>("mouse");

	// The four seat colours ARE the four UNO card colours, members are dealt
	// their seat as an actual card face, not a generic avatar chip. Lobbies
	// with more than 4 seats cycle through them until #13 (polish(game):
	// color-identity fallback for >4 players) lands.
	const SEAT_COLORS = [
		"var(--blueCard)",
		"var(--greenCard)",
		"var(--redCard)",
		"var(--yellowCard)"
	];

	let tableSeats = $derived(storeLobby.current?.settings.max_players ?? SEAT_COLORS.length);
	let emptySeats = $derived(Math.max(0, tableSeats - (storeLobby.current?.members.length ?? 0)));

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

	async function copyInviteLink() {
		const code = storeLobby.current?.invite_code;
		if (!code) return;
		const url = `${location.origin}/invite/${code}`;
		await navigator.clipboard.writeText(url);
		storeToast.success(m.lobby_share_link_copied({}, { locale: storeI18n.locale }));
	}

	function saveName() {
		isEditingName = false;
		const trimmed = editedName.trim();
		if (trimmed && trimmed !== storeLobby.current?.name && trimmed.length <= 22) {
			storeLobby.updateSettings({ name: trimmed });
		}
	}

	// Lobby name lives in the primary TopBar's contextual center slot (the
	// same mechanism LobbyBrowse uses for its search field), not in this
	// screen's own secondary header — that bar is reserved for the invite
	// code/saves/settings/exit controls.
	$effect(() => {
		storeTopbarContent.current = lobbyNameSlot;
		return () => {
			storeTopbarContent.current = undefined;
		};
	});

	// Anchors the pulse's phase to wall-clock time (negative delay) so seats
	// that mount at different moments (e.g. a player leaving mid-cycle) still
	// blink in sync instead of each restarting its own 3s cycle from zero.
	const PULSE_PERIOD_MS = 3000;
	function syncPulse(node: HTMLElement) {
		node.style.animationDelay = `${-(Date.now() % PULSE_PERIOD_MS)}ms`;
	}
</script>

{#snippet lobbyNameSlot()}
	{#if isEditingName && isHost}
		<input
			class="lobby-name w-full max-w-xs truncate border-none bg-transparent p-0 text-center outline-none [clip-path:none!important]"
			bind:value={editedName}
			onblur={saveName}
			onkeydown={(e) => e.key === "Enter" && saveName()}
			maxlength="22"
			use:focusOnMount
		/>
	{:else if isHost}
		<button
			type="button"
			class="lobby-name block max-w-xs truncate border-none bg-transparent p-0 text-center [clip-path:none!important]"
			onclick={startEditing}
		>
			{storeLobby.current?.name}
		</button>
	{:else}
		<span class="lobby-name block max-w-xs truncate text-center">
			{storeLobby.current?.name}
		</span>
	{/if}
{/snippet}

<div
	class="flex h-full w-full flex-col overflow-hidden bg-cover bg-center"
	style="background-image: url('/assets/bg_full.png'); image-rendering: pixelated;"
>
	<!-- Lobby bar: invite code + saves on the left, settings/exit (icon-only)
	     on the right. The lobby name itself lives in the primary TopBar's
	     contextual center slot (see lobbyNameSlot above), not here. -->
	<header class="shell-topbar-secondary">
		<div class="flex flex-wrap items-center gap-2">
			<div
				class="pixel-bordered flex items-center gap-3 px-4 py-2 text-text-h shadow-[var(--elevation-1)] [--pc-fill:var(--bg)] [--pc-border:var(--border)]"
			>
				<span
					class="font-monogram select-all text-2xl font-bold leading-none tracking-widest text-text-h sm:text-3xl"
				>
					{showInviteCode ? storeLobby.current?.invite_code : "••••••"}
				</span>
				<div class="flex items-center gap-1 border-l border-white/10 pl-2">
					<button
						type="button"
						class="flex items-center leading-none text-text transition-colors hover:text-white"
						onclick={() => (showInviteCode = !showInviteCode)}
						title={showInviteCode
							? m.lobby_hide_code({}, { locale: storeI18n.locale })
							: m.lobby_show_code({}, { locale: storeI18n.locale })}
						aria-label={showInviteCode
							? m.lobby_hide_code({}, { locale: storeI18n.locale })
							: m.lobby_show_code({}, { locale: storeI18n.locale })}
					>
						<i
							class="pia {showInviteCode
								? 'pixelart-icons-font-eye'
								: 'pixelart-icons-font-eye-off'} text-lg"
						></i>
					</button>
					<button
						type="button"
						class="flex items-center leading-none text-text transition-colors hover:text-white"
						onclick={copyInviteLink}
						title={m.lobby_share_link_tooltip({}, { locale: storeI18n.locale })}
						aria-label={m.lobby_share_link_tooltip({}, { locale: storeI18n.locale })}
					>
						<i class="pia pixelart-icons-font-share text-lg"></i>
					</button>
				</div>
			</div>
		</div>

		<div class="ml-auto flex items-center gap-2">
			<button
				class="btn pixel-corners flex h-11 w-11 items-center justify-center p-0"
				onclick={() => (settingsOpen = true)}
				title={m.lobby_settings_tooltip({}, { locale: storeI18n.locale })}
				aria-label={m.lobby_settings_tooltip({}, { locale: storeI18n.locale })}
			>
				<i class="pia pixelart-icons-font-settings-2 text-lg"></i>
			</button>

			<!-- Leaving is a normal lobby action, not a destructive one worth a
			     "danger zone" — it's a plain button right here rather than
			     buried in Settings' danger section (see SettingsSections). Still
			     styled as a danger action (red, logout icon) since it does end
			     the member's presence in the lobby immediately. -->
			<button
				class="btn-danger pixel-corners flex h-11 w-11 items-center justify-center p-0"
				onclick={() => storeLobby.leave()}
				title={m.lobby_exit_tooltip({}, { locale: storeI18n.locale })}
				aria-label={m.lobby_exit_tooltip({}, { locale: storeI18n.locale })}
			>
				<i class="pia pixelart-icons-font-logout text-lg"></i>
			</button>
		</div>
	</header>

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
					{@const isSelf = member.username === storeAuth.username && !member.is_bot}
					<!-- svelte-ignore a11y_no_noninteractive_tabindex -- seat stays an <li> for
						list semantics but is host-interactive (kick/promote) when tabindex is set,
						and self-interactive (ready toggle) for the local player's own seat -->
					<li
						class="seat-card group relative w-32 shrink-0 sm:w-36 lg:w-44"
						class:cursor-context-menu={isHost && !member.is_host}
						class:cursor-pointer={isSelf}
						style="aspect-ratio: 1 / 1.5357; --card-color: {color};"
						role={isHost && !member.is_host ? "button" : isSelf ? "button" : undefined}
						tabindex={isHost && !member.is_host ? 0 : isSelf ? 0 : undefined}
						aria-pressed={isSelf ? member.is_ready : undefined}
						onpointerdown={(e) => (pointerKind = e.pointerType)}
						oncontextmenu={(e) => handleSeatMenu(member, e)}
						onclick={(e) => {
							if (pointerKind === "touch") handleSeatMenu(member, e);
							if (isSelf) storeLobby.toggleReady();
						}}
						onkeydown={(e) => {
							if (e.key === "Enter" || e.key === " ") {
								handleSeatMenu(member, e);
								if (isSelf) storeLobby.toggleReady();
							}
						}}
					>
						<div
							class="absolute inset-0 overflow-hidden rounded-[0.8em] shadow-[var(--elevation-1)]"
						>
							<img
								src="/assets/cards/background.png"
								alt=""
								class="absolute inset-0 h-full w-full object-fill"
							/>
							<div class="absolute inset-[14%]">
								{#if member.is_bot}
									<img src="/assets/bot_animated.gif" alt="" class="h-full w-full object-contain" />
								{:else}
									<TintedSprite src="/assets/base_player.gif" {color} fit="contain" />
								{/if}

								<!-- Deco layer: sits on the seat's face art, sized to match it
								     exactly. Host's crown lives here so future cosmetics can
								     stack the same way. -->
								{#if member.is_host}
									<img
										src="/assets/crown_host.gif"
										alt={m.lobby_host_badge_alt({}, { locale: storeI18n.locale })}
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
								<i class="pia pixelart-icons-font-robot" style="color: lightblue"></i>
							{/if}
							<span class="truncate">{member.username}</span>
						</p>

						{#if member.username === storeAuth.username && !member.is_bot}
							<button
								class="btn-secondary absolute left-1 top-1 z-20 flex h-6 w-6 items-center justify-center p-0 {member.is_ready
									? 'text-success'
									: 'text-gold'}"
								aria-pressed={member.is_ready}
								title={member.is_ready
									? m.lobby_ready_confirmed({}, { locale: storeI18n.locale })
									: m.lobby_ready_prompt({}, { locale: storeI18n.locale })}
								aria-label={member.is_ready
									? m.lobby_ready_confirmed({}, { locale: storeI18n.locale })
									: m.lobby_ready_prompt({}, { locale: storeI18n.locale })}
								onclick={(e) => {
									e.stopPropagation();
									storeLobby.toggleReady();
								}}
							>
								<i
									class="pia {member.is_ready
										? 'pixelart-icons-font-check'
										: 'pixelart-icons-font-clock'} text-sm leading-none"
								></i>
							</button>
						{:else}
							<span
								class="pointer-events-none absolute left-1 top-1 z-20 flex h-6 w-6 items-center justify-center bg-black/60 {member.is_ready
									? 'text-success'
									: 'text-gold'}"
								title={member.is_ready
									? m.lobby_ready_status({}, { locale: storeI18n.locale })
									: m.lobby_waiting({}, { locale: storeI18n.locale })}
								aria-label={member.is_ready
									? m.lobby_ready_status({}, { locale: storeI18n.locale })
									: m.lobby_waiting({}, { locale: storeI18n.locale })}
							>
								<i
									class="pia {member.is_ready
										? 'pixelart-icons-font-check'
										: 'pixelart-icons-font-clock'} text-sm leading-none"
								></i>
							</span>
						{/if}

						{#if activeMenu === member.username}
							<div class="absolute right-1 top-1 z-30 min-w-[140px] border-2 border-border bg-bg">
								<button
									class="w-full px-3 py-2.5 text-left text-sm font-bold transition-[background,filter] hover:bg-white/10 hover:shadow-[inset_4px_0_0_var(--accent)]"
									style="color: lightgoldenrodyellow"
									onclick={() => {
										storeLobby.promote(member.username);
										activeMenu = null;
									}}
								>
									{m.lobby_promote({}, { locale: storeI18n.locale })}
								</button>
								<button
									class="w-full px-3 py-2.5 text-left text-sm font-bold transition-[background,filter] hover:bg-white/10 hover:shadow-[inset_4px_0_0_var(--accent)]"
									style="color: lightsalmon"
									onclick={() => {
										storeLobby.kick(member.username);
										activeMenu = null;
									}}
								>
									{m.lobby_kick({}, { locale: storeI18n.locale })}
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
						{#if isHost}
							<button
								type="button"
								class="seat-empty group absolute inset-0 w-full h-full overflow-hidden rounded-[0.8em] shadow-[var(--elevation-1)] border-none p-0 cursor-pointer disabled:cursor-not-allowed"
								style="filter: grayscale(0.55) brightness(0.85);"
								disabled={isAddingBot ||
									(storeLobby.current?.members.length ?? 0) >=
										(storeLobby.current?.settings.max_players ?? 4)}
								onclick={handleAddBot}
								title={m.lobby_add_bot({}, { locale: storeI18n.locale })}
								aria-label={m.lobby_add_bot({}, { locale: storeI18n.locale })}
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
								<div class="absolute inset-0 flex items-center justify-center z-10">
									<i
										class="pia pixelart-icons-font-plus text-3xl text-white/70 transition-transform group-hover:scale-125 group-hover:text-white"
									></i>
								</div>
							</button>
						{:else}
							<div
								class="seat-empty absolute inset-0 overflow-hidden rounded-[0.8em] shadow-[var(--elevation-1)]"
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
						{/if}
						<p
							class="absolute inset-x-0 -bottom-5 text-center font-tiny text-[10px] uppercase text-text sm:text-xs"
						>
							{m.lobby_waiting({}, { locale: storeI18n.locale })}
						</p>
					</li>
				{/each}
			</ul>

			<div class="flex flex-col items-center justify-center">
				<button
					class="start-button flex items-center justify-center border-none bg-transparent p-0"
					onclick={() => storeLobby.startMatch()}
					disabled={!isHost || !storeLobby.startEligibility.canStart || storeLobby.isLoadingStart}
					aria-describedby="start-eligibility-reason"
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
				{#if storeLobby.startEligibility.reason}
					<p id="start-eligibility-reason" class="mt-2 text-center font-tiny text-xs text-text">
						{storeLobby.startEligibility.reason}
					</p>
				{/if}
			</div>
		</div>
	</div>
</div>

{#if settingsOpen}
	<Modal
		bind:open={settingsOpen}
		ariaLabel={m.lobby_settings_tooltip({}, { locale: storeI18n.locale })}
		contentClass="pixel-corners relative flex max-h-[85vh] w-full max-w-xl flex-col overflow-y-auto p-5 sm:p-7"
	>
		<button
			class="absolute right-3 top-3 text-2xl text-text hover:text-text-h"
			title={m.settings_close({}, { locale: storeI18n.locale })}
			aria-label={m.settings_close({}, { locale: storeI18n.locale })}
			onclick={() => (settingsOpen = false)}><i class="pia pixelart-icons-font-close"></i></button
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

	/* Seat cards are responsively sized (w-32/36/44) and TintedSprite here
	   fills either the seat's avatar cutout or the whole card as a border
	   overlay — no fixed step matches, so keep it filling its wrapper like
	   before. Overrides TintedSprite's inline width/height, hence !important. */
	.seat-card :global(.tinted-sprite) {
		width: 100% !important;
		height: 100% !important;
	}

	/* Lobby name, rendered into the primary TopBar's contextual center slot.
	   TinyUnicode (--micro) covers far more scripts than Pypx, so it's the
	   first choice for arbitrary player-entered lobby names; bolded via
	   font-weight since the family ships only one weight. If this renders
	   broken glyphs for some scripts in practice, swap to .pypx-thick
	   (app.css) instead — Latin-only but guaranteed to render cleanly. */
	:global(.lobby-name) {
		font-family: var(--micro);
		font-weight: 700;
		font-size: 1.25rem;
		color: var(--text-h);
		text-shadow: 2px 2px 0 var(--pixel-shadow);
	}
	@media (min-width: 640px) {
		:global(.lobby-name) {
			font-size: 1.5rem;
		}
	}
</style>
