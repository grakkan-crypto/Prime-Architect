# Incremental generation: the Coding Turn

Loose spec. It describes the shape of a Coding Turn — the unit that carries one feature or idea from a rough description through repeated generate-and-audit cycles to a written or dismissed outcome. It does not describe how any part is built.

## Shape: modelled on RebuttalTurn, not on Turn

A Coding Turn holds one turn id open across as many generate/audit cycles as it takes, the same way RebuttalTurn holds one id open across as many exchanges as it takes. It is not modelled on ordinary Turn's CODING HOLD, which closes on every decision (Reject, Write, and Edit alike) — that shape only survives by a shared-context pool that happens to outlive the close, which is fragile. A Coding Turn simply doesn't close until the work is actually done or abandoned.

Only one Coding Turn is ever active at a time, system-wide. While active, it suppresses ordinary Turn entirely — Turn stands down completely, not partially, so there is nothing left watching that could misread a Coding Turn's internal activity as an ordinary turn's close signal.

That suppression is its own mechanism, piggy-backing off Turn, specific to the coding pipeline. It is deliberately NOT a switch registered on LiveRegistry: the existing Turn/RebuttalTurn switch stays exactly as it is, untouched, universal, with no exceptions — that's what earns it a place on a registry every pipeline shares. "Coding" is pipeline-specific and has no business being hardcoded onto something meant to stay domain-general.

Coding-active and rebuttal-active are two independent parameters, not two values of one switch:
- Neither active: ordinary Turn.
- Coding-active alone: Coding Turn, replacing ordinary Turn.
- Rebuttal-active alone: RebuttalTurn, as it already works.
- Both: a Coding-Rebuttal Turn — the Coding Turn's own id stays live throughout, and the Rebuttal fragment gets its own id on top, exactly as an ordinary Rebuttal carries its own id.

A Coding Turn closes on exactly two things: the user dismissing it (UI), or code being sent to Write. Nothing else closes it. Reject does not close it — it discards that cycle's output and the turn stays open for another attempt. A dismissed (not written) Coding Turn is not lost: it sits in RAG, reopenable later.

## Scope: one feature, not one file

A Coding Turn is scoped to one concept or feature, never to one file. It can touch as many existing files as the feature needs and create as many new files as it needs — additive to several existing files, wholly new files, or both at once. File-scoping was considered and rejected outright as too restrictive for real work.

## The list, and how a Coding Turn is born

Each project keeps a list of Coding Turns not yet committed — ideas started but not finished. This is the same kind of stored list RebuttalHost already keeps for its own open rows; here it's selected from rather than cycled through.

Selecting an item reopens it: the same turn id is reused, and the most recent code and key decision points come back from RAG. This needs nothing new built underneath it — every pool everywhere is already stamped with its turn id, and Author already logs all pool metadata, so turn-scoped retrieval already exists as a side effect of the id being there in the first place.

Two ways a Coding Turn starts:
1. **Explicit** — a "+" against the project's list of tasks/features. The user gives a brief description or just a prompt, and generation starts under a freshly minted turn id.
2. **Organic** — mid-conversation, an aside ("that's a good idea, how about X") has its snippet extracted the same way any text is extracted from a prompt or response for context injection, and lands as a new entry on the list, not yet active.

If a Coding Turn is already active when an organic idea comes up, the default is to stay on the current one — the new idea is only parked on the list, never auto-activated. Switching is a manual choice the user makes; automatic switching between ideas would make it impossible to tell what's actually being worked on right now.

## Routing inside an open Coding Turn: code, or not

Masking for the relevant agents (Auditor, Architect-Synth) lifts only while a Coding Turn is active. Lifting the mask is about visibility, not invocation — every agent is always woken regardless; masking only controls whether it can see the input pool at all, and without visibility there's nothing to generate against.

Once visible, each agent reads the actual content and recognises, ordinarily, whether this exchange is code to act on or not — the same kind of contextual recognition Adept-Infer already does for confirm/dismiss, not keyword matching. There are no magic words anywhere in this system.

Each agent's own Arbiter (Arbiter-Auditor, Arbiter-ArchitectSynth) enforces a deterministic rule against that same content: code present and nothing done → corrected into acting; no code present and something done → corrected into stopping. This is a rule check against one read of the content, not a second, independently-arrived-at opinion that could disagree with the first.

## The report

Analyst-Adept produces the human-facing prose diff report. It reads only `Auditor_Output` — never the code, never Adept's output, never shared context.

It's the right agent for this because it's genuinely idle at this moment: its one existing job is checking Adept's reasoning for logical soundness, and there's no reasoning to check while Adept-Coder is purely generating. Giving it this second, narrow, non-overlapping wire is a better use of an already-resident agent than minting a new one — agents here are resident from pipeline load onward, permanently, and the roster is already large enough that a new name is a real cost, not a free label. The terse (Analyst) register is deliberately preferred over the verbose (Adept) one here: metaphor and expansive language are exactly what's unwanted in a report on what code actually does.

This runs concurrently with generation because Auditor is a streaming consumer of the token stream, not a one-shot step that waits for generation to finish — Analyst-Adept just follows Auditor's own output as it updates.

Auditor's diff baseline is simply whatever is already in the pool it reads. For work additive to an existing file, that content is already present, because Analyst-Coder's ordinary job already pulls the existing file in — nothing extra needed for this. For a brand-new file, the baseline is empty, so the report is everything that's been added.

## "What if": Split, not branching

A "what if" is handled by Split: two temperature-variant instances of the identical fusion — same context, same prompt, same weights, only temperature differs, one cool, one hot — each reading one shared chain-of-thought as the other writes to it. The hot instance's audacity forces the cool instance's logic to find a way to make it work; the two converge, mathematically, on one idea that is both ambitious and genuinely feasible. That converged idea is what Analyst-Coder generates for real.

There is deliberately no separate exploratory-branching mechanism alongside this. A "what if" is tried for real, as an ordinary cycle, because reverting is cheap: Reject simply bins that cycle's output, nothing was migrated and nothing was written. Testing the result in the emulator and reading the report is how it's evaluated — not a comparison of multiple generated alternatives.

## Arming the human decision

The Reject/Write decision must never be reachable the instant a diff report appears. A Coding Turn needs two distinct states: **reviewing** (the diff is visible, nothing destructive is reachable) and **armed** (Reject/Write are reachable) — and only an explicit human gesture moves it from the first to the second. One stray press must never be able to commit anything to disk.

Only two terminal decisions exist: **Reject** and **Write**. There is no Edit. A rejected attempt is cheap enough — refetchable from RAG — that a dedicated "revise in place" decision adds nothing the ordinary next cycle doesn't already do.

## Cycle bookkeeping

Each cycle's `Analyst_Output` migrates to shared context so the next cycle can build on it; the previous shared-context version is destroyed at that point rather than kept as a shadow copy. It remains fetchable via RAG/Author's logging if it's ever needed again.

A cycle's output landing in a `User_Output`-classified pool and completing triggers the same ordinary bookkeeping/cleanup that already runs on such a completion elsewhere in the system — but this closes the *cycle*, not the Coding Turn. The Coding Turn itself only ever closes on dismiss or Write, as stated above.

## What's deliberately not covered here

Further questions will surface once this is actually built and used. They're addressed as they arrive, not speculated about in advance — spending more time now inventing issues that may never occur isn't worthwhile.
