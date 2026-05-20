/**
 * Game state management for lobbies, rooms, and game data
 */

export interface Lobby {
	id: string;
	name: string;
	host: string;
	players: string[];
	maxPlayers: number;
	status: 'waiting' | 'in_progress' | 'finished';
	createdAt: number;
}

export interface GameState {
	currentLobby: Lobby | null;
	availableLobbies: Lobby[];
	isLoadingLobbies: boolean;
	gameData: Record<string, unknown>;
}

let gameState = $state<GameState>({
	currentLobby: null,
	availableLobbies: [],
	isLoadingLobbies: false,
	gameData: {},
});

/**
 * Set current lobby
 */
export function setCurrentLobby(lobby: Lobby | null): void {
	gameState.currentLobby = lobby;
}

/**
 * Set available lobbies
 */
export function setAvailableLobbies(lobbies: Lobby[]): void {
	gameState.availableLobbies = lobbies;
}

/**
 * Add lobby to available lobbies
 */
export function addLobby(lobby: Lobby): void {
	gameState.availableLobbies.push(lobby);
}

/**
 * Remove lobby from available lobbies
 */
export function removeLobby(lobbyId: string): void {
	gameState.availableLobbies = gameState.availableLobbies.filter((l) => l.id !== lobbyId);
}

/**
 * Update a lobby in available lobbies
 */
export function updateLobby(lobbyId: string, updates: Partial<Lobby>): void {
	const lobby = gameState.availableLobbies.find((l) => l.id === lobbyId);
	if (lobby) {
		Object.assign(lobby, updates);
	}
}

/**
 * Set loading state for lobbies
 */
export function setLoadingLobbies(loading: boolean): void {
	gameState.isLoadingLobbies = loading;
}

/**
 * Update game data
 */
export function updateGameData(data: Record<string, unknown>): void {
	gameState.gameData = { ...gameState.gameData, ...data };
}

/**
 * Clear game data
 */
export function clearGameData(): void {
	gameState.gameData = {};
}

/**
 * Get current game state (reactive)
 */
export function getGameState(): Readonly<GameState> {
	return gameState;
}
