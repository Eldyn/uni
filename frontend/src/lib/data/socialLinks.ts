/**
 * @file socialLinks.ts
 * @brief The project's social profiles, shared by every place that links to them.
 */

import { assetUrl } from "$lib/utils/assetUrl";

export interface SocialLink {
	label: string;
	href: string;
	icon: string;
}

const socialIcon = (fileName: string): string => assetUrl(`/assets/social/${fileName}`);

export const SOCIAL_LINKS: readonly SocialLink[] = [
	{ label: "GitHub", href: "https://github.com/Eldyn/uni", icon: socialIcon("github_icon.png") },
	{
		label: "Discord",
		href: "https://discord.gg/QYJvfWqG5e",
		icon: socialIcon("discord_icon.png")
	},
	{
		label: "Bluesky",
		href: "https://bsky.app/profile/did:plc:pnfiqgr56esaantendnklouz",
		icon: socialIcon("bluesky_icon.png")
	},
	{ label: "X", href: "https://x.com/theunigamee", icon: socialIcon("x_icon.png") },
	{
		label: "YouTube",
		href: "https://youtube.com/@play-uni",
		icon: socialIcon("youtube_icon.png")
	},
	{
		label: "Instagram",
		href: "https://www.instagram.com/the.uni.game/",
		icon: socialIcon("instagram_icon.png")
	},
	{
		label: "TikTok",
		href: "https://tiktok.com/@the.uni.game",
		icon: socialIcon("tiktok_icon.png")
	}
];
