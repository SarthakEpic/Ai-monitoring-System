#include "ModernDashboardUI.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace std;

namespace {

constexpr COLORREF kCanvas = RGB(8, 15, 23);
constexpr COLORREF kSidebar = RGB(9, 20, 30);
constexpr COLORREF kTopbar = RGB(10, 22, 32);
constexpr COLORREF kSurface = RGB(13, 27, 39);
constexpr COLORREF kSurfaceRaised = RGB(17, 34, 47);
constexpr COLORREF kSurfaceSoft = RGB(12, 25, 36);
constexpr COLORREF kBorder = RGB(38, 59, 73);
constexpr COLORREF kBorderStrong = RGB(53, 78, 94);
constexpr COLORREF kText = RGB(229, 237, 242);
constexpr COLORREF kMuted = RGB(143, 162, 174);
constexpr COLORREF kFaint = RGB(93, 116, 130);
constexpr COLORREF kCyan = RGB(22, 188, 220);
constexpr COLORREF kTeal = RGB(43, 205, 167);
constexpr COLORREF kAmber = RGB(239, 179, 62);
constexpr COLORREF kRed = RGB(237, 91, 91);
constexpr COLORREF kBlue = RGB(77, 158, 226);
constexpr COLORREF kGreenSoft = RGB(18, 63, 58);
constexpr COLORREF kAmberSoft = RGB(61, 47, 22);
constexpr COLORREF kRedSoft = RGB(61, 31, 36);

struct Layout {
    int sidebarWidth = 182;
    int topbarHeight = 68;
    int footerHeight = 70;
    int gap = 10;
    int pad = 12;
    RECT content{};
};

double Clamp(double value, double low = 0.0, double high = 100.0) {
    return max(low, min(high, value));
}

string Upper(string value) {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(toupper(ch));
    });
    return value;
}

string DisplayToken(string value) {
    replace(value.begin(), value.end(), '_', ' ');
    return value;
}

string Number(double value, int precision = 0) {
    ostringstream out;
    out << fixed << setprecision(precision) << value;
    return out.str();
}

string Memory(double megabytes) {
    if (megabytes >= 1024.0) return Number(megabytes / 1024.0, 1) + " GB";
    return Number(max(0.0, megabytes), 0) + " MB";
}

string Rate(double kbps) {
    if (kbps >= 1024.0) return Number(kbps / 1024.0, 1) + " MB/s";
    return Number(kbps, 1) + " KB/s";
}

string Percent(double value) {
    return Number(Clamp(value), 0) + "%";
}

void Fill(HDC hdc, const RECT& bounds, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &bounds, brush);
    DeleteObject(brush);
}

void Panel(HDC hdc, const RECT& bounds, COLORREF fill, COLORREF border = kBorder, int radius = 8) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    const HGDIOBJ oldBrush = SelectObject(hdc, brush);
    const HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom, radius, radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void Rule(HDC hdc, int x1, int y1, int x2, int y2, COLORREF color = kBorder) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    const HGDIOBJ old = SelectObject(hdc, pen);
    MoveToEx(hdc, x1, y1, nullptr);
    LineTo(hdc, x2, y2);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

