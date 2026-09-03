<div class="landing-content">
	<section class="landing-section">
		<h2>What is this</h2>
		<p>
			UNI! is a free, open-source online multiplayer card game. Play in your browser with up to
			4 players in real time — a fast, friendly take on the classic colour-and-number card game
			genre.
		</p>
		<p class="landing-legal">
			UNO® is a registered trademark. UNI is similar to UNO but is an independent project not affiliated with or endorsed by any trademark holder.
		</p>
	</section>

	<section class="landing-section landing-screenshot">
		<img src="/assets/link_image.png" alt="A UNI! match in progress, showing the playmat and hand of cards" loading="lazy" />
	</section>

	<section class="landing-section">
		<h2>If you've played UNO</h2>
		<div class="landing-compare">
			<div>
				<h3>Same</h3>
				<ul>
					<li>Match cards by colour or number</li>
					<li>Familiar action cards: Skip, Reverse, Draw Two</li>
					<li>Round ends when your hand is empty</li>
				</ul>
			</div>
			<div>
				<h3>Different</h3>
				<ul>
					<li>Chaotic UNI-specific cards</li>
					<li>Build and share your own decks</li>
					<li>Play with your favourite custom rules</li>
				</ul>
			</div>
		</div>
		<p class="landing-quote">
			"It feels as if someone played modded Minecraft or Risk of Rain 2, and chugged down all of
			the mods existing."
		</p>
	</section>

	<section class="landing-section">
		<h2>The rules you can turn on</h2>
		<dl class="landing-rules">
			<div>
				<dt>Draw Stacking</dt>
				<dd>Play a +2 onto a +2. The next player eats the whole stack.</dd>
			</div>
			<div>
				<dt>Seven-Zero</dt>
				<dd>Play a 7 to swap hands with anyone. Play a 0 and everyone passes hands around.</dd>
			</div>
			<div>
				<dt>Jump In</dt>
				<dd>Hold the exact same card as the one just played? Play it out of turn.</dd>
			</div>
			<div>
				<dt>Force Play</dt>
				<dd>If you can play a card, you have to — no holding a playable card back.</dd>
			</div>
			<div>
				<dt>Progressive</dt>
				<dd>Draw cards stack across different draw types, not just matching ones.</dd>
			</div>
			<div>
				<dt>No Bluffing</dt>
				<dd>Wild Draw Four challenges are checked automatically, no arguing at the table.</dd>
			</div>
		</dl>
	</section>

	<section class="landing-section">
		<h2>Free, no ads in the game, open source</h2>
		<p>
			UNI! costs nothing to play and never puts ads in front of you mid-match. The code is public
			on <a href="https://github.com/Eldyn/uni">GitHub</a> — read it, run it yourself, or send a
			pull request.
		</p>
	</section>

	<section class="landing-section">
		<h2>Questions</h2>
		<dl class="landing-faq">
			<div>
				<dt>Do I need an account?</dt>
				<dd>No — Play as Guest gets you into a match immediately. An account just remembers your stats.</dd>
			</div>
			<div>
				<dt>How many people can play?</dt>
				<dd>Up to 4 players per match today, human or bot in any mix.</dd>
			</div>
			<div>
				<dt>Does it work on my phone?</dt>
				<dd>Yes — UNI! is built mobile-first and runs in any modern browser, no install required.</dd>
			</div>
			<div>
				<dt>Is this made by Mattel?</dt>
				<dd>No. UNI! is an independent, open-source project and isn't affiliated with or endorsed by the trademark holder.</dd>
			</div>
		</dl>
	</section>
</div>

<style>
	.landing-content {
		display: flex;
		flex-direction: column;
		gap: var(--space-16);
		max-width: 720px;
		margin: 0 auto;
		padding: var(--space-8) var(--space-4);
	}
	.landing-section h2 {
		font-family: var(--heading);
		font-size: var(--text-title);
		margin-bottom: var(--space-3);
	}
	.landing-legal {
		font-size: var(--text-small);
		color: var(--text);
		opacity: 0.7;
	}
	.landing-screenshot img {
		width: 100%;
		display: block;
	}
	.landing-compare {
		display: grid;
		grid-template-columns: 1fr;
		gap: var(--space-4);
	}
	.landing-quote {
		margin-top: var(--space-4);
		font-style: italic;
		color: var(--text);
		opacity: 0.8;
	}
	.landing-rules dt {
		font-family: var(--tiny);
		text-transform: uppercase;
		color: var(--accent);
		margin-top: var(--space-3);
	}
	.landing-faq dt {
		font-weight: bold;
		margin-top: var(--space-3);
	}

	@media (min-width: 640px) {
		.landing-compare {
			grid-template-columns: 1fr 1fr;
		}
	}
</style>
