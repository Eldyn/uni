<script lang="ts">
	/**
	 * @file ProfileView.svelte
	 * @brief Pure profile screen: identity, stats and cards played for any
	 * player. The Account section and its action render only for the owner.
	 */
	import type { Snippet } from "svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import Avatar from "$components/common/Avatar.svelte";
	import Divisor from "$components/common/Divisor.svelte";
	import CardsPlayedChart from "$components/profile/CardsPlayedChart.svelte";
	import {
		cardsPlayedGroups,
		gamesPlayed,
		rankDisplay,
		winRatePercent
	} from "$components/profile/profileModel";
	import type { PlayerStats } from "$stores/stats.svelte";
	import type { Loadout } from "$lib/avatar/types";

	type ProfileViewStatus = "loading" | "ready" | "error" | "guest";

	type ProfileViewProps = {
		profile: { username: string; loadout?: Loadout | null; stats: PlayerStats | null };
		isOwn: boolean;
		status: ProfileViewStatus;
		onRetry?: () => void;
		onLeaderboard?: () => void;
		onRegister?: () => void;
		oneditlook?: () => void;
		account?: Snippet;
		accountAction?: Snippet;
	};

	let {
		profile,
		isOwn,
		status,
		onRetry,
		onLeaderboard,
		onRegister,
		oneditlook,
		account,
		accountAction
	}: ProfileViewProps = $props();

	const EM_DASH = "—";

	const options = $derived({ locale: storeI18n.locale });
	const loading = $derived(status === "loading");
	const groups = $derived(profile.stats ? cardsPlayedGroups(profile.stats) : []);

	/**
	 * Resolve the "Edit look" label, falling back to the English default until
	 * the wardrobe key lands in the i18n catalogue (a later task).
	 */
	function wardrobeLabel(): string {
		const fallback = "Edit look";
		const translate = (m as Record<string, unknown>)["wardrobe_edit_look"];
		if (typeof translate !== "function") return fallback;
		const value = (translate as (inputs: object, opts: { locale: string }) => unknown)({}, options);
		return typeof value === "string" && value.length > 0 ? value : fallback;
	}

	const editLookLabel = $derived(wardrobeLabel());

	/** A stat number, or an em dash while loading or without data. */
	function statValue(value: number | undefined): string {
		if (loading || value === undefined) return EM_DASH;
		return `${value}`;
	}

	const winRate = $derived.by(() => {
		if (loading || !profile.stats) return EM_DASH;
		const percent = winRatePercent(profile.stats);
		return percent === null ? EM_DASH : `${percent}%`;
	});

	const games = $derived.by(() => {
		if (loading || !profile.stats) return EM_DASH;
		return `${gamesPlayed(profile.stats)}`;
	});

	const rank = $derived.by(() => {
		if (loading) return EM_DASH;
		const value = rankDisplay(profile.stats?.rank);
		return value === null ? m.profile_unranked({}, options) : `#${value}`;
	});
</script>

<div class="profile-sections">
	<header class="profile-identity">
		<Avatar loadout={profile.loadout} size={96} />
		<h1 class="title-screen">{profile.username}</h1>
		{#if isOwn}
			<button class="btn-secondary profile-edit-look" type="button" onclick={() => oneditlook?.()}>
				{editLookLabel}
			</button>
		{/if}
	</header>

	{#if status === "guest"}
		<section class="profile-block">
			<p class="profile-guest-cta">{m.profile_guest_cta({}, options)}</p>
			<button class="btn-secondary self-start" type="button" onclick={() => onRegister?.()}>
				{m.profile_guest_register({}, options)}
			</button>
		</section>
	{:else}
		<section class="profile-block">
			<div class="profile-header">
				<Divisor text={m.profile_section_stats({}, options)} />
				<button class="btn-secondary" type="button" onclick={() => onLeaderboard?.()}>
					{m.profile_leaderboard_button({}, options)}
				</button>
			</div>
			{#if status === "error"}
				<div class="profile-error">
					<p>{m.profile_stats_error({}, options)}</p>
					<button class="btn-secondary self-start" type="button" onclick={() => onRetry?.()}>
						{m.profile_retry({}, options)}
					</button>
				</div>
			{:else}
				<div class="profile-stats">
					<div class="stat-cell">
						<span class="stat-label">{m.leaderboard_wins({}, options)}</span>
						<span class="stat-value title-stats">{statValue(profile.stats?.total_wins)}</span>
					</div>
					<div class="stat-cell">
						<span class="stat-label">{m.leaderboard_losses({}, options)}</span>
						<span class="stat-value title-stats">{statValue(profile.stats?.total_losses)}</span>
					</div>
					<div class="stat-cell">
						<span class="stat-label">{m.profile_win_rate({}, options)}</span>
						<span class="stat-value title-stats">{winRate}</span>
					</div>
					<div class="stat-cell">
						<span class="stat-label">{m.profile_games_played({}, options)}</span>
						<span class="stat-value title-stats">{games}</span>
					</div>
					<div class="stat-cell">
						<span class="stat-label">{m.profile_rank({}, options)}</span>
						<span class="stat-value title-stats">{rank}</span>
					</div>
				</div>
			{/if}
		</section>

		{#if status === "ready" && groups.length > 0}
			<section class="profile-block">
				<Divisor text={m.profile_section_cards({}, options)} />
				<CardsPlayedChart {groups} />
			</section>
		{/if}
	{/if}

	{#if isOwn && account}
		<section class="profile-block">
			<div class="profile-header">
				<Divisor text={m.profile_section_account({}, options)} />
				{#if accountAction}
					{@render accountAction()}
				{/if}
			</div>
			{@render account()}
		</section>
	{/if}
</div>

<style>
	.profile-sections {
		display: flex;
		flex-direction: column;
		gap: var(--space-6);
	}

	.profile-identity {
		display: flex;
		align-items: center;
		gap: var(--space-4);
	}

	.profile-edit-look {
		margin-left: auto;
	}

	.profile-block {
		display: flex;
		flex-direction: column;
		gap: var(--space-3);
	}

	.profile-header {
		display: grid;
		grid-template-columns: 1fr auto;
		align-items: center;
		gap: var(--space-3);
	}

	.profile-stats {
		display: grid;
		/* auto-fit wraps the cells at 390 px and in the pseudo locale. */
		grid-template-columns: repeat(auto-fit, minmax(7rem, 1fr));
		gap: var(--space-3);
	}

	.stat-cell {
		display: flex;
		flex-direction: column;
		gap: var(--space-1);
		padding: var(--space-3);
		border: 1px solid var(--border);
	}

	.stat-label {
		font-size: var(--text-small);
		text-transform: uppercase;
		overflow-wrap: anywhere;
	}

	.stat-value {
		overflow-wrap: anywhere;
	}

	.profile-error {
		display: flex;
		flex-direction: column;
		gap: var(--space-3);
	}

	.profile-guest-cta {
		max-width: 32rem;
	}
</style>
