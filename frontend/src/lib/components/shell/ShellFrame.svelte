<script lang="ts">
	import TopBar from "./TopBar.svelte";
	import NavBar from "./NavBar.svelte";
	import type { Snippet } from "svelte";
	import { onMount } from "svelte";
	import { initKeyboardAccelerators } from "$lib/actions/keyboardAccelerators";

	let { children }: { children: Snippet } = $props();

	onMount(() => initKeyboardAccelerators());
</script>

<div class="shell-frame">
	<TopBar />
	<div class="shell-body">
		<NavBar />
		<main class="shell-content">
			{@render children()}
		</main>
	</div>
</div>

<style>
	.shell-frame {
		display: flex;
		flex-direction: column;
		height: 100%;
		width: 100%;
	}
	.shell-body {
		display: flex;
		flex: 1;
		min-height: 0;
		/* Bottom-bar layouts stack nav below content; rail layouts sit it
		   beside content. Order flips at the same breakpoint NavBar itself
		   switches shape at, so the two never disagree. */
		flex-direction: column-reverse;
	}
	.shell-content {
		flex: 1;
		min-height: 0;
		overflow-y: auto;
	}

	@media (min-width: 768px), (max-height: 599px) {
		.shell-body {
			flex-direction: row;
		}
	}
</style>
