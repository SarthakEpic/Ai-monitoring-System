#include "StageControlCenterUI.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

using namespace std;

namespace {
constexpr COLORREF kPanel = RGB(18, 22, 31);
constexpr COLORREF kPanelRaised = RGB(24, 30, 41);
constexpr COLORREF kText = RGB(235, 240, 248);
constexpr COLORREF kMuted = RGB(160, 171, 188);
constexpr COLORREF kBlue = RGB(52, 152, 219);
constexpr COLORREF kGreen = RGB(46, 204, 113);
constexpr COLORREF kAmber = RGB(241, 196, 15);
constexpr COLORREF kRed = RGB(231, 76, 60);

string Shorten(const string& value, size_t maxLength) {
    if (value.size() <= maxLength) return value;
    if (maxLength <= 3) return value.substr(0, maxLength);
    return value.substr(0, maxLength - 3) + "...";
}

string DisplayToken(string value) {
    replace(value.begin(), value.end(), '_', ' ');
    return value;
}

string FormatNumber(double value, int precision = 0) {
    ostringstream out;
    out << fixed << setprecision(precision) << max(0.0, value);
    return out.str();
}

string FormatMemory(double megabytes) {
    if (megabytes >= 1024.0) return FormatNumber(megabytes / 1024.0, 1) + " GB";
    return FormatNumber(megabytes) + " MB";
}

void DrawTextLine(HDC hdc, int x, int y, const string& text, COLORREF color, HFONT font) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    HGDIOBJ oldFont = SelectObject(hdc, font);
    TextOutA(hdc, x, y, text.c_str(), static_cast<int>(text.size()));
    SelectObject(hdc, oldFont);
}

void DrawRoundedBox(HDC hdc, const RECT& bounds, COLORREF fill, COLORREF border, int radius = 16) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawMeter(HDC hdc, int x, int y, int width, double value, COLORREF accent) {
    const int safeWidth = max(20, width);
    const double clamped = max(0.0, min(100.0, value));
    RECT track{ x, y, x + safeWidth, y + 10 };
    HBRUSH trackBrush = CreateSolidBrush(RGB(42, 51, 66));
    FillRect(hdc, &track, trackBrush);
    DeleteObject(trackBrush);

    RECT fill{ x, y, x + static_cast<int>(safeWidth * (clamped / 100.0)), y + 10 };
    HBRUSH fillBrush = CreateSolidBrush(accent);
    FillRect(hdc, &fill, fillBrush);
    DeleteObject(fillBrush);
}

void DrawPanelTitle(HDC hdc, const RECT& bounds, const string& title, COLORREF accent, HFONT sectionFont) {
    DrawRoundedBox(hdc, bounds, kPanel, accent, 18);
    DrawTextLine(hdc, bounds.left + 16, bounds.top + 14, title, kText, sectionFont);
}

void DrawStatus(HDC hdc, const RECT& bounds, const string& text, COLORREF accent, HFONT sectionFont) {
    DrawRoundedBox(hdc, bounds, kPanelRaised, accent, 14);
    DrawTextLine(hdc, bounds.left + 14, bounds.top + 10, Shorten(DisplayToken(text), 34), kText, sectionFont);
}

COLORREF AutopilotAccent(const LowEndAutopilotResult& result) {
    if (!result.enabled) return kMuted;
    if (result.status == "READY_DRY_RUN") return kGreen;
    if (result.status == "REVIEW_REQUIRED") return kAmber;
    return kBlue;
}

COLORREF ProofAccent(const BenchmarkProofResult& result) {
    if (result.status == "PROOF_READY") return kGreen;
    if (result.status == "REVIEW_REQUIRED") return kAmber;
    return kBlue;
}
}

