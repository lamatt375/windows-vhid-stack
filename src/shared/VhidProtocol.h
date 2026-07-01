#pragma once

#define VHID_PROTOCOL_VERSION_MAJOR 0u
#define VHID_PROTOCOL_VERSION_MINOR 1u
#define VHID_PROJECT_NAME "windows-vhid-stack"

/*
 * Write-capable commands are intentionally not defined in the source skeleton.
 * Future commands must be fixed-size, versioned, allow-listed, reject-by-default,
 * and paired with readiness plus clear_all semantics before any VM write proof.
 */
