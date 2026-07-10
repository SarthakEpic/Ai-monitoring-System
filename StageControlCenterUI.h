#pragma once

#include <windows.h>

#include "BackgroundAgent.h"
#include "BenchmarkProof.h"
#include "LowEndAutopilot.h"

void DrawLowEndAutopilotPanel(
    HDC hdc,
    const RECT& bounds,
    const LowEndAutopilotResult& result,
    HFONT sectionFont,
    HFONT valueFont,
    HFONT smallFont
);

void DrawBackgroundAgentPanel(
    HDC hdc,
    const RECT& bounds,
    const BackgroundAgentResult& result,
    HFONT sectionFont,
    HFONT smallFont
);

void DrawBenchmarkProofPanel(
    HDC hdc,
    const RECT& bounds,
    const BenchmarkProofResult& result,
    HFONT sectionFont,
    HFONT valueFont,
    HFONT smallFont
);
