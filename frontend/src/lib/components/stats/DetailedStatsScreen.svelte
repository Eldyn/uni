<script lang="ts">
	import { onMount } from "svelte";
	import { storeStats } from "../../stores/stats.svelte";
	import { storeNavigation } from "../../stores/navigation.svelte";
	import GameCard from "../game/GameCard.svelte";

	// 1. Fetch stats if we navigated directly or refreshed the page
	onMount(() => {
		if (!storeStats.myStats) {
			storeStats.fetchMe();
		}
	});

	const COLOR_HEX: Record<string, string> = {
		red: "#dc251c",
		blue: "#0493de",
		green: "#018d41",
		yellow: "#fcf604"
	};

	// The canonical list of card types we want to iterate over
	const CARD_TYPES = [
		"red",
		"blue",
		"green",
		"yellow",
		"0",
		"1",
		"2",
		"3",
		"4",
		"5",
		"6",
		"7",
		"8",
		"9",
		"skip",
		"reverse",
		"draw2",
		"colorswitch",
		"draw4"
	];

	let cardStats = $derived.by(() => {
		const stats = [];

		// 2. Loop over the types dynamically instead of relying on the backend JSON keys
		for (const type of CARD_TYPES) {
			// Trick TypeScript into accepting our dynamic string as a key
			const key = `cards_played_${type}` as keyof typeof storeStats.myStats;

			// Safely grab the count. If the backend omitted it (e.g. 0 games played), default to 0.
			const count = (storeStats.myStats?.[key] as number) ?? 0;

			let color = "black";
			let value = type;
			let label = type.toUpperCase();
			let textCol = "#fff";

			// Dynamically assign visual properties based on the type
			if (type in COLOR_HEX) {
				color = type;
				value = "";
				textCol = COLOR_HEX[type];
			} else if (type === "colorswitch" || type === "draw4") {
				color = "wild";
				value = type === "draw4" ? "+4" : "colorswitch";
				label = type === "draw4" ? "WILD +4" : "WILD";
				textCol = "var(--accent)";
			} else if (type === "draw2") {
				value = "+2";
				label = "DRAW 2";
			}

			stats.push({ key, type, color, value, label, textCol, count });
		}

		return stats;
	});
</script>

<div class="doodle-bg"></div>

<div class="stats-container">
	<header class="top-bar">
		<button class="styled-btn" onclick={() => storeNavigation.back()}> ↩ Back </button>
		<h1>CARD ARSENAL</h1>
		<div style="width: 80px;"></div>
	</header>

	<div class="content">
		<div class="cards-grid">
			{#each cardStats as stat (stat.key)}
				<div class="card-stat-box">
					<div class="card-wrapper">
						<!-- ID is 0 as it's just a display prop here -->
						<GameCard
							card={{ id: 0, color: stat.color as any, value: stat.value as any }}
							style="position: relative; left: 0;"
						/>
					</div>
					<div class="stat-info">
						<span class="card-label">{stat.label}</span>
						<span class="card-count" style="color: {stat.textCol};">
							{stat.count}
						</span>
					</div>
				</div>
			{/each}
		</div>
	</div>
</div>

<style>
	.doodle-bg {
		position: fixed;
		inset: 0;
		background-color: #121212;
		/* background-image: url('/assets/doodle-bg.png'); */
		background-size: cover;
		z-index: 0;
	}

	.stats-container {
		position: relative;
		z-index: 1;
		display: flex;
		flex-direction: column;
		height: 100vh;
		color: white;
	}

	.top-bar {
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding: 20px;
		background-color: #1c1c1e;
		border-bottom: 4px solid #333;
	}

	.top-bar h1 {
		margin: 0;
		font-size: 1.5rem;
		color: var(--accent);
		text-shadow: 2px 2px 0px #000;
		font-family: "Pixel", sans-serif;
	}

	/* Your requested button styling */
	.styled-btn {
		padding: 8px 16px;
		background: var(--accent);
		color: white;
		border: none;
		border-radius: 6px;
		font-size: 14px;
		font-weight: 500;
		cursor: pointer;
		transition: opacity 0.2s;
		font-family: "Pixel", sans-serif;
	}

	.styled-btn:hover {
		opacity: 0.8;
	}

	.content {
		flex: 1;
		overflow-y: auto;
		padding: 40px;
		max-width: 1000px;
		margin: 0 auto;
		width: 100%;
	}

	/* SNKRX / Balatro Grid Style */
	.cards-grid {
		display: grid;
		grid-template-columns: repeat(auto-fill, minmax(130px, 1fr));
		gap: 35px;
		justify-items: center;
	}

	.card-stat-box {
		display: flex;
		flex-direction: column;
		align-items: center;
		background: #1c1c1e;
		padding: 15px;
		border-radius: 12px;
		box-shadow: 0 8px 0px #111;
		border: 2px solid #333;
		transition: transform 0.1s ease;
	}

	.card-stat-box:hover {
		transform: translateY(-4px);
	}

	.card-wrapper {
		margin-bottom: 15px;
		transform: scale(0.9); /* Prevents the cards from being overly massive */
	}

	.stat-info {
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 5px;
	}

	.card-label {
		font-size: 0.85rem;
		color: #8e8e93;
		text-transform: uppercase;
		letter-spacing: 1px;
	}

	.card-count {
		font-size: 1.8rem;
		font-weight: 900;
		font-family: "Pixel", sans-serif;
		text-shadow: 2px 2px 0px #000;
	}
</style>
