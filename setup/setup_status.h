// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The decisions of SimOptix Setup, from the facts it found: which buttons each part's row offers
// (install, update or repair, and uninstall), whether it can be changed now and what to close first, and
// which folder layout an OBS version uses. Logic only: it reads no file, registry or process, so the same
// facts always give the same answer.
#pragma once

#include <string>

#include "setup_facts.h"
#include "setup_parts.h"

// What the row's main button does. Each one installs the part from setup. Update replaces a different
// build. Repair keeps the same build and puts right its place or its registration.
enum class PartAction { kNone, kInstall, kUpdate, kRepair };

struct PartStatus {
    PartAction action = PartAction::kNone;
    bool hasSomething = false;      // something of the part is on the PC, to uninstall
    std::wstring installBlockedBy;  // why it can't be installed now, empty when it can
    std::wstring removeBlockedBy;   // why it can't be uninstalled now, empty when it can
};

// The plugin layout of OBS with the major version `major`: OBS 33 and later look for the DLL in the
// plugin's folder.
PluginLayout LayoutOfObs(int major);

PartStatus DescribePart(PartId id, const PartFacts& part, const SystemFacts& system);

// The main button's label, empty for kNone.
const wchar_t* ActionLabel(PartAction action);

// The line under the title: "OBS Studio 32.2.2", or that it wasn't found.
std::wstring DescribeObs(const SystemFacts& system);

// The question asked before an unsigned or untrusted layer is installed, empty when the layer inside
// setup is signed and trusted.
std::wstring UnsignedLayerWarning(const SystemFacts& system);
