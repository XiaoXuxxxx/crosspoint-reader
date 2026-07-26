#pragma once

#include <string>
#include <vector>

// Returns true if `text` contains any Thai codepoint (U+0E01–U+0E5B).
// Used to gate the Thai segmentation path before the CJK fallback.
bool containsThaiCodepoint(const std::string& text);

// Returns byte offsets within `text` where line breaks are allowed for Thai
// script. Uses forward maximal matching against a flash-resident dictionary
// to find word boundaries, and also adds breaks at Thai<->non-Thai transitions.
//
// Returns an empty vector if:
//   - `text` contains no Thai codepoints (caller should fall through to CJK),
//   - or the text is a single Thai word with no internal break opportunities
//     (caller should push it as one token, NOT fall through to CJK which would
//     split at every character).
//
// Use containsThaiCodepoint() first to distinguish these two cases.
std::vector<size_t> thaiWordBreakByteOffsets(const std::string& text);
