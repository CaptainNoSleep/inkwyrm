#pragma once

namespace InkwyrmConfigImport {

// Imports /inkwyrm.conf from the SD card root, applies supported settings in-memory.
// Defers the credential scrub write until performDeferredScrub() is called after first render.
bool applyFromSdRoot();

// Performs the deferred credential scrub write after the first render (off boot-critical window).
// Should be called after activityManager.goToBoot() to avoid blocking boot task.
void performDeferredScrub();

}  // namespace InkwyrmConfigImport
