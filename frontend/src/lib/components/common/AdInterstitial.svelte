<script lang="ts">
	import { onMount } from "svelte";
	export let show: boolean = false;

	let visible = show;

	$: if (show && !visible) {
		visible = true;
		startTimer();
		pushAd();
	}

	function pushAd() {
		try {
			// @ts-ignore
			(window.adsbygoogle = window.adsbygoogle || []).push({});
		} catch (e) {
			console.error(e);
		}
	}

	function startTimer() {
		setTimeout(() => {
			visible = false;
		}, 5000);
	}

	onMount(() => {
		if (show) {
			startTimer();
			pushAd();
		}
	});
</script>

{#if visible}
	<div class="ad-interstitial-overlay">
		<div class="ad-interstitial-content">
			<span class="ad-label">Advertisement</span>
			<ins class="adsbygoogle"
				style="display:block"
				data-ad-client="ca-pub-7899204097948216"
				data-ad-format="auto"
				data-full-width-responsive="true"></ins>
			<button class="close-btn" on:click={() => (visible = false)}>Close</button>
		</div>
	</div>
{/if}

<style>
	.ad-interstitial-overlay {
		position: fixed;
		top: 0;
		left: 0;
		width: 100vw;
		height: 100vh;
		background: rgba(0, 0, 0, 0.85);
		z-index: 9999;
		display: flex;
		align-items: center;
		justify-content: center;
	}

	.ad-interstitial-content {
		position: relative;
		background: #fff;
		padding: 2rem;
		border-radius: 8px;
		width: 90%;
		max-width: 800px;
		text-align: center;
	}

	.ad-label {
		display: block;
		margin-bottom: 1rem;
		font-size: 0.8rem;
		color: #666;
		text-transform: uppercase;
		letter-spacing: 1px;
	}

	.close-btn {
		margin-top: 1rem;
		padding: 0.5rem 1rem;
		background: #333;
		color: #fff;
		border: none;
		border-radius: 4px;
		cursor: pointer;
	}
	.close-btn:hover {
		background: #555;
	}
</style>
