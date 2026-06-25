<script lang="ts">
	import { onMount } from "svelte";
	export let show: boolean = false;

	onMount(() => {
		if (show) {
			try {
				// @ts-ignore
				(window.adsbygoogle = window.adsbygoogle || []).push({});
			} catch (e) {
				console.error(e);
			}
		}
	});
</script>

{#if show}
	<div class="ad-banner">
		<ins class="adsbygoogle"
			style="display:block"
			data-ad-client="ca-pub-7899204097948216"
			data-ad-format="auto"
			data-full-width-responsive="true"></ins>
	</div>
{/if}

<style>
	.ad-banner {
		width: 100%;
		text-align: center;
		margin-top: 1rem;
	}
</style>
