// context_matcher.h — ContextMatcher: context matching. It links prompt IDs
//                     to pool IDs for Shared Context revealing.
//
// ===========================================================================
// WHAT THIS FILE IS
//
//   One function, context matching, reached by two routes. The first is
//   ContextMatcher_Watch: it asks Watcher to watch for a pool arriving in
//   each input pool the matching serves. The second is
//   ContextMatcher_Receive: the recipient Watcher reaches when one arrives.
//
//   Arriving is a pool entering the class. It is created into it or it
//   migrates into it; the map shows both the same way, and one request per
//   class catches both. There is one request per input pool, so each
//   delivery says which input pool it concerns and nothing has to be
//   untangled.
//
//   The name and the message of each request are the input pool's registry
//   name, exactly as the pipeline declares it. The name is prefixed with
//   ContextMatcher so no other registrant can collide with it. The message
//   is the bare name, which is the token LiveRegistry answers to.
//
//   On a delivery, the input pool named by the message is turned back into
//   its class ID through LiveRegistry, and the pool map is read on the
//   screen for the newest pool of that class: the one with the greatest
//   pool ID, pool IDs being sequential. Its one prompt ID is held. The work
//   runs apart from the receipt; Watcher is never kept waiting.
//
//   The map is read only through the map key. The key is asked of Pool
//   Maintenance once, before this file's first visit to the screen in the
//   session, and held for the rest of the session. Between deliveries this
//   file holds the key and the held prompt IDs, nothing else.
//
// ===========================================================================
// OFFICIAL RULINGS — STRICT RULES. A change that would break one is wrong by
// definition. It is raised with the user, never made.
//
// 1. CONTEXTMATCHER DOES NOT POLL.
//    It never loops, never checks a pool on a timer, never looks to see
//    whether something has happened. Watcher watches; this file is told.
//
// 2. THE CLASS IS LOOKED UP, NEVER HELD.
//    Class IDs belong to LiveRegistry and change with the pipeline. Each is
//    asked for by name at the moment of registering and handed straight to
//    Watcher. No class ID is stored here, and none is written in.
//
// 3. EVERY REQUEST ENDS WITH THE PIPELINE.
//    A class ID is true only for the pipeline that assigned it, so every
//    request carries the pipeline scope and dies with it. Registering again
//    belongs to the next pipeline's load.
//
// 4. A REQUEST IS HANDED OVER COMPLETE.
//    Every part Watcher takes is stated here, including what matches its
//    default. Nothing is left for Watcher to assume.
//
// 5. A MISSING INPUT POOL IS A FACT FOR WELLNESS, NOT A STOP.
//    If the registry does not know a name, Wellness is told and no request
//    is made for that name. Every other name goes ahead. Nothing stands in
//    for the missing class, and nothing is retried.
//
// 6. A DELIVERY IS A PROMPT TO LOOK, NOT AN ANSWER.
//    Watcher says which input pool, never which pool arrived. The receipt
//    is mechanical and immediate. Whatever needs the pool itself reads the
//    map; nothing is asked of Watcher and nothing is sent back.
//
// 7. THE MAP KEY IS ASKED FOR ONCE A SESSION AND HELD.
//    It is taken from Pool Maintenance before the first visit to the screen
//    and before no other; every read of the map goes through it. It is never
//    asked for again in the session, never passed on, never edited here.
//    The map is never read through its layout.
//
// 8. A MAP READ IS ARRIVED, USED AND LEFT, EVERY TIME.
//    Each delivery arrives on the screen, copies out what it needs, and
//    leaves before anything else is done with it. A read is never kept,
//    never carried to the next delivery, never handed to anyone. What was
//    copied out is a working copy for that one delivery, gone with it.
//
// 9. THE NEWEST POOL IS THE ONE WITH THE GREATEST POOL ID.
//    Pool IDs of a kind only ever increase. An input pool carries exactly
//    one prompt ID.
//
// 10. CONTEXTMATCHER OWNS NO POOL.
//    It does not create, write, migrate, or destroy any pool. It reads and
//    it links. Pool Maintenance owns the pool; LiveRegistry holds the links.
//
// WELLNESS
//   Bare booleans, named for what they answer, set at the instant they are
//   answered, never read again here. Wellness sees them because they exist.
// ===========================================================================

#pragma once

#include <string>

namespace prime {

// Registers one Watcher request per input pool the registry knows.
void ContextMatcher_Watch();

// Watcher's recipient: the request's name and message. The mechanical
// receipt goes back at once.
bool ContextMatcher_Receive(const std::string& name, const std::string& message);

} // namespace prime
