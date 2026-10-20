<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import BlankIcon from "$components/common/BlankIcon.svelte";
	import { localEarning } from "./matchEarnings";
	import * as m from "$lib/paraglide/messages.js";

	let amount = $derived(
		localEarning(storeGame.state?.payouts, storeAuth.username, storeAuth.isGuest === true)
	);
</script>

{#if amount !== null}
	<p class="earnings-line font-pixel" data-earned={amount}>
		<BlankIcon />
		{m.game_end_earned({ amount }, { locale: storeI18n.locale })}
	</p>
{/if}

<style>
	.earnings-line {
		display: inline-flex;
		align-items: center;
		gap: var(--space-2);
		margin: 0;
		color: var(--accent);
	}
</style>
