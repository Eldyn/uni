import type { Card, GamePlayer, GameState } from "$stores/game.svelte";
import type { BotTakeoverMode, Lobby, LobbyMember, LobbySettings } from "$stores/lobby.svelte";

export function makeCard(overrides?: Partial<Card>): Card {
	return {
		id: 1,
		type: "red",
		value: "5",
		...overrides
	};
}

export function makePlayer(overrides?: Partial<GamePlayer>): GamePlayer {
	return {
		username: "player1",
		card_count: 7,
		is_bot: false,
		...overrides
	};
}

export function makeGameState(overrides?: Partial<GameState>): GameState {
	const p1 = makePlayer({ username: "player1" });
	const p2 = makePlayer({ username: "player2" });
	return {
		active_type: "red",
		current_turn: "player1",
		play_direction: 1,
		top_card: makeCard(),
		players: [p1, p2],
		pending_draws: 0,
		...overrides
	};
}

export function makeLobby(overrides?: Partial<Lobby>): Lobby {
	const settings: LobbySettings = {
		is_public: false,
		active_mods: [],
		turn_time_limit_ms: 15000,
		save_state: false,
		quit_deletes_match: false,
		starting_cards: 7,
		max_players: 4,
		bot_count: 0,
		bot_mode: 0 as BotTakeoverMode,
		allow_bot_takeover: false,
		allow_bot_replacement: false,
		count_zeros: 1,
		count_numbered: 2,
		count_skips: 2,
		count_reverses: 2,
		count_draw_two: 2,
		count_wild: 4,
		count_wild_draw_four: 4
	};
	const members: LobbyMember[] = [
		{ username: "player1", is_connected: true, is_host: true, is_bot: false },
		{ username: "player2", is_connected: true, is_host: false, is_bot: false }
	];
	return {
		invite_code: "AAAAAA",
		host: "player1",
		name: "Test Lobby",
		members,
		settings,
		...overrides
	};
}

export function makeServerFrame(
	action: string,
	payload?: Record<string, unknown>
): Record<string, unknown> {
	return { action, ...(payload ?? {}) };
}
