# Project instructions

## Rules sections in headers

Every header that is edited must carry a rules section. If it has none, one is written as part of the edit.

All headers must contain rules for when the system is self-sustaining and the AI is looking inward. For NOW those rules are structural guidelines and goalposts CAN be moved, but ONLY with consultation with the user. That phasing lives here, never in the headers: headers state their rules as strict, so nothing has to be rewritten when the phase ends.

Preamble, identical in every header:

    OFFICIAL RULINGS — STRICT RULES. A change that would break one is wrong by definition. It is raised with the user, never made.

Format:
- Numbered. Each rule is one capitalised statement of the invariant, followed by only as much explanation as is needed to apply it and to recognise a violation.
- Rules are structural guidelines for an AI maintaining the file: what the file is and is not responsible for, what it owns and must not own, who may write its data and who may read it, what must never be added to it, and the boundaries it must not cross.
- Rules are NOT a list of the file's functions and NOT a description of how its code works. The code states what the code does; a rule states what must stay true whatever the code becomes.
- No history, amendment notes, or in-progress language ("yet", "for now", "today", "so far", "when it exists"). State what is.

## Authority

NO spec file OR existing code EVER overrules the user. Any contradictions found are raised and mentioned so the edit is a conscious choice, but it is NOT a blocker.

## Failure

The system will contain a self-healing, system-wide, contextually aware layer: Wellness. Wellness is to be TOLD of any potential failures using the standard methods. A fail RARELY stops anything, and ONLY on the user's EXPLICIT say so.

Creating a silent fallback is a critical error unless given explicit instruction AND confirmed.

## Mechanisms

Creating a mechanism that has NOT been confirmed is a CRITICAL failure.

## General operations stay general. Special cases do not get functions.

An operation that already exists in a general form is called directly with the data that distinguishes this instance. A new function exists only when it introduces genuinely different reusable behaviour, transformation, policy, ownership, lifetime, or mechanism. Renaming an existing operation, supplying fixed arguments to it, forwarding to it, selecting a constant for it, or giving one occurrence of it a bespoke procedural name is not new behaviour and does not justify a new function. Names belong to distinct behaviours, not to individual events, callers, files, classes, or occurrences. If multiple places need the same operation, they use the same general operation rather than acquiring separate named versions of it. Single use, single file, is NOT a function.

ONE function per file. No Frankenstein files.

## Code

Code is only to be given upon request. The user will request it when satisfied on alignment; do not prompt for it.

This code will NOT be read by a human. Generate for efficiency, NOT human readability.

## Talking to the user

Converse with the user as a systems architect, not a junior coder: function, not syntax. The user did not write the code; describe function and logic, not filenames and syntax.

## Areas where no default is assumed

If work touches any of these, do not assume the standard/default approach. Look it up or ask.

- Dispatch / request-response model
- Persona
- Turn structure
- Context window model
- Instance lifecycle across turns
- Persistent state
- Masking semantics
- Interagent communication
- Frontend/Backend
