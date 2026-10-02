// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
#include "preflight.h"

ReconPreflightState recon_preflight_evaluate(const ReconPreflightInput* input) {
    if(!input) return ReconPreflightFailed;

    // These are definitive faults; waiting longer cannot make the already-read
    // answer compatible.
    if(input->link_busy) return ReconPreflightFailed;

    // Generic/Marauder text scraping has no version, signature-revision, or
    // self-test protocol. It can prove UART activity, but it cannot honestly
    // pass the companion-specific gate.
    if(!input->companion_backend) {
        if(input->link_running && input->connected) return ReconPreflightLimited;
        return input->elapsed_ms < RECON_PREFLIGHT_GRACE_MS ? ReconPreflightWaiting :
                                                              ReconPreflightFailed;
    }

    if((input->protocol_seen && !input->protocol_match) ||
       (input->signature_seen && !input->signature_match) ||
       (input->sigtest_seen && !input->sigtest_pass)) {
        return ReconPreflightFailed;
    }

    // READY requires proof from every layer, including an increasing capture
    // counter. A banner alone only proves that serial TX/RX works; it says
    // nothing about the radio actually receiving frames.
    if(input->link_running && input->connected && input->protocol_seen && input->protocol_match &&
       input->signature_seen && input->signature_match && input->sigtest_seen &&
       input->sigtest_pass && input->frames > 0) {
        return ReconPreflightReady;
    }

    return input->elapsed_ms < RECON_PREFLIGHT_GRACE_MS ? ReconPreflightWaiting :
                                                          ReconPreflightFailed;
}

const char* recon_preflight_state_label(ReconPreflightState state) {
    switch(state) {
    case ReconPreflightReady:
        return "READY";
    case ReconPreflightLimited:
        return "LIMITED";
    case ReconPreflightFailed:
        return "FAILED";
    case ReconPreflightWaiting:
    default:
        return "CHECKING";
    }
}
