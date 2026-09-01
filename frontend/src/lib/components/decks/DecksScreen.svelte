<script lang="ts">
	import { storeNavigation } from "$stores/navigation.svelte";
</script>

<div class="flex flex-1 flex-col items-center justify-center gap-4 p-8 text-center">
	<h1 class="title-screen">Decks</h1>
	<p class="max-w-md font-tiny text-sm text-text">
		Build and share your own card sets. Coming soon.
	</p>
	<button class="btn pixel-corners px-5 py-3" onclick={() => storeNavigation.goto("main")}>
		Back to Home
	</button>
</div>
