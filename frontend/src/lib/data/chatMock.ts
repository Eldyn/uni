/**
 * @file chatMock.ts
 * @brief Fixture data for the chat dock mockup — no backend wiring yet.
 * See memory `chat-system-design` for the real (server-backed) design.
 */

export type ChatFriendStatus = "offline" | "online" | "in-game";

export interface ChatFriend {
	id: string;
	name: string;
	status: ChatFriendStatus;
	color: string;
}

export interface ChatLine {
	id: string;
	username: string;
	color: string;
	text: string;
}

export const CHAT_FRIENDS: ChatFriend[] = [
	{ id: "f1", name: "Bianca", status: "online", color: "#f97373" },
	{ id: "f2", name: "Renn", status: "in-game", color: "#60a5fa" },
	{ id: "f3", name: "Suki", status: "offline", color: "#4ade80" },
	{ id: "f4", name: "Otto", status: "offline", color: "#facc15" }
];

export const CHAT_MOCK_GLOBAL: ChatLine[] = [
	{ id: "mock-g1", username: "Bianca", color: "#f97373", text: "anyone up for a game?" },
	{
		id: "mock-g2",
		username: "Renn",
		color: "#60a5fa",
		text: "*yawns* just woke up, give me a sec"
	},
	{
		id: "mock-g3",
		username: "Otto",
		color: "#facc15",
		text: "**GG** last round, that draw-4 chain was brutal"
	}
];

export const CHAT_MOCK_PARTY: ChatLine[] = [
	{ id: "mock-p1", username: "Suki", color: "#4ade80", text: "someone grab the last slot" },
	{ id: "mock-p2", username: "Bianca", color: "#f97373", text: "on it" }
];

export const CHAT_MOCK_FRIEND_THREADS: Record<string, ChatLine[]> = {
	f1: [
		{
			id: "mock-f1-1",
			username: "Bianca",
			color: "#f97373",
			text: "hey, saved match still there?"
		},
		{ id: "mock-f1-2", username: "You", color: "#c084fc", text: "*checks* yep, resuming now" }
	],
	f2: [{ id: "mock-f2-1", username: "Renn", color: "#60a5fa", text: "**gg** that was close" }],
	f3: [],
	f4: []
};
