# ContextMatcher: where it stands

Loose spec. It records the point reached with ContextMatcher, the rough shape of what it will look at, and the attention-weight mechanics it depends on. It is parked while the agent context builder is worked out.

## Purpose

ContextMatcher links prompt IDs to pool IDs for Shared Context revealing. For each prompt, it finds which pools the prompt-generating agent actually focused on while generating that prompt. Those pools are the context selected for that prompt ID. It runs fresh for every prompt: a new prompt means new context.

## Built so far

- **At pipeline load.** ContextMatcher registers two Watcher requests, one for each input pool: `ANALYST_INPUT` and `ADEPT_INPUT`.
  - Each request is endless, scoped to the pipeline, and runs in the foreground.
  - Each fires when a pool is created into its class or migrates into it.
  - The class ID is looked up by name in LiveRegistry and never stored.
  - If a name is missing, Wellness is flagged and the other request still goes ahead.
- **Also at pipeline load.** It finds the prompt agents. A prompt agent is any agent with write access on an input pool, identified by agent name (never by model, because agents share models). There is no limit on how many. They are held for the pipeline's lifespan.
- **On a delivery.** It asks Pool Maintenance for the map key once per session and holds it. It reads the pool map on the screen, finds the newest pool of that class (the greatest pool ID), and holds that pool's one prompt ID.
- **Open question, to be raised once the shape is fixed.** Using write access to identify the prompt agent does not cover:
  - an agent that writes those pools for a different kind of prompt, and so has its own prompt ID;
  - a sub-prompt that writes those pools without needing a prompt ID.

## Rough shape of what it will look at

1. The prompt agent generates the prompt.
2. **Re-read pass.** Once the prompt is finished, the same model runs over the same context plus the finished prompt in one batch, with attention capture on.
3. **Per-pool scoring.** Every candidate pool is scored from that capture. The candidates are the pools up for selection. Rules/Directives, persona and User_Input are always excluded.
4. **Relative selection.** Pools are chosen by relative standout, never by a fixed count or a fixed cut-off.
5. **Release.** Everything from steps 2 to 4 is released. The next prompt starts from nothing.

## Attention-weight mechanics

**The model's input.** A model takes one flat list of token IDs. It has no notion of pools.

**Context assembly.** Each visible pool is tokenized and appended to the list. At each append, the pool's span is known for free: its start is the list length before the append and its end is the length after. Recording {pool ID, start, end} at that moment is the only clean way to know which positions belong to which pool. Once the list is built, the boundaries are gone. Nothing in the system does this today; it is to be built. See "Agent context builder" below.

**Prefill.** For every position, at every layer and every head, the model computes three vectors:
- **Query (Q):** what the position looks for.
- **Key (K):** what the position offers.
- **Value (V):** what the position hands over.

The keys and values are stored. That store is the KV cache.

**Generating one token.**
1. The new token's query is scored against every earlier key: Q · K_j.
2. A softmax turns those scores into weights that sum to 1. These are the attention weights, for one generated token, in one head, in one layer.
3. The head's output is the weighted sum of the values.

**Pool attention.** A pool's attention is the weights at that pool's positions. The weights are per token, at both ends: per context token looked at and per generated token doing the looking. A per-pool figure is always a roll-up.

**Priority bias.** Priority per class works by adding a class-specific amount to the score of every position in that class, before the softmax. The labels needed to apply the bias are the same labels needed to extract per pool or per class. Label every position with its pool ID; the class follows from the pool.
- A single bias applied evenly across a class keeps the ratios between pools inside that class exactly.
- Excluded pools take a different share of attention on every generated token. So each candidate's share is recalculated per generated token, over the candidates only.

**The re-read is exact.** A generated token's attention depends only on the context and the tokens before it. Re-reading the finished prompt over the same context therefore reproduces exactly the weights from generation. Sampling and temperature do not matter: the tokens are already chosen. Three conditions must hold:
- the same model;
- the same context in the same positions;
- the same bias.

This means generation runs at full speed with no capture. The re-read costs roughly the prompt's length, in one batch. The full prompt is needed for the selection, but capture does not stop other processing. The only wait is that context cannot be chosen until the prompt that chooses it exists.

**What the kernel reduces to.** With pool labels, the attention kernel reduces the weights to per-pool figures as it runs, over the chosen layers and candidate pools only. The raw weights (generated tokens × layers × heads × context tokens) never leave the kernel.

## Scoring: relative, never fixed

- **Mean lift per token.** A pool's attention compared with background. This is baseline relevance and is not biased by pool size.
- **Standout.** The strongest sentence-length window, and the generated tokens at which the pool stood clearly above background. This catches the one vital sentence in a large pool.
- **Natural break.** Candidates sorted by lift split into "drawn on" and "background". The split decides how many are selected, whether that is dozens of small snippets or 21 of 25 pools.
- **No split means all.** The system is stateless and the prompt's own inference sits in Shared Context, so no split means every candidate is relevant.
- **Noise to calibrate out:**
  - sink tokens: the first token, separators, pool boundary and structural tokens;
  - extra attention on tokens near the end;
  - early layers, which track surface form.

  Calibration uses contexts where it is known which pools matter.

## Parked on

The agent context builder: what builds each agent's context and KV cache, records pool spans, and applies class priority.

## Recorded for the dataset

Everything else about a run is already logged. These three are added, for every fragment.

- **The KV map.** Every Class 1 section the prompt agent could see while writing the fragment:
  - the section's pool ID;
  - its length in tokens;
  - its position;
  - for a section that joined during generation, the step at which it joined.
- **Per-token, per-pool figures.** For each generated token: the Class 1 attention from that token to each pool, after the layer and head reduction.
  - They are kept per token, not only as totals, so any future scoring rule can be run over past data.
  - A 500-token fragment against 30 pools is about 60 KB.
- **The per-step total.** For each generated token, its total attention to Class 1. Class 1 is all ContextMatcher sees, so this is the baseline lift is measured against, and lift can be recalculated exactly from the record.