void Text(
    HDC hdc,
    RECT bounds,
    const string& value,
    COLORREF color,
    HFONT font,
    UINT format = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS
) {
    const HGDIOBJ old = SelectObject(hdc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextA(hdc, value.c_str(), static_cast<int>(value.size()), &bounds, format | DT_NOPREFIX);
    SelectObject(hdc, old);
}

void Label(HDC hdc, int x, int y, int width, const string& value, const DashboardUiFonts& fonts, COLORREF color = kMuted) {
    Text(hdc, RECT{x, y, x + width, y + 20}, value, color, fonts.body);
}

void Value(HDC hdc, int x, int y, int width, const string& value, const DashboardUiFonts& fonts, COLORREF color = kText) {
    Text(hdc, RECT{x, y, x + width, y + 42}, value, color, fonts.value);
}

COLORREF RiskAccent(const string& level) {
    const string normalized = Upper(level);
    if (normalized == "CRITICAL") return kRed;
    if (normalized == "WARNING") return kAmber;
    return kTeal;
}

COLORREF RiskFill(const string& level) {
    const string normalized = Upper(level);
    if (normalized == "CRITICAL") return kRedSoft;
    if (normalized == "WARNING") return kAmberSoft;
    return kGreenSoft;
}

Layout BuildLayout(const RECT& client) {
    Layout layout;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    layout.sidebarWidth = width < 1320 ? 168 : 184;
    layout.topbarHeight = height < 820 ? 62 : 68;
    layout.footerHeight = height < 820 ? 60 : 70;
    layout.pad = width < 1320 ? 9 : 12;
    layout.gap = width < 1320 ? 8 : 10;
    layout.content = RECT{
        layout.sidebarWidth + layout.pad,
        layout.topbarHeight + layout.pad,
        client.right - layout.pad,
        client.bottom - layout.footerHeight - layout.pad
    };
    return layout;
}

RECT NavRect(const Layout& layout, int index) {
    const int top = layout.topbarHeight + 48 + index * 48;
    return RECT{8, top, layout.sidebarWidth - 8, top + 40};
}

void DrawNavIcon(HDC hdc, const RECT& bounds, int index, COLORREF color) {
    const int cx = bounds.left + 20;
    const int cy = (bounds.top + bounds.bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HBRUSH hollow = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
    const HGDIOBJ oldPen = SelectObject(hdc, pen);
    const HGDIOBJ oldBrush = SelectObject(hdc, hollow);
    if (index == 0) {
        Rectangle(hdc, cx - 7, cy - 6, cx + 7, cy + 6);
        Rule(hdc, cx - 3, cy + 9, cx + 3, cy + 9, color);
    } else if (index == 1) {
        for (int row = -1; row <= 1; ++row) Rule(hdc, cx - 7, cy + row * 6, cx + 7, cy + row * 6, color);
    } else if (index == 2) {
        Ellipse(hdc, cx - 7, cy - 7, cx + 7, cy + 7);
        Rule(hdc, cx, cy - 11, cx, cy - 7, color);
        Rule(hdc, cx, cy + 7, cx, cy + 11, color);
    } else if (index == 3) {
        MoveToEx(hdc, cx, cy - 9, nullptr);
        LineTo(hdc, cx + 8, cy - 5);
        LineTo(hdc, cx + 6, cy + 6);
        LineTo(hdc, cx, cy + 10);
        LineTo(hdc, cx - 6, cy + 6);
        LineTo(hdc, cx - 8, cy - 5);
        LineTo(hdc, cx, cy - 9);
    } else if (index == 4) {
        MoveToEx(hdc, cx - 5, cy - 9, nullptr);
        LineTo(hdc, cx - 2, cy + 2);
        LineTo(hdc, cx - 7, cy + 9);
        LineTo(hdc, cx + 7, cy + 9);
        LineTo(hdc, cx + 2, cy + 2);
        LineTo(hdc, cx + 5, cy - 9);
    } else {
        Ellipse(hdc, cx - 7, cy - 7, cx + 7, cy + 7);
        Ellipse(hdc, cx - 2, cy - 2, cx + 2, cy + 2);
    }
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

const char* ViewName(DashboardView view) {
    switch (view) {
    case DashboardView::Overview: return "Overview";
    case DashboardView::Processes: return "Processes";
    case DashboardView::Intelligence: return "Intelligence";
    case DashboardView::Safety: return "Safety";
    case DashboardView::Experiments: return "Experiments";
    case DashboardView::Settings: return "Settings";
    }
    return "Overview";
}

void DrawChrome(HDC hdc, const RECT& client, const Layout& layout, const DashboardUiState& state, DashboardView view, const DashboardUiFonts& fonts) {
    Fill(hdc, client, kCanvas);
    Fill(hdc, RECT{0, 0, layout.sidebarWidth, client.bottom}, kSidebar);
    Fill(hdc, RECT{layout.sidebarWidth, 0, client.right, layout.topbarHeight}, kTopbar);
    Rule(hdc, layout.sidebarWidth, 0, layout.sidebarWidth, client.bottom, kBorder);
    Rule(hdc, layout.sidebarWidth, layout.topbarHeight, client.right, layout.topbarHeight, kBorder);

    Text(hdc, RECT{16, 10, layout.sidebarWidth - 8, 38}, "Predictive", kText, fonts.section);
    Text(hdc, RECT{16, 34, layout.sidebarWidth - 8, 56}, "AUTOHEAL", kCyan, fonts.body);

    Text(hdc, RECT{layout.sidebarWidth + 18, 8, layout.sidebarWidth + 400, 39}, ViewName(view), kText, fonts.title);
    Text(hdc, RECT{layout.sidebarWidth + 18, 37, layout.sidebarWidth + 520, 59},
         "Intent-aware performance control center", kMuted, fonts.body);

    const bool actionsLocked = state.actionGlobalDisable || !state.actionExecutionEnabled || !state.onlinePolicyPromoted;
    RECT live{client.right - 410, 13, client.right - 286, layout.topbarHeight - 13};
    Panel(hdc, live, kSurfaceSoft, state.runtime.status == "HEALTHY" ? kTeal : kBorderStrong, 7);
    Fill(hdc, RECT{live.left + 12, live.top + 15, live.left + 19, live.top + 22}, kTeal);
    Text(hdc, RECT{live.left + 27, live.top + 5, live.right - 8, live.bottom - 4}, "LIVE", kText, fonts.section);

    RECT lock{client.right - 274, 10, client.right - 16, layout.topbarHeight - 10};
    Panel(hdc, lock, actionsLocked ? kAmberSoft : kGreenSoft, actionsLocked ? kAmber : kTeal, 7);
    Text(hdc, RECT{lock.left + 14, lock.top + 4, lock.right - 10, lock.bottom - 17},
         actionsLocked ? "ACTIONS LOCKED" : "ACTIONS ARMED", actionsLocked ? kAmber : kTeal, fonts.section);
    Text(hdc, RECT{lock.left + 14, lock.top + 27, lock.right - 10, lock.bottom - 2},
         actionsLocked ? "Shadow learning only" : "Safety gates active", kMuted, fonts.body);

    Text(hdc, RECT{16, layout.topbarHeight + 13, layout.sidebarWidth - 10, layout.topbarHeight + 42},
         "CONTROL CENTER", kFaint, fonts.body);
    const DashboardView views[] = {
        DashboardView::Overview,
        DashboardView::Processes,
        DashboardView::Intelligence,
        DashboardView::Safety,
        DashboardView::Experiments,
        DashboardView::Settings,
    };
    for (int index = 0; index < 6; ++index) {
        const RECT nav = NavRect(layout, index);
        const bool active = view == views[index];
        if (active) {
            Panel(hdc, nav, RGB(12, 48, 64), RGB(28, 102, 126), 6);
            Fill(hdc, RECT{nav.left, nav.top + 5, nav.left + 3, nav.bottom - 5}, kCyan);
        }
        DrawNavIcon(hdc, nav, index, active ? kCyan : kMuted);
        Text(hdc, RECT{nav.left + 42, nav.top, nav.right - 8, nav.bottom},
             ViewName(views[index]), active ? kText : kMuted, fonts.body);
    }

    const int statusTop = min(static_cast<int>(client.bottom) - layout.footerHeight - 152, layout.topbarHeight + 360);
    Text(hdc, RECT{16, statusTop, layout.sidebarWidth - 10, statusTop + 24}, "THIS DEVICE", kFaint, fonts.body);
    Text(hdc, RECT{16, statusTop + 28, layout.sidebarWidth - 10, statusTop + 50},
         state.performanceMode, kText, fonts.section);
    Text(hdc, RECT{16, statusTop + 55, layout.sidebarWidth - 10, statusTop + 77},
         "Foreground: " + state.system.intent.foregroundProcess, kMuted, fonts.body);
    Text(hdc, RECT{16, statusTop + 80, layout.sidebarWidth - 10, statusTop + 102},
         "Workload: " + string(ToString(state.qoe.workload)), kMuted, fonts.body);
    Text(hdc, RECT{16, statusTop + 105, layout.sidebarWidth - 10, statusTop + 127},
         "Processes: " + to_string(state.system.processCount), kMuted, fonts.body);
}

void DrawSparkline(HDC hdc, const RECT& bounds, const deque<double>& history, COLORREF color, double maximum = 100.0) {
    if (history.size() < 2) return;
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    const HGDIOBJ old = SelectObject(hdc, pen);
    const int width = max(1, static_cast<int>(bounds.right - bounds.left));
    const int height = max(1, static_cast<int>(bounds.bottom - bounds.top));
    for (size_t index = 0; index < history.size(); ++index) {
        const int x = bounds.left + static_cast<int>(index * width / max<size_t>(1, history.size() - 1));
        const double normalized = Clamp(history[index], 0.0, maximum) / maximum;
        const int y = bounds.bottom - static_cast<int>(normalized * height);
        if (index == 0) MoveToEx(hdc, x, y, nullptr);
        else LineTo(hdc, x, y);
    }
    SelectObject(hdc, old);
    DeleteObject(pen);
}

void DrawMetric(HDC hdc, const RECT& bounds, const string& label, const string& value, const string& note,
                const deque<double>& history, COLORREF accent, const DashboardUiFonts& fonts, double maximum = 100.0) {
    Panel(hdc, bounds, kSurface, kBorder, 7);
    Fill(hdc, RECT{bounds.left, bounds.top, bounds.left + 3, bounds.bottom}, accent);
    Text(hdc, RECT{bounds.left + 14, bounds.top + 8, bounds.right - 8, bounds.top + 31}, label, kMuted, fonts.body);
    Text(hdc, RECT{bounds.left + 14, bounds.top + 28, bounds.right - 8, bounds.top + 70}, value, kText, fonts.value);
    Text(hdc, RECT{bounds.left + 14, bounds.bottom - 28, bounds.right - 8, bounds.bottom - 7}, note, kFaint, fonts.body);
    DrawSparkline(hdc, RECT{bounds.left + 14, bounds.bottom - 50, bounds.right - 12, bounds.bottom - 31}, history, accent, maximum);
}

void DrawTitle(HDC hdc, const RECT& panel, const string& title, const string& subtitle, const DashboardUiFonts& fonts) {
    Text(hdc, RECT{panel.left + 14, panel.top + 8, panel.right - 12, panel.top + 34}, title, kText, fonts.section);
    if (!subtitle.empty()) {
        Text(hdc, RECT{panel.left + 14, panel.top + 31, panel.right - 12, panel.top + 54}, subtitle, kMuted, fonts.body);
    }
}

void DrawChart(HDC hdc, const RECT& bounds, const DashboardUiState& state, const DashboardUiFonts& fonts) {
    Panel(hdc, bounds, kSurface, kBorder, 7);
    DrawTitle(hdc, bounds, "System activity", "Last 30 samples  |  measured", fonts);
    Text(hdc, RECT{bounds.right - 256, bounds.top + 8, bounds.right - 14, bounds.top + 30},
         "CPU   MEMORY   DISK FREE", kMuted, fonts.body, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    RECT chart{bounds.left + 42, bounds.top + 60, bounds.right - 14, bounds.bottom - 20};
    for (int index = 0; index <= 4; ++index) {
        const int y = chart.top + index * (chart.bottom - chart.top) / 4;
        Rule(hdc, chart.left, y, chart.right, y, RGB(28, 48, 61));
        Text(hdc, RECT{bounds.left + 6, y - 10, chart.left - 5, y + 10},
             to_string(100 - index * 25), kFaint, fonts.body, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
    DrawSparkline(hdc, chart, state.cpuHistory, kCyan);
    DrawSparkline(hdc, chart, state.memoryHistory, kTeal);
    DrawSparkline(hdc, chart, state.diskHistory, kAmber);
}

vector<ProcessSnapshot> RankedProcesses(const DashboardUiState& state) {
    vector<ProcessSnapshot> rows = state.processes;
    sort(rows.begin(), rows.end(), [](const ProcessSnapshot& left, const ProcessSnapshot& right) {
        const double leftRank = left.expectedGainMB + left.wasteScore * 2.0 + left.cpuPercent * 4.0;
        const double rightRank = right.expectedGainMB + right.wasteScore * 2.0 + right.cpuPercent * 4.0;
        return leftRank > rightRank;
    });
    if (rows.size() > 12) rows.resize(12);
    return rows;
}

void DrawProcessTable(HDC hdc, const RECT& bounds, const DashboardUiState& state, const DashboardUiFonts& fonts, int maximumRows) {
    Panel(hdc, bounds, kSurface, kBorder, 7);
    DrawTitle(hdc, bounds, "Process opportunities", "Ranked by pressure, waste, safety, and expected gain", fonts);
    const int tableTop = bounds.top + 58;
    const int rowHeight = 25;
    Fill(hdc, RECT{bounds.left + 1, tableTop, bounds.right - 1, tableTop + rowHeight}, kSurfaceRaised);
    const int width = bounds.right - bounds.left - 28;
    const int x0 = bounds.left + 14;
    const int columns[] = {0, 28, 44, 55, 66, 77, 88, 100};
    const char* labels[] = {"PROCESS", "ROLE", "CPU", "RAM", "CRIT", "SAFETY", "GAIN", "RECOMMENDATION"};
    for (int index = 0; index < 8; ++index) {
        const int left = x0 + width * columns[index] / 100;
        const int right = index == 7 ? bounds.right - 10 : x0 + width * columns[index + 1] / 100 - 4;
        Text(hdc, RECT{left, tableTop, right, tableTop + rowHeight}, labels[index], kMuted, fonts.body);
    }

    const vector<ProcessSnapshot> rows = RankedProcesses(state);
    const int availableRows = max(0, (static_cast<int>(bounds.bottom) - tableTop - rowHeight - 8) / rowHeight);
    const int count = min({maximumRows, availableRows, static_cast<int>(rows.size())});
    for (int row = 0; row < count; ++row) {
        const ProcessSnapshot& process = rows[row];
        const int y = tableTop + rowHeight * (row + 1);
        if ((row % 2) != 0) Fill(hdc, RECT{bounds.left + 1, y, bounds.right - 1, y + rowHeight}, kSurfaceSoft);
        const string criticality = process.isForeground || process.matchesUserIntent ? "HIGH" :
            (process.isRecentlyActive || process.importanceScore >= 70.0 ? "MED" : "LOW");
        const string values[] = {
            process.name,
            DisplayToken(process.category),
            Number(process.cpuPercent, 1) + "%",
            Memory(process.workingSetMB),
            criticality,
            DisplayToken(process.safety),
            Memory(process.expectedGainMB),
            DisplayToken(process.recommendation),
        };
        for (int column = 0; column < 8; ++column) {
            const int left = x0 + width * columns[column] / 100;
            const int right = column == 7 ? bounds.right - 10 : x0 + width * columns[column + 1] / 100 - 4;
            COLORREF color = column == 5 ? (Upper(process.safety).find("SAFE") != string::npos ? kTeal : kAmber) : kText;
            if (process.isForeground && column == 0) color = kCyan;
            Text(hdc, RECT{left, y, right, y + rowHeight}, values[column], color, fonts.body);
        }
    }
    if (rows.empty()) {
        Text(hdc, RECT{bounds.left + 14, tableTop + 38, bounds.right - 14, tableTop + 70},
             "Collecting process intelligence...", kMuted, fonts.body);
    }
}

void InfoRow(HDC hdc, const RECT& panel, int y, const string& label, const string& value,
             const DashboardUiFonts& fonts, COLORREF valueColor = kText) {
    Text(hdc, RECT{panel.left + 14, y, panel.left + 150, y + 22}, label, kMuted, fonts.body);
    Text(hdc, RECT{panel.left + 148, y, panel.right - 14, y + 22}, value, valueColor, fonts.body);
}

void DrawOpportunity(HDC hdc, const RECT& bounds, const DashboardUiState& state, const DashboardUiFonts& fonts) {
    const bool blocked = !state.onlinePolicy.eligible || state.actionGlobalDisable || !state.actionExecutionEnabled;
    Panel(hdc, bounds, kSurface, blocked ? kAmber : kTeal, 7);
    DrawTitle(hdc, bounds, "Optimization opportunity", "Shadow recommendation - not executed", fonts);
    RECT status{bounds.right - 144, bounds.top + 9, bounds.right - 12, bounds.top + 34};
    Panel(hdc, status, blocked ? kAmberSoft : kGreenSoft, blocked ? kAmber : kTeal, 5);
    Text(hdc, status, blocked ? "LOCKED" : "ELIGIBLE", blocked ? kAmber : kTeal, fonts.body,
         DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    Text(hdc, RECT{bounds.left + 14, bounds.top + 62, bounds.right - 14, bounds.top + 84},
         "RECOMMENDED REVERSIBLE ACTION", kFaint, fonts.body);
    Text(hdc, RECT{bounds.left + 14, bounds.top + 83, bounds.right - 14, bounds.top + 116},
         Upper(DisplayToken(state.recommendedAction)), kText, fonts.section);
    InfoRow(hdc, bounds, bounds.top + 124, "Target", state.actionTarget, fonts);
    InfoRow(hdc, bounds, bounds.top + 151, "Expected gain", Memory(state.expectedGainMB) + " estimated", fonts, kCyan);
    InfoRow(hdc, bounds, bounds.top + 178, "Confidence", Percent(state.actionConfidence), fonts);
    InfoRow(hdc, bounds, bounds.top + 205, "Safety gate", DisplayToken(state.safetyGate), fonts, blocked ? kAmber : kTeal);
    InfoRow(hdc, bounds, bounds.top + 232, "Policy", DisplayToken(state.shadowPolicy.mode), fonts, kCyan);

    Rule(hdc, bounds.left + 14, bounds.top + 264, bounds.right - 14, bounds.top + 264);
    Text(hdc, RECT{bounds.left + 14, bounds.top + 277, bounds.right - 14, bounds.top + 301}, "WHY THIS ACTION", kFaint, fonts.body);
    Text(hdc, RECT{bounds.left + 14, bounds.top + 300, bounds.right - 14, bounds.top + 350},
         state.rootCauseDetail, kText, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    Text(hdc, RECT{bounds.left + 14, bounds.top + 356, bounds.right - 14, bounds.top + 407},
         "Blocked: " + state.blockedReason, blocked ? kAmber : kMuted, fonts.body,
         DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    const int footerTop = max(static_cast<int>(bounds.top) + 420, static_cast<int>(bounds.bottom) - 70);
    Rule(hdc, bounds.left + 14, footerTop - 8, bounds.right - 14, footerTop - 8);
    Text(hdc, RECT{bounds.left + 14, footerTop, bounds.right - 14, footerTop + 24},
         "Execution is disabled by default", kAmber, fonts.section);
    Text(hdc, RECT{bounds.left + 14, footerTop + 25, bounds.right - 14, footerTop + 49},
         "No process has been changed", kMuted, fonts.body);
}

void DrawFooter(HDC hdc, const RECT& client, const Layout& layout, const DashboardUiState& state, const DashboardUiFonts& fonts) {
    RECT footer{layout.sidebarWidth, client.bottom - layout.footerHeight, client.right, client.bottom};
    Fill(hdc, footer, kTopbar);
    Rule(hdc, footer.left, footer.top, footer.right, footer.top, kBorder);
    const int width = footer.right - footer.left;
    const int cellWidth = width / 7;
    const string labels[] = {"POLICY MODE", "MODEL", "BASELINE", "BENCHMARK", "DATABASE", "AGENT", "TELEMETRY"};
    const string values[] = {
        state.policyMode,
        state.modelVersion,
        state.baseline.ready ? "READY" : "LEARNING",
        "NO CLAIM",
        state.storageReady ? "SQLITE OK" : "OFFLINE",
        state.agent.trayIconReady ? "RUNNING" : "DASHBOARD",
        state.runtime.status,
    };
    const COLORREF colors[] = {kCyan, kText, state.baseline.ready ? kTeal : kAmber, kMuted,
                               state.storageReady ? kTeal : kRed, state.agent.trayIconReady ? kTeal : kMuted,
                               state.runtime.status == "HEALTHY" ? kTeal : kAmber};
    for (int index = 0; index < 7; ++index) {
        const int left = footer.left + index * cellWidth;
        if (index > 0) Rule(hdc, left, footer.top + 10, left, footer.bottom - 10, kBorder);
        Text(hdc, RECT{left + 12, footer.top + 7, left + cellWidth - 8, footer.top + 29}, labels[index], kFaint, fonts.body);
        Text(hdc, RECT{left + 12, footer.top + 28, left + cellWidth - 8, footer.bottom - 7}, values[index], colors[index], fonts.section);
    }
}

void DrawOverview(HDC hdc, const Layout& layout, const DashboardUiState& state, const DashboardUiFonts& fonts) {
    const RECT area = layout.content;
    const int width = static_cast<int>(area.right - area.left);
    const int height = static_cast<int>(area.bottom - area.top);
    const int summaryHeight = height < 640 ? 54 : 60;
    const int metricHeight = height < 640 ? 92 : 108;
    RECT summary{area.left, area.top, area.right, area.top + summaryHeight};
    Panel(hdc, summary, RiskFill(state.riskLevel), RiskAccent(state.riskLevel), 7);
    Text(hdc, RECT{summary.left + 14, summary.top + 6, summary.left + 190, summary.bottom - 4},
         "SYSTEM " + state.riskLevel, RiskAccent(state.riskLevel), fonts.section);
    Text(hdc, RECT{summary.left + 195, summary.top + 6, summary.right - 320, summary.bottom - 4},
         state.decisionSummary, kText, fonts.body);
    Text(hdc, RECT{summary.right - 305, summary.top + 6, summary.right - 14, summary.bottom - 4},
         "Risk " + Percent(state.riskScore) + "  |  AI " + Percent(state.aiProbability) + "  |  " + state.aiSource,
         kMuted, fonts.body, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

    const int metricTop = summary.bottom + layout.gap;
    const int metricGap = layout.gap;
    const int metricWidth = (width - metricGap * 3) / 4;
    RECT metricBounds[4];
    for (int index = 0; index < 4; ++index) {
        metricBounds[index] = RECT{area.left + index * (metricWidth + metricGap), metricTop,
                                   area.left + index * (metricWidth + metricGap) + metricWidth, metricTop + metricHeight};
    }
    DrawMetric(hdc, metricBounds[0], "CPU", Percent(state.system.cpuUsage), "Measured now", state.cpuHistory, kCyan, fonts);
    DrawMetric(hdc, metricBounds[1], "MEMORY", Percent(state.system.memoryUsage),
               Memory(state.system.availableMemoryMB) + " available", state.memoryHistory, kTeal, fonts);
    DrawMetric(hdc, metricBounds[2], "DISK FREE", Percent(state.system.diskFree), "Measured on system drive", state.diskHistory, kAmber, fonts);
    const bool latencyAvailable = state.qoe.inputAvailable && state.qoe.inputResponseMs > 0.0;
    DrawMetric(hdc, metricBounds[3], latencyAvailable ? "FOREGROUND RESPONSE" : "NETWORK",
               latencyAvailable ? Number(state.qoe.inputResponseMs, 0) + " ms" : Rate(state.system.netDownKBps),
               latencyAvailable ? "Input response proxy" : "Download rate", state.latencyHistory,
               latencyAvailable ? kBlue : kCyan, fonts, latencyAvailable ? 500.0 : 1024.0);

    const int bodyTop = metricTop + metricHeight + layout.gap;
    const int rightWidth = max(300, min(365, width / 3));
    const int leftRight = area.right - rightWidth - layout.gap;
    const int bodyHeight = area.bottom - bodyTop;
    const int chartHeight = max(190, bodyHeight * 52 / 100);
    RECT chart{area.left, bodyTop, leftRight, bodyTop + chartHeight};
    RECT table{area.left, chart.bottom + layout.gap, leftRight, area.bottom};
    RECT opportunity{leftRight + layout.gap, bodyTop, area.right, area.bottom};
    DrawChart(hdc, chart, state, fonts);
    DrawProcessTable(hdc, table, state, fonts, 7);
    DrawOpportunity(hdc, opportunity, state, fonts);
}

void DrawProcesses(HDC hdc, const Layout& layout, const DashboardUiState& state, const DashboardUiFonts& fonts) {
    const RECT area = layout.content;
    const int width = static_cast<int>(area.right - area.left);
    const int topHeight = 92;
    const int gap = layout.gap;
    const int cardWidth = (width - gap * 2) / 3;
    const string topName = state.system.topProcess.name;
    const string foreground = state.system.intent.foregroundProcess;
    const string candidate = state.autopilot.primaryTarget.empty() ? "system" : state.autopilot.primaryTarget;
    const string labels[] = {"FOREGROUND PROTECTED", "TOP PRESSURE PROCESS", "BEST SAFE CANDIDATE"};
    const string values[] = {foreground, topName, candidate};
    const string notes[] = {state.system.intent.appKind, state.system.topProcess.reason, state.autopilot.primaryAction};
    const COLORREF colors[] = {kTeal, kAmber, kCyan};
    for (int index = 0; index < 3; ++index) {
        RECT card{area.left + index * (cardWidth + gap), area.top,
                  area.left + index * (cardWidth + gap) + cardWidth, area.top + topHeight};
        Panel(hdc, card, kSurface, kBorder, 7);
        Fill(hdc, RECT{card.left, card.top, card.left + 3, card.bottom}, colors[index]);
        Text(hdc, RECT{card.left + 14, card.top + 8, card.right - 10, card.top + 31}, labels[index], kMuted, fonts.body);
        Text(hdc, RECT{card.left + 14, card.top + 31, card.right - 10, card.top + 59}, values[index], kText, fonts.section);
        Text(hdc, RECT{card.left + 14, card.top + 62, card.right - 10, card.bottom - 6}, notes[index], kFaint, fonts.body);
    }
    const int bodyTop = area.top + topHeight + gap;
    const int inspectorWidth = max(290, width / 4);
    RECT table{area.left, bodyTop, area.right - inspectorWidth - gap, area.bottom};
    RECT inspector{table.right + gap, bodyTop, area.right, area.bottom};
    DrawProcessTable(hdc, table, state, fonts, 14);
    Panel(hdc, inspector, kSurface, kBorder, 7);
    DrawTitle(hdc, inspector, "Process intelligence", "Selected pressure candidate", fonts);
    Text(hdc, RECT{inspector.left + 14, inspector.top + 60, inspector.right - 14, inspector.top + 94},
         state.system.topProcess.name, kText, fonts.section);
    InfoRow(hdc, inspector, inspector.top + 105, "PID", to_string(state.system.topProcess.pid), fonts);
    InfoRow(hdc, inspector, inspector.top + 132, "Category", DisplayToken(state.system.topProcess.category), fonts);
    InfoRow(hdc, inspector, inspector.top + 159, "CPU", Number(state.system.topProcess.cpuPercent, 1) + "%", fonts);
    InfoRow(hdc, inspector, inspector.top + 186, "Working set", Memory(state.system.topProcess.memoryMB), fonts);
    InfoRow(hdc, inspector, inspector.top + 213, "Private RAM", Memory(state.system.topProcess.privateMemoryMB), fonts);
    InfoRow(hdc, inspector, inspector.top + 240, "Waste score", Number(state.system.topProcess.wasteScore, 0), fonts);
    InfoRow(hdc, inspector, inspector.top + 267, "Safety", DisplayToken(state.system.topProcess.safety), fonts, kAmber);
    InfoRow(hdc, inspector, inspector.top + 294, "Expected gain", Memory(state.system.topProcess.expectedGainMB) + " estimated", fonts, kCyan);
    Rule(hdc, inspector.left + 14, inspector.top + 329, inspector.right - 14, inspector.top + 329);
    Text(hdc, RECT{inspector.left + 14, inspector.top + 342, inspector.right - 14, inspector.top + 366}, "REASONING", kFaint, fonts.body);
    Text(hdc, RECT{inspector.left + 14, inspector.top + 366, inspector.right - 14, inspector.top + 434},
         state.system.topProcess.reason, kText, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    Text(hdc, RECT{inspector.left + 14, inspector.bottom - 58, inspector.right - 14, inspector.bottom - 34},
         "No process changes executed", kAmber, fonts.section);
    Text(hdc, RECT{inspector.left + 14, inspector.bottom - 34, inspector.right - 14, inspector.bottom - 10},
         "Recommendations remain reversible and locked", kMuted, fonts.body);
}

void DrawIntelligence(HDC hdc, const Layout& layout, const DashboardUiState& state, const DashboardUiFonts& fonts) {
    const RECT area = layout.content;
    const int gap = layout.gap;
    const int width = static_cast<int>(area.right - area.left);
    const int col = (width - gap * 2) / 3;
    const int topHeight = max(235, static_cast<int>(area.bottom - area.top) * 46 / 100);
    RECT risk{area.left, area.top, area.left + col, area.top + topHeight};
    RECT decision{risk.right + gap, area.top, risk.right + gap + col, area.top + topHeight};
    RECT qoe{decision.right + gap, area.top, area.right, area.top + topHeight};
    Panel(hdc, risk, kSurface, RiskAccent(state.riskLevel), 7);
    DrawTitle(hdc, risk, "AI risk model", "State prediction and reliability", fonts);
    Label(hdc, risk.left + 14, risk.top + 66, 130, "RISK", fonts);
    Value(hdc, risk.left + 14, risk.top + 88, 150, Percent(state.aiProbability), fonts, RiskAccent(state.riskLevel));
    InfoRow(hdc, risk, risk.top + 142, "Confidence", Percent(state.aiConfidence), fonts);
    InfoRow(hdc, risk, risk.top + 169, "Source", state.aiSource, fonts, state.aiSource == "MODEL" ? kTeal : kAmber);
    InfoRow(hdc, risk, risk.top + 196, "Class", state.aiClass, fonts);
    InfoRow(hdc, risk, risk.top + 223, "Features", to_string(state.modelFeatureCount), fonts);
    Text(hdc, RECT{risk.left + 14, risk.top + 257, risk.right - 14, risk.bottom - 12},
         state.aiReason, kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    Panel(hdc, decision, kSurface, kBorder, 7);
    DrawTitle(hdc, decision, "Decision engine", "Deterministic risk and root cause", fonts);
    InfoRow(hdc, decision, decision.top + 68, "Risk", Percent(state.riskScore), fonts, RiskAccent(state.riskLevel));
    InfoRow(hdc, decision, decision.top + 95, "Anomaly", Percent(state.anomalyScore), fonts);
    InfoRow(hdc, decision, decision.top + 122, "Pressure", Percent(state.pressureScore), fonts);
    InfoRow(hdc, decision, decision.top + 149, "Root cause", DisplayToken(state.rootCause), fonts, kAmber);
    InfoRow(hdc, decision, decision.top + 176, "Action", DisplayToken(state.recommendedAction), fonts, kCyan);
    Text(hdc, RECT{decision.left + 14, decision.top + 214, decision.right - 14, decision.bottom - 12},
         state.rootCauseDetail, kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    Panel(hdc, qoe, kSurface, kBorder, 7);
    DrawTitle(hdc, qoe, "Foreground QoE", "User-facing performance signals", fonts);
    InfoRow(hdc, qoe, qoe.top + 68, "Workload", DisplayToken(ToString(state.qoe.workload)), fonts, kCyan);
    InfoRow(hdc, qoe, qoe.top + 95, "Input response", state.qoe.inputAvailable ? Number(state.qoe.inputResponseMs, 0) + " ms" : "Unavailable", fonts);
    InfoRow(hdc, qoe, qoe.top + 122, "Page reads", state.qoe.pageReadAvailable ? Number(state.qoe.systemPageReadsPerSecond, 1) + "/s" : "Unavailable", fonts);
    InfoRow(hdc, qoe, qoe.top + 149, "Disk queue", state.qoe.diskQueueAvailable ? Number(state.qoe.diskQueueLength, 2) : "Unavailable", fonts);
    InfoRow(hdc, qoe, qoe.top + 176, "Overhead", Number(state.qoe.collectionOverheadMs, 2) + " ms", fonts,
            state.qoe.withinOverheadBudget ? kTeal : kAmber);
    InfoRow(hdc, qoe, qoe.top + 203, "Critical graph", to_string(state.criticality.nodes.size()) + " nodes", fonts);
    Text(hdc, RECT{qoe.left + 14, qoe.top + 240, qoe.right - 14, qoe.bottom - 12},
         state.qoe.availability, kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    const int lowerTop = risk.bottom + gap;
    const int lowerWidth = (width - gap) / 2;
    RECT shadow{area.left, lowerTop, area.left + lowerWidth, area.bottom};
    RECT baseline{shadow.right + gap, lowerTop, area.right, area.bottom};
    Panel(hdc, shadow, kSurface, kCyan, 7);
    DrawTitle(hdc, shadow, "Action-impact learning", "Contextual policy operates in shadow mode", fonts);
    InfoRow(hdc, shadow, shadow.top + 66, "Policy", state.shadowPolicy.policyVersion, fonts);
    InfoRow(hdc, shadow, shadow.top + 93, "Mode", state.shadowPolicy.mode, fonts, kCyan);
    InfoRow(hdc, shadow, shadow.top + 120, "Would act", state.shadowPolicy.wouldAct ? "YES" : "NO", fonts,
            state.shadowPolicy.wouldAct ? kAmber : kTeal);
    InfoRow(hdc, shadow, shadow.top + 147, "Expected reward", Number(state.shadowPolicy.expectedReward, 3), fonts);
    InfoRow(hdc, shadow, shadow.top + 174, "Uncertainty", Number(state.shadowPolicy.uncertainty, 3), fonts);
    InfoRow(hdc, shadow, shadow.top + 201, "Lower bound", Number(state.shadowPolicy.lowerConfidenceBound, 3), fonts);
    Text(hdc, RECT{shadow.left + 14, shadow.top + 234, shadow.right - 14, shadow.bottom - 10},
         state.shadowPolicy.reason, kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    Panel(hdc, baseline, kSurface, state.baseline.ready ? kTeal : kAmber, 7);
    DrawTitle(hdc, baseline, "Per-device baseline", "Learns what is normal for this hardware", fonts);
    InfoRow(hdc, baseline, baseline.top + 66, "Status", DisplayToken(state.baseline.status), fonts,
            state.baseline.ready ? kTeal : kAmber);
    InfoRow(hdc, baseline, baseline.top + 93, "Samples", to_string(state.baseline.sampleCount), fonts);
    InfoRow(hdc, baseline, baseline.top + 120, "Confidence", Percent(state.baseline.confidence), fonts);
    InfoRow(hdc, baseline, baseline.top + 147, "Dominant drift", DisplayToken(state.baseline.dominantMetric), fonts);
    InfoRow(hdc, baseline, baseline.top + 174, "Anomaly", Percent(state.baseline.anomalyScore), fonts);
    InfoRow(hdc, baseline, baseline.top + 201, "CPU baseline", Percent(state.baseline.cpuMean), fonts);
    InfoRow(hdc, baseline, baseline.top + 228, "Memory baseline", Percent(state.baseline.memoryMean), fonts);
    Text(hdc, RECT{baseline.left + 14, baseline.top + 260, baseline.right - 14, baseline.bottom - 10},
         state.baseline.summary, kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
}

void DrawGate(HDC hdc, const RECT& panel, int y, const string& label, bool passed, const DashboardUiFonts& fonts) {
    Fill(hdc, RECT{panel.left + 14, y + 6, panel.left + 22, y + 14}, passed ? kTeal : kAmber);
    Text(hdc, RECT{panel.left + 31, y, panel.right - 90, y + 22}, label, kText, fonts.body);
    Text(hdc, RECT{panel.right - 86, y, panel.right - 14, y + 22}, passed ? "PASS" : "BLOCK", passed ? kTeal : kAmber,
         fonts.body, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
}

void DrawSafety(HDC hdc, const Layout& layout, const DashboardUiState& state, const DashboardUiFonts& fonts) {
    const RECT area = layout.content;
    const int gap = layout.gap;
    const int width = static_cast<int>(area.right - area.left);
    const int leftWidth = width * 44 / 100;
    RECT lock{area.left, area.top, area.left + leftWidth, area.top + 158};
    Panel(hdc, lock, kAmberSoft, kAmber, 7);
    DrawTitle(hdc, lock, "Safety lock is ON", "Real resource actions remain disabled", fonts);
    Text(hdc, RECT{lock.left + 14, lock.top + 62, lock.right - 14, lock.top + 98},
         "NO PROCESS HAS BEEN CHANGED", kAmber, fonts.section);
    Text(hdc, RECT{lock.left + 14, lock.top + 101, lock.right - 14, lock.bottom - 12},
         "Configuration, offline promotion evidence, deterministic safety gates, allowlisting, and approval must all pass before execution.",
         kText, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    RECT gate{lock.right + gap, area.top, area.right, area.top + 158};
    Panel(hdc, gate, kSurface, kBorder, 7);
    DrawTitle(hdc, gate, "Execution gates", "Independent locks cannot be bypassed by AI", fonts);
    DrawGate(hdc, gate, gate.top + 61, "Execution switch enabled", state.actionExecutionEnabled, fonts);
    DrawGate(hdc, gate, gate.top + 86, "Global safety lock released", !state.actionGlobalDisable, fonts);
    DrawGate(hdc, gate, gate.top + 111, "Online policy promoted", state.onlinePolicyPromoted, fonts);
    DrawGate(hdc, gate, gate.top + 136, "Current decision eligible", state.onlinePolicy.eligible, fonts);

    const int bodyTop = lock.bottom + gap;
    const int bodyWidth = (width - gap * 2) / 3;
    RECT policy{area.left, bodyTop, area.left + bodyWidth, area.bottom};
    RECT plan{policy.right + gap, bodyTop, policy.right + gap + bodyWidth, area.bottom};
    RECT verify{plan.right + gap, bodyTop, area.right, area.bottom};
    Panel(hdc, policy, kSurface, state.safetyPolicy.executionEligible ? kTeal : kAmber, 7);
    DrawTitle(hdc, policy, "Deterministic safety policy", state.safetyPolicy.reasonCode, fonts);
    InfoRow(hdc, policy, policy.top + 65, "Level", DisplayToken(state.safetyPolicy.levelName), fonts, kAmber);
    InfoRow(hdc, policy, policy.top + 92, "Score", Percent(state.safetyPolicy.policyScore), fonts);
    InfoRow(hdc, policy, policy.top + 119, "Required", Percent(state.safetyPolicy.minimumRequiredScore), fonts);
    InfoRow(hdc, policy, policy.top + 146, "Target", state.safetyPolicy.targetName, fonts);
    InfoRow(hdc, policy, policy.top + 173, "Protected", state.safetyPolicy.targetProtected ? "YES" : "NO", fonts,
            state.safetyPolicy.targetProtected ? kAmber : kTeal);
    InfoRow(hdc, policy, policy.top + 200, "Approval", state.safetyPolicy.requiresApproval ? "REQUIRED" : "NOT REQUIRED", fonts);
    Text(hdc, RECT{policy.left + 14, policy.top + 235, policy.right - 14, policy.bottom - 12},
         state.safetyPolicy.reason, kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    Panel(hdc, plan, kSurface, kBorder, 7);
    DrawTitle(hdc, plan, "Dry-run action plan", state.healPlan.executionMode, fonts);
    InfoRow(hdc, plan, plan.top + 65, "Status", DisplayToken(state.healPlan.status), fonts);
    InfoRow(hdc, plan, plan.top + 92, "Action", DisplayToken(state.healPlan.actionName), fonts, kCyan);
    InfoRow(hdc, plan, plan.top + 119, "Target", state.healPlan.targetName, fonts);
    InfoRow(hdc, plan, plan.top + 146, "Readiness", Percent(state.healPlan.readinessScore), fonts);
    InfoRow(hdc, plan, plan.top + 173, "Expected gain", Memory(state.healPlan.expectedGainMB) + " estimated", fonts);
    Text(hdc, RECT{plan.left + 14, plan.top + 211, plan.right - 14, plan.top + 258},
         "Pre-check: " + state.healPlan.preCheck, kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    Text(hdc, RECT{plan.left + 14, plan.top + 266, plan.right - 14, plan.top + 320},
         "Rollback: " + state.healPlan.rollbackPlan, kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    Text(hdc, RECT{plan.left + 14, plan.bottom - 48, plan.right - 14, plan.bottom - 12},
         state.healPlan.blockedReason, kAmber, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    Panel(hdc, verify, kSurface, kBorder, 7);
    DrawTitle(hdc, verify, "Outcome verification", "Estimates and measurements never mix", fonts);
    InfoRow(hdc, verify, verify.top + 65, "Status", DisplayToken(state.healVerification.status), fonts);
    InfoRow(hdc, verify, verify.top + 92, "Mode", DisplayToken(state.healVerification.mode), fonts, kAmber);
    InfoRow(hdc, verify, verify.top + 119, "Before risk", Percent(state.healVerification.riskBefore), fonts);
    InfoRow(hdc, verify, verify.top + 146, "After estimate", Percent(state.healVerification.riskAfterEstimate), fonts, kCyan);
    InfoRow(hdc, verify, verify.top + 173, "Observation", to_string(state.healVerification.observationWindowSeconds) + " sec", fonts);
    Text(hdc, RECT{verify.left + 14, verify.top + 211, verify.right - 14, verify.top + 271},
         state.healVerification.evidence, kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    Rule(hdc, verify.left + 14, verify.bottom - 86, verify.right - 14, verify.bottom - 86);
    Text(hdc, RECT{verify.left + 14, verify.bottom - 75, verify.right - 14, verify.bottom - 49},
         "QUICK RESTORE: " + Upper(DisplayToken(state.agent.quickRestoreStatus)), kCyan, fonts.section);
    Text(hdc, RECT{verify.left + 14, verify.bottom - 45, verify.right - 14, verify.bottom - 12},
         state.agent.quickRestoreAvailable ? "Available for executed reversible actions" : "No executed actions to restore",
         kMuted, fonts.body);
}

void DrawExperiments(HDC hdc, const Layout& layout, const DashboardUiState& state, const DashboardUiFonts& fonts) {
    const RECT area = layout.content;
    const int gap = layout.gap;
    const int width = static_cast<int>(area.right - area.left);
    const int topHeight = max(250, static_cast<int>(area.bottom - area.top) * 55 / 100);
    const int leftWidth = width * 58 / 100;
    RECT simulation{area.left, area.top, area.left + leftWidth, area.top + topHeight};
    RECT evidence{simulation.right + gap, area.top, area.right, area.top + topHeight};
    Panel(hdc, simulation, kSurface, kAmber, 7);
    DrawTitle(hdc, simulation, "Impact simulation", "Estimated outcome - not measured proof", fonts);
    const int half = (simulation.right - simulation.left - 42) / 2;
    RECT before{simulation.left + 14, simulation.top + 64, simulation.left + 14 + half, simulation.top + 190};
    RECT after{before.right + 12, before.top, simulation.right - 14, before.bottom};
    Panel(hdc, before, kSurfaceSoft, kBorderStrong, 6);
    Panel(hdc, after, kSurfaceSoft, kCyan, 6);
    Text(hdc, RECT{before.left + 12, before.top + 8, before.right - 8, before.top + 32}, "CURRENT - MEASURED", kMuted, fonts.body);
    InfoRow(hdc, before, before.top + 40, "CPU", Percent(state.benchmark.beforeCpu), fonts);
    InfoRow(hdc, before, before.top + 67, "Memory", Percent(state.benchmark.beforeMemory), fonts);
    InfoRow(hdc, before, before.top + 94, "Risk", Percent(state.benchmark.beforeRisk), fonts);
    Text(hdc, RECT{after.left + 12, after.top + 8, after.right - 8, after.top + 32}, "AFTER - ESTIMATED", kCyan, fonts.body);
    InfoRow(hdc, after, after.top + 40, "CPU", Percent(state.benchmark.afterCpuEstimate), fonts);
    InfoRow(hdc, after, after.top + 67, "Memory", Percent(state.benchmark.afterMemoryEstimate), fonts);
    InfoRow(hdc, after, after.top + 94, "Risk", Percent(state.benchmark.afterRiskEstimate), fonts);
    InfoRow(hdc, simulation, simulation.top + 211, "Estimated RAM recovery", Memory(state.benchmark.recoveredRamMB), fonts, kCyan);
    InfoRow(hdc, simulation, simulation.top + 238, "Estimated CPU drop", Number(state.benchmark.cpuDropPercent, 1) + "%", fonts);
    InfoRow(hdc, simulation, simulation.top + 265, "Actions proposed", to_string(state.benchmark.actionsRecommended), fonts);
    InfoRow(hdc, simulation, simulation.top + 292, "User apps touched", to_string(state.benchmark.userAppsTouched), fonts,
            state.benchmark.userAppsTouched == 0 ? kTeal : kAmber);
    Text(hdc, RECT{simulation.left + 14, simulation.bottom - 46, simulation.right - 14, simulation.bottom - 12},
         "These values must not be used as a performance claim.", kAmber, fonts.section);

    Panel(hdc, evidence, kSurface, kBorder, 7);
    DrawTitle(hdc, evidence, "Research evidence", "Claim gates protect against misleading results", fonts);
    Text(hdc, RECT{evidence.left + 14, evidence.top + 68, evidence.right - 14, evidence.top + 104},
         "NO CLAIM", kMuted, fonts.value);
    InfoRow(hdc, evidence, evidence.top + 118, "Current source", "Simulation only", fonts, kAmber);
    InfoRow(hdc, evidence, evidence.top + 145, "Real repetitions", "Pending", fonts);
    InfoRow(hdc, evidence, evidence.top + 172, "Randomized pairs", "Pending", fonts);
    InfoRow(hdc, evidence, evidence.top + 199, "95% confidence", "Pending", fonts);
    InfoRow(hdc, evidence, evidence.top + 226, "Safety failures", "Must equal zero", fonts);
    InfoRow(hdc, evidence, evidence.top + 253, "Independent test", "Pending", fonts);
    Text(hdc, RECT{evidence.left + 14, evidence.bottom - 73, evidence.right - 14, evidence.bottom - 12},
         "A qualified claim appears only after measured campaigns pass every predefined quality and safety gate.",
         kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    const int lowerTop = simulation.bottom + gap;
    const int lowerWidth = (width - gap * 2) / 3;
    const string titles[] = {"Benchmark laboratory", "Policy promotion", "Negative results"};
    const string states[] = {"READY FOR REAL RUNS", "SHADOW ONLY", "RETAINED"};
    const string descriptions[] = {
        "Randomized cold/warm plans, raw JSONL validation, median/p95/p99, bootstrap confidence intervals.",
        "Offline lower-confidence benefit must beat the no-intervention baseline before promotion.",
        "Unsafe, neutral, and failed experiments stay visible and are never removed from reports.",
    };
    for (int index = 0; index < 3; ++index) {
        RECT card{area.left + index * (lowerWidth + gap), lowerTop,
                  area.left + index * (lowerWidth + gap) + lowerWidth, area.bottom};
        Panel(hdc, card, kSurface, index == 0 ? kCyan : kBorder, 7);
        DrawTitle(hdc, card, titles[index], states[index], fonts);
        Text(hdc, RECT{card.left + 14, card.top + 68, card.right - 14, card.bottom - 12}, descriptions[index],
             kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    }
}

void DrawSettings(HDC hdc, const Layout& layout, const DashboardUiState& state, const DashboardUiFonts& fonts) {
    const RECT area = layout.content;
    const int gap = layout.gap;
    const int width = static_cast<int>(area.right - area.left);
    const int col = (width - gap * 2) / 3;
    RECT runtime{area.left, area.top, area.left + col, area.bottom};
    RECT config{runtime.right + gap, area.top, runtime.right + gap + col, area.bottom};
    RECT integrations{config.right + gap, area.top, area.right, area.bottom};
    Panel(hdc, runtime, kSurface, state.runtime.status == "HEALTHY" ? kTeal : kAmber, 7);
    DrawTitle(hdc, runtime, "Runtime and storage", state.runtime.summary, fonts);
    InfoRow(hdc, runtime, runtime.top + 68, "Health", state.runtime.status, fonts,
            state.runtime.status == "HEALTHY" ? kTeal : kAmber);
    InfoRow(hdc, runtime, runtime.top + 95, "Mode", state.performanceMode, fonts);
    InfoRow(hdc, runtime, runtime.top + 122, "Model interval", to_string(state.predictIntervalSeconds) + " sec", fonts);
    InfoRow(hdc, runtime, runtime.top + 149, "Cache", to_string(state.cacheSeconds) + " sec", fonts);
    InfoRow(hdc, runtime, runtime.top + 176, "Model success", Percent(state.runtime.modelSuccessRate), fonts);
    InfoRow(hdc, runtime, runtime.top + 203, "Fallback", Percent(state.runtime.fallbackRate), fonts,
            state.runtime.fallbackRate > 20.0 ? kAmber : kTeal);
    InfoRow(hdc, runtime, runtime.top + 230, "Prediction latency", Number(state.runtime.avgPredictionLatencyMs, 0) + " ms", fonts);
    InfoRow(hdc, runtime, runtime.top + 257, "SQLite", state.storageReady ? "HEALTHY" : "OFFLINE", fonts,
            state.storageReady ? kTeal : kRed);
    InfoRow(hdc, runtime, runtime.top + 284, "Write queue", to_string(state.pendingWrites), fonts);
    InfoRow(hdc, runtime, runtime.top + 311, "Storage success", Percent(state.runtime.storageSuccessRate), fonts);
    InfoRow(hdc, runtime, runtime.top + 338, "Agent", state.agent.status, fonts);
    InfoRow(hdc, runtime, runtime.top + 365, "Tray icon", state.agent.trayIconReady ? "READY" : "NOT READY", fonts);
    Text(hdc, RECT{runtime.left + 14, runtime.bottom - 74, runtime.right - 14, runtime.bottom - 12},
         "Last failure: " + state.runtime.lastFailure, kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    Panel(hdc, config, kSurface, kBorder, 7);
    DrawTitle(hdc, config, "Monitoring configuration", "Read from config.txt at startup", fonts);
    InfoRow(hdc, config, config.top + 68, "CPU threshold", to_string(state.cpuThreshold) + "%", fonts);
    InfoRow(hdc, config, config.top + 95, "Memory threshold", to_string(state.memoryThreshold) + "%", fonts);
    InfoRow(hdc, config, config.top + 122, "Disk free threshold", to_string(state.diskThreshold) + "%", fonts);
    InfoRow(hdc, config, config.top + 149, "Warning risk", to_string(state.warningThreshold) + "%", fonts);
    InfoRow(hdc, config, config.top + 176, "Critical risk", to_string(state.criticalThreshold) + "%", fonts);
    InfoRow(hdc, config, config.top + 203, "Scenario label", Upper(state.system.scenarioLabel), fonts);
    Rule(hdc, config.left + 14, config.top + 239, config.right - 14, config.top + 239);
    Text(hdc, RECT{config.left + 14, config.top + 252, config.right - 14, config.top + 278}, "ACTION CONTROLS", kFaint, fonts.body);
    InfoRow(hdc, config, config.top + 282, "Execution", state.actionExecutionEnabled ? "ENABLED" : "DISABLED", fonts,
            state.actionExecutionEnabled ? kAmber : kTeal);
    InfoRow(hdc, config, config.top + 309, "Global lock", state.actionGlobalDisable ? "ON" : "OFF", fonts,
            state.actionGlobalDisable ? kTeal : kAmber);
    InfoRow(hdc, config, config.top + 336, "Online policy", state.onlinePolicyEnabled ? "ENABLED" : "DISABLED", fonts);
    InfoRow(hdc, config, config.top + 363, "Promoted", state.onlinePolicyPromoted ? "YES" : "NO", fonts);
    Text(hdc, RECT{config.left + 14, config.bottom - 74, config.right - 14, config.bottom - 12},
         "Restart the application after editing config.txt. Safety settings intentionally cannot be changed from this dashboard.",
         kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    Panel(hdc, integrations, kSurface, kBorder, 7);
    DrawTitle(hdc, integrations, "Cooperative integrations", "Opt-in and capability detected", fonts);
    InfoRow(hdc, integrations, integrations.top + 68, "Browser bridge", state.browserIntegrationEnabled ? "ENABLED" : "OFF", fonts,
            state.browserIntegrationEnabled ? kCyan : kMuted);
    InfoRow(hdc, integrations, integrations.top + 95, "BITS adapter", state.bitsIntegrationEnabled ? "ENABLED" : "OFF", fonts,
            state.bitsIntegrationEnabled ? kCyan : kMuted);
    InfoRow(hdc, integrations, integrations.top + 122, "Prefetch", state.prefetchEnabled ? "ENABLED" : "OFF", fonts,
            state.prefetchEnabled ? kCyan : kMuted);
    Rule(hdc, integrations.left + 14, integrations.top + 158, integrations.right - 14, integrations.top + 158);
    Text(hdc, RECT{integrations.left + 14, integrations.top + 171, integrations.right - 14, integrations.top + 197}, "BACKGROUND AGENT", kFaint, fonts.body);
    InfoRow(hdc, integrations, integrations.top + 203, "Status", state.agent.status, fonts);
    InfoRow(hdc, integrations, integrations.top + 230, "Silent monitor", state.agent.silentMonitoring ? "ACTIVE" : "OFF", fonts);
    InfoRow(hdc, integrations, integrations.top + 257, "Start on boot", state.agent.startOnBootConfigured ? "CONFIGURED" : "OPTIONAL", fonts);
    InfoRow(hdc, integrations, integrations.top + 284, "Dashboard", state.agent.dashboardVisible ? "VISIBLE" : "HIDDEN", fonts);
    InfoRow(hdc, integrations, integrations.top + 311, "Quick Restore", state.agent.quickRestoreAvailable ? "AVAILABLE" : "IDLE", fonts);
    Rule(hdc, integrations.left + 14, integrations.top + 348, integrations.right - 14, integrations.top + 348);
    Text(hdc, RECT{integrations.left + 14, integrations.top + 361, integrations.right - 14, integrations.top + 387}, "MODEL CONTRACT", kFaint, fonts.body);
    InfoRow(hdc, integrations, integrations.top + 393, "Readiness", DisplayToken(state.modelReadiness), fonts);
    InfoRow(hdc, integrations, integrations.top + 420, "Feature count", to_string(state.modelFeatureCount), fonts);
    Text(hdc, RECT{integrations.left + 14, integrations.bottom - 74, integrations.right - 14, integrations.bottom - 12},
         "Model built: " + state.modelGeneratedAt, kMuted, fonts.body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
}

} // namespace

void DrawModernDashboard(HDC hdc, const RECT& client, const DashboardUiState& state, DashboardView view, const DashboardUiFonts& fonts) {
    const Layout layout = BuildLayout(client);
    DrawChrome(hdc, client, layout, state, view, fonts);
    switch (view) {
    case DashboardView::Overview:
        DrawOverview(hdc, layout, state, fonts);
        break;
    case DashboardView::Processes:
        DrawProcesses(hdc, layout, state, fonts);
        break;
    case DashboardView::Intelligence:
        DrawIntelligence(hdc, layout, state, fonts);
        break;
    case DashboardView::Safety:
        DrawSafety(hdc, layout, state, fonts);
        break;
    case DashboardView::Experiments:
        DrawExperiments(hdc, layout, state, fonts);
        break;
    case DashboardView::Settings:
        DrawSettings(hdc, layout, state, fonts);
        break;
    }
    DrawFooter(hdc, client, layout, state, fonts);
}

DashboardView HitTestDashboardNavigation(const RECT& client, int x, int y, DashboardView current) {
    const Layout layout = BuildLayout(client);
    const DashboardView views[] = {
        DashboardView::Overview,
        DashboardView::Processes,
        DashboardView::Intelligence,
        DashboardView::Safety,
        DashboardView::Experiments,
        DashboardView::Settings,
    };
    for (int index = 0; index < 6; ++index) {
        const RECT bounds = NavRect(layout, index);
        if (x >= bounds.left && x <= bounds.right && y >= bounds.top && y <= bounds.bottom) return views[index];
    }
    return current;
}



bool HitTestDashboardManualCanary(const RECT& client, int x, int y, DashboardView current) {
    if (current != DashboardView::Overview) return false;
    const Layout layout = BuildLayout(client);
    const RECT area = layout.content;
    const int width = static_cast<int>(area.right - area.left);
    const int height = static_cast<int>(area.bottom - area.top);
    const int summaryHeight = height < 640 ? 54 : 60;
    const int metricHeight = height < 640 ? 92 : 108;
    const int bodyTop = area.top + summaryHeight + layout.gap + metricHeight + layout.gap;
    const int rightWidth = max(300, min(365, width / 3));
    const int leftRight = area.right - rightWidth - layout.gap;
    const RECT opportunity{leftRight + layout.gap, bodyTop, area.right, area.bottom};
    const int footerTop = max(static_cast<int>(opportunity.top) + 420, static_cast<int>(opportunity.bottom) - 70);
    const RECT button{opportunity.left + 14, footerTop, opportunity.right - 14, footerTop + 27};
    return x >= button.left && x <= button.right && y >= button.top && y <= button.bottom;
}