void DrawLowEndAutopilotPanel(
    HDC hdc,
    const RECT& bounds,
    const LowEndAutopilotResult& result,
    HFONT sectionFont,
    HFONT valueFont,
    HFONT smallFont
) {
    (void)valueFont;
    const int savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    const COLORREF accent = AutopilotAccent(result);
    const int height = bounds.bottom - bounds.top;
    DrawPanelTitle(hdc, bounds, "Low-End PC Autopilot", accent, sectionFont);

    RECT status{ bounds.left + 16, bounds.top + 48, bounds.right - 16, bounds.top + 90 };
    DrawStatus(hdc, status, result.status, accent, sectionFont);
    DrawTextLine(hdc, bounds.left + 16, bounds.top + 110,
                 result.lowEndDevice ? "MODE  LOW-END DEVICE" : "MODE  STANDARD DEVICE",
                 result.lowEndDevice ? kAmber : kMuted, smallFont);
    DrawTextLine(hdc, bounds.left + 16, bounds.top + 136,
                 "Pressure " + FormatNumber(result.pressureScore) + "%", kMuted, smallFont);
    DrawMeter(hdc, bounds.left + 16, bounds.top + 160, bounds.right - bounds.left - 32,
              result.pressureScore, accent);

    if (height >= 235) {
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 184,
                     "Actions " + to_string(result.actionsRecommended) + "  Reversible " +
                         to_string(result.reversibleActionCount), kText, smallFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 208,
                     "Recovered estimate " + FormatMemory(result.estimatedRecoveredRamMB), kGreen, smallFont);
    }

    if (height >= 320) {
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 242, "Foreground protection", kMuted, smallFont);
        DrawTextLine(hdc, bounds.left + 190, bounds.top + 242,
                     result.foregroundProtected ? "ACTIVE" : "OFF", result.foregroundProtected ? kGreen : kRed, smallFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 268,
                     "User apps touched  " + to_string(result.userAppsTouched),
                     result.userAppsTouched == 0 ? kGreen : kAmber, smallFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 294,
                     "Quick restore  " + string(result.quickRestoreAvailable ? "READY" : "NOT NEEDED"),
                     result.quickRestoreAvailable ? kBlue : kMuted, smallFont);
    }

    if (height >= 390) {
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 334, "Primary recommendation", kText, sectionFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 366,
                     Shorten(DisplayToken(result.primaryAction), 42), kText, smallFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 392,
                     "Target  " + Shorten(result.primaryTarget, 34), kMuted, smallFont);
    }

    if (height >= 485) {
        int y = bounds.top + 434;
        DrawTextLine(hdc, bounds.left + 16, y, "Safe candidate queue", kText, sectionFont);
        y += 34;
        if (result.recommendations.empty()) {
            DrawTextLine(hdc, bounds.left + 16, y, "No safe background candidate selected.", kMuted, smallFont);
        } else {
            const int maxRows = min(4, static_cast<int>(result.recommendations.size()));
            for (int i = 0; i < maxRows && y < bounds.bottom - 24; ++i, y += 28) {
                const auto& item = result.recommendations[static_cast<size_t>(i)];
                DrawTextLine(hdc, bounds.left + 16, y,
                             to_string(item.rank) + ". " + Shorten(item.targetName, 18) + "  " +
                                 Shorten(DisplayToken(item.actionName), 26),
                             i == 0 ? kGreen : kMuted, smallFont);
            }
        }
    }

    RestoreDC(hdc, savedDc);
}

void DrawBackgroundAgentPanel(
    HDC hdc,
    const RECT& bounds,
    const BackgroundAgentResult& result,
    HFONT sectionFont,
    HFONT smallFont
) {
    const int savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    const COLORREF accent = result.trayIconReady ? kGreen : kAmber;
    const int height = bounds.bottom - bounds.top;
    DrawPanelTitle(hdc, bounds, "Background Agent", accent, sectionFont);

    RECT status{ bounds.left + 16, bounds.top + 48, bounds.right - 16, bounds.top + 90 };
    DrawStatus(hdc, status, result.status, accent, sectionFont);
    DrawTextLine(hdc, bounds.left + 16, bounds.top + 112, "Control center", kMuted, smallFont);
    DrawTextLine(hdc, bounds.left + 150, bounds.top + 112, DisplayToken(result.controlCenter), kText, smallFont);
    DrawTextLine(hdc, bounds.left + 16, bounds.top + 140, "Tray icon", kMuted, smallFont);
    DrawTextLine(hdc, bounds.left + 150, bounds.top + 140, result.trayIconReady ? "READY" : "PENDING", accent, smallFont);
    DrawTextLine(hdc, bounds.left + 16, bounds.top + 168, "Silent monitor", kMuted, smallFont);
    DrawTextLine(hdc, bounds.left + 150, bounds.top + 168, result.silentMonitoring ? "ACTIVE" : "OFF", result.silentMonitoring ? kGreen : kMuted, smallFont);
    DrawTextLine(hdc, bounds.left + 16, bounds.top + 196, "Start on boot", kMuted, smallFont);
    DrawTextLine(hdc, bounds.left + 150, bounds.top + 196, result.startOnBootConfigured ? "CONFIGURED" : "OPTIONAL", kText, smallFont);

    if (height >= 280) {
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 230, "Quick restore", kText, sectionFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 262,
                     result.quickRestoreAvailable ? "AVAILABLE FROM TRAY" : "NO EXECUTED ACTIONS TO RESTORE",
                     result.quickRestoreAvailable ? kBlue : kMuted, smallFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 288,
                     "State  " + Shorten(DisplayToken(result.quickRestoreStatus), 30), kMuted, smallFont);
    }

    if (height >= 365) {
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 330, "Agent contract", kText, sectionFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 362, "Runs monitoring when the dashboard is hidden.", kMuted, smallFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 388, "Double-click tray icon to reopen control center.", kMuted, smallFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 414, "Current optimization remains recommendation-only.", kAmber, smallFont);
    }

    RestoreDC(hdc, savedDc);
}

