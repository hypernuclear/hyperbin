// A stable, anonymous identifier for this machine.
//
// The same algorithm hypershot uses, deliberately: hardware id where the
// platform offers one, hostname plus primary MAC where it does not, and
// SHA-256 over the result. Verified byte-identical to that
// implementation, so a machine reports the same id to both products.
//
// That last property is a choice, and it cuts both ways. It is what lets
// you ask whether hyperbin's audience overlaps hypershot's — the whole
// point of a lead magnet — and it is also a correlation the user did not
// ask for. It only happens at all once they opt in, which is the reason
// the opt-in exists.
//
// The hash is one-way and the raw hardware id never leaves the machine,
// so what reaches the server distinguishes installs without identifying
// a person or a device.
//
// Lives under analytics/ because analytics is its only consumer. If a
// second one ever appears — licensing, say — move it to core/.
#pragma once

#include <QString>

namespace hyperbin {

/// The id, computed once and cached for the process lifetime.
QString machineId();

} // namespace hyperbin
