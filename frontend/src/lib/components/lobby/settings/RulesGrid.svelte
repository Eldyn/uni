<script lang="ts">
	import Tooltip from "$components/common/Tooltip.svelte";
	import RichText from "$components/common/RichText.svelte";
	import PixelCheckbox from "$components/common/PixelCheckbox.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import { storeI18n } from "$stores/i18n.svelte";
	import { glossaryTag } from "$lib/glossary/glossary";

	export interface RuleDef {
		id: string;
		label: string;
		description: string;
		enabled: boolean;
	}

	let {
		rules,
		disabled = false,
		onrulechange
	}: {
		rules: RuleDef[];
		disabled?: boolean;
		onrulechange: (id: string, enabled: boolean) => void;
	} = $props();
</script>

<div class="rules-section">
	<span class="rules-header">Custom Rules</span>
	<div class="rules-grid">
		{#each rules as rule (rule.id)}
			<Tooltip
				interactive={true}
				title={rule.label}
				tags={[glossaryTag("rule", m.glossary_tag_rule({}, { locale: storeI18n.locale }))]}
			>
				{#snippet tooltipContent()}
					<RichText text={rule.description} allowKeywords={true} />
				{/snippet}

				<PixelCheckbox
					label={rule.label}
					checked={rule.enabled}
					{disabled}
					oncommit={(v) => onrulechange(rule.id, v)}
				/>
			</Tooltip>
		{/each}
	</div>
</div>

<style>
	.rules-section {
		display: flex;
		flex-direction: column;
		gap: 10px;
	}

	.rules-header {
		font-family: var(--tiny);
		font-size: 15px;
		color: var(--text-h);
		font-weight: 500;
	}

	.rules-grid {
		display: grid;
		grid-template-columns: 1fr 1fr;
		gap: var(--space-3);
	}
</style>