void DrawBenchmarkProofPanel(
    HDC hdc,
    const RECT& bounds,
    const BenchmarkProofResult& result,
    HFONT sectionFont,
    HFONT valueFont,
    HFONT smallFont
) {
    const int savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    const COLORREF accent = ProofAccent(result);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    DrawPanelTitle(hdc, bounds, "Impact Simulation", accent, sectionFont);

    RECT status{ bounds.left + 16, bounds.top + 48, bounds.right - 16, bounds.top + 90 };
    DrawStatus(hdc, status, result.status + "  |  ESTIMATED", accent, sectionFont);

    const int columnGap = 12;
    const int columnWidth = max(120, (width - 44 - columnGap) / 2);
    RECT before{ bounds.left + 16, bounds.top + 108, bounds.left + 16 + columnWidth, bounds.top + 258 };
    RECT after{ before.right + columnGap, before.top, bounds.right - 16, before.bottom };
    DrawRoundedBox(hdc, before, kPanelRaised, RGB(67, 78, 94), 14);
    DrawRoundedBox(hdc, after, kPanelRaised, accent, 14);

    DrawTextLine(hdc, before.left + 14, before.top + 12, "BEFORE", kMuted, sectionFont);
    DrawTextLine(hdc, before.left + 14, before.top + 48, "CPU  " + FormatNumber(result.beforeCpu) + "%", kText, smallFont);
    DrawTextLine(hdc, before.left + 14, before.top + 76, "RAM  " + FormatNumber(result.beforeMemory) + "%", kText, smallFont);
    DrawTextLine(hdc, before.left + 14, before.top + 104, "RISK " + FormatNumber(result.beforeRisk) + "%", kText, smallFont);

    DrawTextLine(hdc, after.left + 14, after.top + 12, "AFTER ESTIMATE", accent, sectionFont);
    DrawTextLine(hdc, after.left + 14, after.top + 48, "CPU  " + FormatNumber(result.afterCpuEstimate) + "%", kText, smallFont);
    DrawTextLine(hdc, after.left + 14, after.top + 76, "RAM  " + FormatNumber(result.afterMemoryEstimate) + "%", kText, smallFont);
    DrawTextLine(hdc, after.left + 14, after.top + 104, "RISK " + FormatNumber(result.afterRiskEstimate) + "%", kText, smallFont);

    if (height >= 340) {
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 280, "Estimated recovery", kMuted, smallFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 306, FormatMemory(result.recoveredRamMB), kGreen, valueFont);
    }

    if (height >= 385) {
        DrawTextLine(hdc, bounds.left + 230, bounds.top + 282,
                     "CPU drop  " + FormatNumber(result.cpuDropPercent) + "%", kText, smallFont);
        DrawTextLine(hdc, bounds.left + 230, bounds.top + 310,
                     "Risk drop " + FormatNumber(result.riskDropPercent) + "%", kText, smallFont);
        DrawTextLine(hdc, bounds.left + 230, bounds.top + 338,
                     "Confidence " + FormatNumber(result.confidence) + "%", kText, smallFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 360,
                     "Actions " + to_string(result.actionsRecommended) + "   User apps touched " +
                         to_string(result.userAppsTouched),
                     result.userAppsTouched == 0 ? kGreen : kAmber, smallFont);
    }

    if (height >= 455) {
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 404, "Foreground protected", kMuted, smallFont);
        DrawTextLine(hdc, bounds.left + 180, bounds.top + 404, Shorten(result.foregroundProcess, 28), kText, smallFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 434, Shorten(result.summary, 76), kMuted, smallFont);
        DrawTextLine(hdc, bounds.left + 16, bounds.top + 462,
                     "Measured outcomes appear only after a real action and observation window.", kAmber, smallFont);
    }

    RestoreDC(hdc, savedDc);
}
