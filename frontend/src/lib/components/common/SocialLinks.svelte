<!-- Row of social profile icons. An icon that fails to load (blocked, offline,
     404) is replaced by the profile's name in the tiny pixel font, so the
     link never degrades to a clipped alt string. -->
<script lang="ts">
	import { SOCIAL_LINKS } from "$data/socialLinks";

	let { class: className = "" }: { class?: string } = $props();

	let failedIcons = $state<Record<string, boolean>>({});
</script>

<nav class="social-row {className}" aria-label="Social links">
	{#each SOCIAL_LINKS as link (link.label)}
		<a
			href={link.href}
			target="_blank"
			rel="noopener noreferrer"
			class="social-link"
			aria-label={link.label}
		>
			{#if failedIcons[link.label]}
				<span class="social-fallback">{link.label}</span>
			{:else}
				<img
					src={link.icon}
					alt={link.label}
					width="32"
					height="32"
					onerror={() => (failedIcons[link.label] = true)}
				/>
			{/if}
		</a>
	{/each}
</nav>

<style>
	.social-row {
		display: flex;
		flex-wrap: wrap;
		align-items: center;
		gap: var(--space-4);
	}

	.social-link {
		display: inline-flex;
		align-items: center;
		opacity: 0.5;
		image-rendering: pixelated;
		transition: opacity var(--duration-fast, 0.15s) var(--ease-standard, ease);
	}
	.social-link:hover {
		opacity: 1;
	}

	.social-fallback {
		font-family: var(--tiny);
		font-size: 14px;
		line-height: 1;
		color: var(--text-h);
	}
</style>
