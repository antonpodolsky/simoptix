// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Reads what is on the PC into the facts that setup_status.cpp decides from: each part's files against
// the ones inside setup, the layer's registrations, OBS and LMU. Reads only, and changes
// nothing.
#pragma once

#include <string>
#include <vector>

#include "setup_facts.h"

// `setupLayerSignature` is the signature of the layer inside setup, which never changes while setup runs,
// so it is checked once (CheckEmbeddedSignature) and passed in.
Facts ReadFacts(SignatureKind setupLayerSignature);

// The full paths of every registration of SimOptix's layer with the OpenXR loader, from any folder.
std::vector<std::wstring> FindLayerRegistrations();
