<script lang="ts">
	import { director, setCleanView, togglePlayerHidden, setXRay } from "$stores/director.svelte";
	import { storeGame } from "$stores/game.svelte";
	import { storeLobby } from "$stores/lobby.svelte";

	const settings = $derived(
		(storeLobby.current?.settings ?? {}) as Record<string, unknown>
	);
	const isCreative = $derived(settings["creative"] === true);
	const players = $derived(storeGame.state?.players ?? []);
</script>

{#if isCreative}
	<aside class="director-panel" aria-label="Director controls">
		<span class="director-badge">DIRECTOR</span>
		<label>
			<input
				type="checkbox"
				checked={$director.cleanView}
				onchange={(e) => setCleanView(e.currentTarget.checked)}
			/>
			Clean view
		</label>
		<label>
			<input
				type="checkbox"
				checked={$director.xRay}
				onchange={(e) => setXRay(e.currentTarget.checked)}
			/>
			X-ray hands
		</label>
		<fieldset>
			<legend>Hide players</legend>
			{#each players as p (p.username)}
				<label>
					<input
						type="checkbox"
						checked={$director.hidePlayers.includes(p.username)}
						onchange={() => togglePlayerHidden(p.username)}
					/>
					{p.username}
				</label>
			{/each}
		</fieldset>
	</aside>
{/if}

<style>
	.director-panel {
		position: absolute;
		top: 8px;
		right: 8px;
		z-index: 50;
		display: flex;
		flex-direction: column;
		gap: 6px;
		padding: 8px 10px;
		background: rgb(0 0 0 / 0.65);
		border-radius: 8px;
		font-size: 12px;
	}
	.director-badge {
		font-weight: 700;
		letter-spacing: 0.08em;
	}
</style>
