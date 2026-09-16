#include "core/Lifecycle.hpp"

namespace jot::core {

// The order of these tests IS the design. Each one is a claim about which
// consideration outranks which, and every pair of them is under the selftest.
OnClose on_close(const Lifecycle& s) {
    // A quit already asked the questions and got its answers; this close is the
    // quit doing its work. Re-deciding here would either re-prompt or -- far
    // worse, under residency -- hide the window a quit was trying to destroy,
    // and the Quit item would do nothing at all.
    if (s.quitting) return OnClose::Exit;

    // Residency outranks the scratch prompt, and this is the inversion the
    // milestone exists for: nothing is being lost, so there is nothing to ask.
    // The notes stay in memory with the process that holds them.
    if (s.background) return OnClose::StayResident;

    // The prompt has been answered already -- this is the second close_request,
    // the one the continuation raised.
    if (s.forced) return OnClose::Exit;

    if (s.scratch) return OnClose::AskFirst;
    return OnClose::Exit;
}

OnQuit on_quit(const Lifecycle& s) {
    // Note what is NOT consulted: `background`. A resident jot quits exactly
    // the way a non-resident one does -- the preference decides what the X
    // does, never what Quit does. An off switch a preference can disarm is not
    // an off switch.
    if (s.quitting) return OnQuit::Exit;   // already under way; do not ask twice
    if (s.forced)   return OnQuit::Exit;   // asked and answered
    if (s.scratch)  return OnQuit::AskFirst;
    return OnQuit::Exit;
}

}  // namespace jot::core
