<script lang="ts">
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
</script>

<div class="flex flex-1 flex-col items-center justify-center gap-4 p-8 text-center">
	<h1 class="title-screen">{m.shop_title({}, { locale: storeI18n.locale })}</h1>
	<p class="max-w-md font-tiny text-sm text-text">
		{m.shop_coming_soon({}, { locale: storeI18n.locale })}
	</p>
	<button class="btn pixel-corners px-5 py-3" onclick={() => storeNavigation.goto("main")}>
		{m.common_back_to_home({}, { locale: storeI18n.locale })}
	</button>
</div>
