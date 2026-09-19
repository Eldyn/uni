<script lang="ts">
	import { storeLobby } from "$stores/lobby.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { chatStore } from "$stores/chat.svelte";
	import { storeToast } from "$stores/toast.svelte";
	import { storeTopbarContent } from "$stores/topbarContent.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import { computeSeatLayout } from "$utils/lobbySeatLayout";

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

	// Box the seat grid has to fit inside, measured off the scroll area so the
	// layout can scale cards to whatever room the viewport actually leaves.
	let seatBoxWidth = $state(0);
	let seatBoxHeight = $state(0);

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

	// Removing a bot is a bot-count decrement, not a targeted kick: the server
	// decides which seat empties, and no "kicked" toast fires for bots.
	async function handleRemoveBot() {
		if (!isHost || isAddingBot) return;
		const current = storeLobby.current;
		if (!current) return;
		const currentBotCount = current.settings.bot_count ?? 0;
		if (currentBotCount <= 0) return;

		isAddingBot = true;
		try {
			await storeLobby.updateSettings({ bot_count: currentBotCount - 1 });
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
	// with more than 4 seats cycle through them.
	const SEAT_COLORS = [
		"var(--blueCard)",
		"var(--greenCard)",
		"var(--redCard)",
		"var(--yellowCard)"
	];

	const SEAT_GAP = 8;

	let tableSeats = $derived(storeLobby.current?.settings.max_players ?? SEAT_COLORS.length);
	let emptySeats = $derived(Math.max(0, tableSeats - (storeLobby.current?.members.length ?? 0)));
	let readyCount = $derived(
		(storeLobby.current?.members ?? []).filter((member) => member.is_ready).length
	);
	let memberCount = $derived(storeLobby.current?.members.length ?? 0);

	let seatLayout = $derived(
		computeSeatLayout({
			boxWidth: seatBoxWidth,
			boxHeight: seatBoxHeight,
			count: tableSeats,
			gap: SEAT_GAP
		})
	);

	let friendNames = $derived(new Set(chatStore.friends.map((friend) => friend.username)));
	function isFriend(username: string): boolean {
		return friendNames.has(username);
	}

	// Badge/plus glyphs scale with the computed card width. Emitted as concrete
	// px (not a CSS calc on a custom property) so jsdom can resolve font sizes
	// when tests compute accessible names.
	let badgeFontSize = $derived(`${Math.round(seatLayout.cardWidth * 0.3)}px`);
	// Bots use a smaller mark so their remove "X" can't be mistaken for a
	// human's not-ready state.
	let botBadgeFontSize = $derived(`${Math.round(seatLayout.cardWidth * 0.2)}px`);
	let plusFontSize = $derived(`${Math.round(seatLayout.cardWidth * 0.6)}px`);

	function handleSeatMenu(
		member: { username: string; is_host: boolean; is_bot: boolean },
		event: Event
	) {
		if (!isHost || member.is_host || member.is_bot) return;
		event.preventDefault();
		event.stopPropagation();
		activeMenu = activeMenu === member.username ? null : member.username;
	}

	function openMessage(username: string) {
		chatStore.selectChannel({ friendId: username });
		chatStore.open();
		activeMenu = null;
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

	// Desktop and mobile both render the lobby name in the secondary header, so
	// the primary TopBar's centre slot stays free. On mobile the settings and
	// exit controls are promoted into the TopBar's left cluster instead (see
	// lobbyActionsSlot).
	$effect(() => {
		storeTopbarContent.actions = lobbyActionsSlot;
		return () => {
			storeTopbarContent.actions = undefined;
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
			class="lobby-name w-full max-w-full truncate border-none bg-transparent p-0 text-center outline-none md:text-left [clip-path:none!important]"
			bind:value={editedName}
			onblur={saveName}
			onkeydown={(e) => e.key === "Enter" && saveName()}
			maxlength="22"
			use:focusOnMount
		/>
	{:else if isHost}
		<button
			type="button"
			class="lobby-name block w-full max-w-full truncate border-none bg-transparent p-0 text-center md:text-left [clip-path:none!important]"
			onclick={startEditing}
		>
			{storeLobby.current?.name}
		</button>
	{:else}
		<span class="lobby-name block w-full max-w-full truncate text-center md:text-left"
			>{storeLobby.current?.name}</span
		>
	{/if}
{/snippet}

{#snippet lobbyActionsSlot()}
	<button
		class="btn pixel-corners flex h-11 w-11 items-center justify-center p-0"
		onclick={() => (settingsOpen = true)}
		title={m.lobby_settings_tooltip({}, { locale: storeI18n.locale })}
		aria-label={m.lobby_settings_tooltip({}, { locale: storeI18n.locale })}
	>
		<i class="pia pixelart-icons-font-settings-2 text-lg"></i>
	</button>
	<button
		class="btn-danger pixel-corners flex h-11 w-11 items-center justify-center p-0"
		onclick={() => storeLobby.leave()}
		title={m.lobby_exit_tooltip({}, { locale: storeI18n.locale })}
		aria-label={m.lobby_exit_tooltip({}, { locale: storeI18n.locale })}
	>
		<i class="pia pixelart-icons-font-logout text-lg"></i>
	</button>
{/snippet}

<div
	class="flex h-full w-full flex-col overflow-hidden bg-cover bg-center"
	style="background-image: url('/assets/bg_full.png'); image-rendering: pixelated;"
>
	<!-- Lobby bar: name pinned left, invite code pinned right. On mobile the
	     settings/exit controls live up in the TopBar's left cluster instead. -->
	<header class="shell-topbar-secondary flex-nowrap">
		<div class="flex min-w-0 flex-1 items-center gap-2">
			{@render lobbyNameSlot()}
		</div>

		<div class="ml-auto flex shrink-0 items-center gap-2">
			<div
				class="pixel-bordered flex items-center gap-3 px-4 py-2 text-text-h shadow-[var(--elevation-1)] [--pc-fill:var(--bg)] [--pc-border:var(--border)]"
			>
				<span
					class="font-monogram inline-block min-w-[7ch] select-all text-center text-2xl font-bold leading-none tracking-widest text-text-h sm:text-3xl"
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

			<div class="hidden items-center gap-2 md:flex">
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
		</div>
	</header>

	<!-- Table: dealt seats, sized to fit the remaining box ------------------- -->
	<div
		class="scrollbar-accent flex min-h-0 flex-1 justify-center overflow-y-auto"
		bind:clientWidth={seatBoxWidth}
		bind:clientHeight={seatBoxHeight}
	>
		<div class="flex min-h-full w-full items-center justify-center px-3 py-4 sm:px-6">
			<ul
				class="hand grid list-none justify-center p-0"
				class:fan={tableSeats <= 4}
				style="grid-template-columns: repeat({seatLayout.cols}, {seatLayout.cardWidth}px); gap: {SEAT_GAP}px; --card-w: {seatLayout.cardWidth}px;"
			>
				{#each storeLobby.current?.members ?? [] as member, i}
					{@const color = member.is_bot ? "var(--blackCard)" : SEAT_COLORS[i % SEAT_COLORS.length]}
					{@const isSelf = member.username === storeAuth.username && !member.is_bot}
					{@const interactive = isSelf || (isHost && !member.is_host && !member.is_bot)}
					<!-- svelte-ignore a11y_no_noninteractive_tabindex -- seat stays an <li> for
						list semantics but is host-interactive (kick/promote) when tabindex is set,
						and self-interactive (ready toggle) for the local player's own seat -->
					<li
						class="seat-item group relative flex flex-col items-center"
						class:cursor-context-menu={isHost && !member.is_host && !member.is_bot}
						class:cursor-pointer={isSelf}
						role={interactive ? "button" : undefined}
						tabindex={interactive ? 0 : undefined}
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
							class="seat-card relative w-full overflow-hidden rounded-[0.8em] shadow-[var(--elevation-1)]"
							style="aspect-ratio: 1 / 1.5357; --card-color: {color};"
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

							{#if member.is_bot}
								{#if isHost}
									<button
										class="seat-badge text-danger transition-transform hover:scale-110"
										style="font-size: {botBadgeFontSize}"
										title={m.lobby_kick({}, { locale: storeI18n.locale })}
										aria-label={m.lobby_kick({}, { locale: storeI18n.locale })}
										onclick={(e) => {
											e.stopPropagation();
											handleRemoveBot();
										}}
									>
										<i class="pia pixelart-icons-font-close leading-none"></i>
									</button>
								{:else}
									<span class="seat-badge text-text" style="font-size: {botBadgeFontSize}">
										<i class="pia pixelart-icons-font-robot leading-none"></i>
									</span>
								{/if}
							{:else if isSelf}
								<button
									class="seat-badge {member.is_ready ? 'text-success' : 'text-gold'}"
									style="font-size: {badgeFontSize}"
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
											: 'pixelart-icons-font-clock'} leading-none"
									></i>
								</button>
							{:else}
								<span
									class="seat-badge {member.is_ready ? 'text-success' : 'text-gold'}"
									style="font-size: {badgeFontSize}"
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
											: 'pixelart-icons-font-clock'} leading-none"
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
									{#if isFriend(member.username)}
										<button
											class="w-full px-3 py-2.5 text-left text-sm font-bold transition-[background,filter] hover:bg-white/10 hover:shadow-[inset_4px_0_0_var(--accent)]"
											style="color: lightblue"
											onclick={() => openMessage(member.username)}
										>
											{m.lobby_message({}, { locale: storeI18n.locale })}
										</button>
									{/if}
								</div>
							{/if}
						</div>

						<p
							class="mt-4 w-full truncate text-center font-tiny text-[10px] uppercase text-white sm:text-xs"
						>
							<span class="truncate">{member.username}</span>
						</p>
					</li>
				{/each}

				{#each Array(emptySeats) as _, idx}
					{@const seatIndex = (storeLobby.current?.members.length ?? 0) + idx}
					{@const color = SEAT_COLORS[seatIndex % SEAT_COLORS.length]}
					<li class="seat-item relative flex flex-col items-center">
						<div
							class="seat-card relative w-full"
							style="aspect-ratio: 1 / 1.5357; --card-color: {color};"
						>
							{#if isHost}
								<button
									type="button"
									class="seat-empty group/empty absolute inset-0 h-full w-full cursor-pointer overflow-hidden rounded-[0.8em] border-none p-0 shadow-[var(--elevation-1)] disabled:cursor-not-allowed"
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
									<span class="absolute inset-0 z-10 flex items-center justify-center">
										<i
											class="seat-plus pia pixelart-icons-font-plus text-white/75 transition-transform group-hover/empty:scale-125 group-hover/empty:text-white"
											style="font-size: {plusFontSize}"
										></i>
									</span>
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
						</div>
						<p
							class="mt-4 w-full truncate text-center font-tiny text-[10px] uppercase text-white/70 sm:text-xs"
						>
							{m.lobby_waiting({}, { locale: storeI18n.locale })}
						</p>
					</li>
				{/each}
			</ul>
		</div>
	</div>

	<!-- Pinned action bar: never scrolls, so START is always reachable. No bar
	     chrome so it reads as breathing over the felt. A 1fr/auto/1fr grid keeps
	     START dead centre while the ready readout gets its own cell, so the two
	     can never overlap on narrow screens (the readout truncates instead). -->
	<footer class="grid shrink-0 grid-cols-[1fr_auto_1fr] items-center px-4 py-3">
		<div class="flex min-w-0 items-center gap-2 font-tiny text-xs text-white/85 sm:text-sm">
			<i class="pia pixelart-icons-font-check shrink-0 opacity-70" aria-hidden="true"></i>
			<span class="truncate"
				>{m.lobby_ready_status({}, { locale: storeI18n.locale })}
				{readyCount}/{memberCount}</span
			>
		</div>

		<div class="flex flex-col items-center">
			<button
				class="start-button flex items-center justify-center border-none bg-transparent p-0"
				onclick={() => storeLobby.startMatch()}
				disabled={!isHost || !storeLobby.startEligibility.canStart || storeLobby.isLoadingStart}
				aria-describedby="start-eligibility-reason"
			>
				<span class="start-label flex items-center justify-center">
					<TextEffects
						text={m.lobby_start_button({}, { locale: storeI18n.locale })}
						effect="undulate"
						class="start-letters"
						font="var(--heading)"
						amplitude={14}
						speed={1.8}
						frequency={0.15}
					/>
				</span>
			</button>
			{#if storeLobby.startEligibility.reason}
				<p id="start-eligibility-reason" class="sr-only">
					{storeLobby.startEligibility.reason}
				</p>
			{/if}
		</div>

		<div aria-hidden="true"></div>
	</footer>
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

	/* Same rendering path as the home title: one undulating word, line-height 1,
	   no per-glyph stroke. Letter-spacing/stroke on the animated spans made the
	   pixel glyphs read as stretched mid-tween. */
	:global(.start-letters) {
		font-family: var(--heading);
		font-size: var(--text-display);
		/* Enough leading that the "!" descender stays inside its own (composited)
		   char box — line-height: 1 clipped it mid-glyph. */
		line-height: 1.35;
		color: #fff;
		text-shadow: 2px 2px 0 var(--pixel-shadow);
	}
	/* Narrow phones: shrink START so the ready readout keeps its own space and
	   the two never crowd each other in the pinned bar. */
	@media (max-width: 520px) {
		:global(.start-letters) {
			font-size: 1.5rem;
		}
	}
	@media (max-width: 400px) {
		:global(.start-letters) {
			font-size: 1.25rem;
		}
	}
	@media (max-width: 340px) {
		:global(.start-letters) {
			font-size: 1.1rem;
		}
	}

	/* Status glyph in the card's top-right corner: ready check / waiting clock
	   for humans, remove X / robot for bots. No chrome of its own — a hard
	   pixel drop-shadow keeps it legible straight on the card art. */
	.seat-badge {
		position: absolute;
		right: 8%;
		top: 6%;
		z-index: 20;
		display: flex;
		align-items: center;
		justify-content: center;
		filter: drop-shadow(2px 2px 0 var(--pixel-shadow));
	}

	.seat-plus {
		line-height: 1;
	}

	/* Seats read as a fanned, dealt hand only for small lobbies on desktop: the
	   outer cards rotate outward AND drop, so the row curves into an arc. The
	   whole item (card + name) transforms, so the label rides along. Dense/large
	   lobbies and mobile keep them upright for legibility. */
	.fan .seat-item:nth-child(1) {
		--seat-tilt: -8deg;
		--seat-drop: 26px;
	}
	.fan .seat-item:nth-child(2) {
		--seat-tilt: -3deg;
		--seat-drop: 7px;
	}
	.fan .seat-item:nth-child(3) {
		--seat-tilt: 3deg;
		--seat-drop: 7px;
	}
	.fan .seat-item:nth-child(4) {
		--seat-tilt: 8deg;
		--seat-drop: 26px;
	}

	@media (min-width: 1024px) {
		.fan .seat-item {
			transform: translateY(var(--seat-drop, 0px)) rotate(var(--seat-tilt, 0deg));
			transition: transform 0.15s ease;
		}
		.fan .seat-item:hover {
			transform: translateY(calc(var(--seat-drop, 0px) - 10px)) rotate(0deg);
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
		.fan .seat-item {
			transition: none;
		}
		.seat-empty {
			animation: none;
			opacity: 0.7;
		}
	}

	/* Seat cards are responsively sized (computed card width) and TintedSprite
	   here fills either the seat's avatar cutout or the whole card as a border
	   overlay — no fixed step matches, so keep it filling its wrapper like
	   before. Overrides TintedSprite's inline width/height, hence !important. */
	.seat-card :global(.tinted-sprite) {
		width: 100% !important;
		height: 100% !important;
	}

	/* Lobby name, rendered into the primary TopBar on desktop and the secondary
	   header on mobile. TinyUnicode (--micro) covers far more scripts than Pypx,
	   so it's the first choice for arbitrary player-entered lobby names; bolded
	   via font-weight since the family ships only one weight. If this renders
	   broken glyphs for some scripts in practice, swap to .pypx-thick
	   (app.css) instead — Latin-only but guaranteed to render cleanly. */
	:global(.lobby-name) {
		font-family: var(--micro);
		font-weight: 700;
		font-size: 1rem;
		color: var(--text-h);
		text-shadow: 2px 2px 0 var(--pixel-shadow);
	}
	@media (min-width: 640px) {
		:global(.lobby-name) {
			font-size: 1.125rem;
		}
	}
</style>
