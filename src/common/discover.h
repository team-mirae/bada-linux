// Where installed IMEs are (docs/spec.md §1): every <dir>/bada-linux/imes/<name>/ime.json under
// $XDG_DATA_HOME (else ~/.local/share), then each of $XDG_DATA_DIRS (else /usr/local/share:/usr/share),
// and first of all each folder in $BADA_LINUX_IMES (colon-separated; for tests). The first manifest
// with a given id wins, so a user's copy overrides the system's.
#pragma once

#include <string>
#include <vector>

#include "manifest.h"

namespace bl {

// Manifests that don't parse are skipped, their errors in `errors` ("path: message").
std::vector<Manifest> DiscoverManifests(std::vector<std::string>* errors);

}  // namespace bl
