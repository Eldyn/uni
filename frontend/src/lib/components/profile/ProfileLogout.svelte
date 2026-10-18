<script lang="ts">
	/**
	 * @file ProfileLogout.svelte
	 * @brief Logout button for the profile Account header, same behaviour as
	 * the Settings logout. Renders nothing for guests.
	 */
	import { storeAuth } from "$stores/auth.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let logoutPending = $state(false);

	const options = $derived({ locale: storeI18n.locale });

	async function handleLogout() {
		if (logoutPending) return;
		logoutPending = true;
		try {
			await storeAuth.logout();
		} finally {
			logoutPending = false;
		}
	}
</script>

{#if storeAuth.isLoggedIn}
	<button class="btn-danger btn-sm logout-btn" disabled={logoutPending} onclick={handleLogout}>
		<i class="pia pixelart-icons-font-logout" aria-hidden="true"></i>
		{m.home_logout({}, options)}
	</button>
{/if}
