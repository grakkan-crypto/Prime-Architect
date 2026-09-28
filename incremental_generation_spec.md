# Incremental generation: rough idea, generate, audit, refine

Loose spec. It describes the shape of a coding turn as a repeated small loop instead of one large upfront design turn. It does not describe how any part is built.

## The problem this replaces

The alternative is spending several turns building a fully detailed spec for a file or edit before generating anything, then generating it in one pass and hoping the result matches every particular of that spec. Mismatches surface late, after the investment in detail is already spent, and an invented mechanism sitting inside a wall of prose is hard to catch before it gets generated into code.

## The loop

1. **Rough idea.** The user states, in a sentence or two, what a file or edit should do — enough to generate against, not enough to be a spec.
2. **Generate.** One pass of code is produced against that rough idea alone.
3. **Audit.** The Auditor's diff report is produced against what generated. What the audit compares against — the file as it stood before, the roster's directive, the header's own rulings — is the Auditor's business, not this loop's.
4. **Human decision.** The user reads the diff, asks whatever questions it raises, and decides: Reject, Write, or Edit — the same three outcomes Turn already recognises at the CODING HOLD.
   - **Reject** — thrown away, nothing kept.
   - **Write** — accepted as it stands; the file write proceeds.
   - **Edit** — the rough idea gets one small refinement and the loop repeats from step 2, in the SAME turn's shared context. Nothing about the file starts over from a blank prompt.
5. Repeat the Edit branch as many times as it takes. Each pass is a SMALL correction, not a rewrite: the loop is for narrowing in on the right file, not for re-specifying it from scratch on every pass.

## Why this catches invented mechanisms early

A fabricated mechanism written into three paragraphs of spec is hard to notice until it has already been generated wrong from it; the same fabrication sitting in one pass of actual code, next to a diff report, is a far smaller thing to spot and question. The rough idea is deliberately under-specified so that whatever the model invents to fill the gap shows up in the FIRST diff, not the tenth paragraph of a spec nobody has generated code against yet.

## Fit with the existing Turn mechanism

This is not a new mechanism. turn.h / turn.cpp already carry it end to end, under CODING HOLD:

- The hold triggers the instant Auditor_Output completes: Analyst_Output takes the reserve mark and the turn stops there for the human decision — it does not close.
- Reject and Write both end the turn the same way (the shared-context resolution, then Analyst_Output destroyed).
- Edit is the refinement step: the shared-context resolution runs, then Analyst_Output migrates to shared context and no successor turn opens — the next input continues in place.

That migration on Edit IS this loop's "small refinement, next turn." The rough-idea-first workflow described above is a way of DRIVING that mechanism, not a change to it: keep the first pass under-specified, and lean on repeated Edit passes instead of front-loading detail no one has generated code against yet.

## Outside the loop

Trying a Write result in the emulator is downstream of this loop entirely — an ordinary check the user runs against a Write turn's output the same way they would test any other piece of generated code. It is not a system mechanism and is not specced here.

## Open questions — not decided here

- What the Auditor's diff compares against when several Edit passes stack inside one held turn: the file as it stands on disk, or the previous Edit pass's Analyst_Output.
- Whether a later Edit pass carries forward the Auditor's and the user's prior questions/comments as context, or starts each pass seeing only the latest generated state.

Both are Turn-structure and Auditor-behaviour decisions; per the areas where no default is assumed, they are raised here rather than answered here.
