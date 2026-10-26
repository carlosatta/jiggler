#pragma once

#include "update_manager.h"

// Starts the background task that checks GitHub for new releases, downloads them over HTTPS (certificates are
// verified against the bundle in data/cert) and asks for a restart once the new image is written.
void startUpdateTask(jig::UpdateManager& manager, void (*restart)());
