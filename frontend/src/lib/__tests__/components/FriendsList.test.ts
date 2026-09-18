import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeNavigation } from "$stores/navigation.svelte";

vi.mock("$stores/auth.svelte", () => ({
	storeAuth: {
		isLoggedIn: true,
		isGuest: false,
		emailVerified: false
	}
}));

vi.mock("$stores/navigation.svelte", () => ({
	storeNavigation: {
		openVerifyModal: vi.fn(),
		gotoAuth: vi.fn()
	}
}));

vi.mock("$stores/chat.svelte", () => ({
	chatStore: {
		friends: [],
		incomingRequests: [],
		openDirectMessage: vi.fn(),
		requestFriend: vi.fn(),
		respondToRequest: vi.fn()
	}
}));

vi.mock("$stores/i18n.svelte", () => ({
	storeI18n: {
		locale: "en"
	}
}));

vi.mock("$lib/paraglide/messages.js", () => ({
	chat_friends_guest_notice: vi.fn(() => "Guests cannot add friends."),
	chat_friends_guest_register: vi.fn(() => "Register"),
	chat_friends_unverified_notice: vi.fn(() => "Unverified users cannot add friends."),
	verify_submit_button: vi.fn(() => "Verify"),
	chat_friends_search_placeholder: vi.fn(() => "Add friend by username…"),
	chat_friends_search_aria: vi.fn(() => "Add friend by username"),
	chat_friends_add_button: vi.fn(() => "Add"),
	chat_friends_requests_heading: vi.fn(() => "Requests"),
	chat_friends_accept: vi.fn(() => "Accept"),
	chat_friends_reject: vi.fn(() => "Reject"),
	chat_friends_status_online: vi.fn(() => "Online"),
	chat_friends_status_offline: vi.fn(() => "Offline")
}));

import FriendsList from "$components/chat/FriendsList.svelte";

describe("FriendsList", () => {
	beforeEach(() => {
		vi.restoreAllMocks();
		vi.mocked(storeAuth).isGuest = false;
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
	});

	it("shows unverified notice and opens verify modal on click", async () => {
		render(FriendsList);

		expect(screen.getByText("Unverified users cannot add friends.")).toBeInTheDocument();
		const verifyBtn = screen.getByRole("button", { name: "Verify" });
		expect(verifyBtn).toBeInTheDocument();

		await fireEvent.click(verifyBtn);
		expect(storeNavigation.openVerifyModal).toHaveBeenCalled();
	});

	it("shows guest notice when user is guest", () => {
		vi.mocked(storeAuth).isGuest = true;
		vi.mocked(storeAuth).isLoggedIn = false;

		render(FriendsList);

		expect(screen.getByText("Guests cannot add friends.")).toBeInTheDocument();
		expect(screen.getByRole("button", { name: "Register" })).toBeInTheDocument();
	});

	it("shows search input when user is verified", () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = true;

		render(FriendsList);

		expect(screen.getByPlaceholderText("Add friend by username…")).toBeInTheDocument();
	});
});
