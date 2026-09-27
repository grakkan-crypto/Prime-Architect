# Project instructions

## Two audiences — keep them separate

- THIS FILE is instructions for Claude, working on the codebase with the user now.
- THE RULES SECTIONS IN HEADERS are for the system's own internal AI, which maintains the system when it is self-sustaining and looking inward. They are written for that AI, not for Claude, and not for a human.

Nothing meant for Claude goes into a header's rules. Nothing meant for the internal AI depends on this file.

## System model

This is an AI server, all-in-one system, not an application running on an OS. Subsystems such as Pool Maintenance (pools), FileLoader (disk), the memory distributor (RAM and VRAM) and Wellness (system health) are core layers, each directly owning and controlling its domain. They do not ask a separate OS for permission within their own domain. Do not describe or design them as applications requesting from an OS.

## Rules sections in headers

Every header that is edited must carry a rules section. If it has none, one is written as part of the edit.

All headers must contain rules for when the system is self-sustaining and the internal AI is looking inward. For NOW, while Claude is working with the user, those rules are structural guidelines to Claude and goalposts CAN be moved, but ONLY with consultation with the user. That phasing is an instruction to Claude and lives here, never in the headers: headers state their rules to the internal AI as strict, so nothing has to be rewritten when the phase ends.

Preamble, identical in every header:

    OFFICIAL RULINGS — STRICT RULES. A change that would break one is wrong by definition. It is raised with the user, never made.

Format:
- Numbered. Each rule is one capitalised statement of the invariant, followed by only as much explanation as is needed to apply it and to recognise a violation.
- Rules are structural guidelines for an AI maintaining the file: what the file is and is not responsible for, what it owns and must not own, who may write its data and who may read it, what must never be added to it, and the boundaries it must not cross.
- Rules are NOT a list of the file's functions and NOT a description of how its code works. The code states what the code does; a rule states what must stay true whatever the code becomes.

## No in-progress language in headers

Headers carry no history, no amendment notes, and no in-progress language ("yet", "for now", "today", "so far", "when it exists", "not built"). The internal AI has no "in progress"; a header states what is.

The one exception: a build outline for something not yet built (for example, what the OS must provide), placed as a comment where it is needed, and marked plainly at its start as to be removed once that thing is built. Nothing else in the header refers to it.

## Authority

NO spec file OR existing code EVER overrules the user. Any contradictions found are raised and mentioned so the edit is a conscious choice, but it is NOT a blocker.

The system is NOT complete. Some files will NOT align. This is not a blocker. This does NOT mean the user is wrong. Raise conflicts for awareness ONLY.

## The system does not build — and that is expected

For the entire time this project is worked on with Claude, the system is unbuildable. There is no boot mechanism, many files are broken or mid-conversion, and whole layers are not yet built. If it could build, it would be building itself.

- "This won't build", "this won't link", "this breaks X", "nothing provides Y yet" are NOT findings. Do not raise them, and never weigh a design against whether the current code builds.
- Judge every change on one thing: is it right for the designed system, as the user describes it?
- Raise only a genuine design conflict: two parts of the design, as described, that contradict each other. Say it once, briefly, for awareness. Never repeat it and never treat it as a blocker.

## No shadow copies

No shadow copy of anything, anywhere: nothing is ever kept in step with something else by a call or an extra step. Rejected outright.

A working copy is not a shadow copy: a copy taken for one strict function, never kept in step with its source, and destroyed a moment later (for example, a copy taken to compare against the next read, discarded when that read replaces it).

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
