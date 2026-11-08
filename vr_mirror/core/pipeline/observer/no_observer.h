// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// NoObserver: no debugging (the default): every record and stats line is dropped, and no stamp is
// numbered. Stateless. Knows nothing about traces or stamps.
#pragma once

#include "pipeline/observer/observer.h"

class NoObserver : public Observer {};
