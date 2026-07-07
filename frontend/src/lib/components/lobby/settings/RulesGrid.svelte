<script lang="ts">
	import Tooltip from "$components/common/Tooltip.svelte";

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
			<Tooltip>
				{#snippet tooltipContent()}
					<span>{rule.description}</span>
				{/snippet}

				<label class="toggle-label rule-label" class:disabled>
					<input
						type="checkbox"
						checked={rule.enabled}
						{disabled}
						onchange={(e) => onrulechange(rule.id, (e.target as HTMLInputElement).checked)}
					/>
					<span>{rule.label}</span>
				</label>
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
		font-size: 13px;
		color: var(--text-h);
		font-weight: 500;
	}

	.rules-grid {
		display: grid;
		grid-template-columns: 1fr 1fr;
		gap: 8px;
	}

	.toggle-label {
		display: flex;
		align-items: center;
		gap: 8px;
		font-size: 13px;
		font-weight: 500;
		color: var(--text-h);
		cursor: help;
		user-select: none;
	}

	.toggle-label.disabled {
		cursor: not-allowed;
		opacity: 0.6;
	}
</style>
