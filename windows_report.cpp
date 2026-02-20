// Self-contained C++ program for Windows 10 GUI (WinAPI) that
// simulates an energy report and displays several charts (line, bar, pie,
// table, checklist) in one window. The program uses a seeded RNG to
// generate reproducible data matching the logic of the provided JS code.
// It also contains stubs for ESC/POS printing; printing to a BLE thermal
// printer is highly device specific, so the print function here generates
// the ESC/POS command sequence and writes it to a file or stub. To
// actually print via Bluetooth, you would need to open a serial port
// mapped to your printer or use a dedicated library.

#include <windows.h>
#include <commctrl.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

// Link common controls library
#pragma comment(lib, "comctl32.lib")

// ---- RNG implementation matching JS/Swift version ----
class SeededRNG {
public:
    explicit SeededRNG(uint64_t seed) : state(seed & ((1ULL << 64) - 1)) {}
    // Linear congruential generator constants as in JS/Swift code
    uint64_t next() {
        const uint64_t a = 6364136223846793005ULL;
        const uint64_t c = 1ULL;
        state = state * a + c;
        return state;
    }
    double nextDouble01() {
        uint64_t v = next() >> 11;
        // convert top 53 bits to [0,1)
        return static_cast<double>(v) / static_cast<double>(1ULL << 53);
    }
private:
    uint64_t state;
};

// ---- Data structures for energy report ----
struct Consumer {
    std::string name;
    double kWh;
};
struct Category {
    std::string name;
    double kWh;
};
struct EnergyDay {
    std::string buildingName;
    SYSTEMTIME date;
    std::vector<double> hourlyKWh;
    std::vector<Consumer> topConsumers;
    std::vector<Category> categoryBreakdown;
    double priceCZKPerKWh;
};

// Simulate a day of energy usage with reproducible random variations
EnergyDay simulateEnergyDay() {
    SeededRNG rng(0xC0FFEEULL);
    EnergyDay day;
    day.buildingName = "Kancelářská budova A (menší)";
    // current date
    GetLocalTime(&day.date);
    day.priceCZKPerKWh = 3.20;
    day.hourlyKWh.resize(24);
    for (int h = 0; h < 24; ++h) {
        double baseNight = 6.5;
        double baseWork = 16.0;
        double baseEvening = 10.0;
        double base;
        if (h < 6 || h >= 23) base = baseNight;
        else if (h < 8) base = baseNight + 3.0;
        else if (h <= 18) base = baseWork;
        else base = baseEvening;
        double wave = (h >= 8 && h <= 18) ? (7.0 * sin((h - 8.0) / 10.0 * 3.14159265358979323846)) : 0.0;
        double noise = (rng.nextDouble01() - 0.5) * 2.0;
        double spike = (rng.nextDouble01() < 0.08) ? (5.0 + rng.nextDouble01() * 10.0) : 0.0;
        double kWh = std::max(3.0, base + wave + noise + spike);
        day.hourlyKWh[h] = kWh;
    }
    // total consumption
    double total = 0.0;
    for (double v : day.hourlyKWh) total += v;
    // categories (shares)
    const struct { const char* name; double share; } cats[] = {
        {"HVAC (chlazení + VZT)", 0.42},
        {"Osvětlení", 0.22},
        {"IT + serverovna", 0.18},
        {"Zásuvky / kuchyňky", 0.10},
        {"Ostatní", 0.08}
    };
    day.categoryBreakdown.clear();
    for (auto& c : cats) {
        Category cat;
        cat.name = c.name;
        cat.kWh = total * c.share;
        day.categoryBreakdown.push_back(cat);
    }
    // top consumers
    const struct { const char* name; double share; } consumersRaw[] = {
        {"Chiller / tepelné čerpadlo", 0.22},
        {"VZT jednotky", 0.17},
        {"Osvětlení open-space", 0.15},
        {"Serverovna UPS", 0.14},
        {"EV nabíjení", 0.10},
        {"Výtahy", 0.05},
        {"Ostatní", 0.17}
    };
    std::vector<Consumer> list;
    for (auto& cr : consumersRaw) {
        Consumer cons;
        cons.name = cr.name;
        cons.kWh = total * cr.share;
        list.push_back(cons);
    }
    // sort by descending consumption and take top 6
    std::sort(list.begin(), list.end(), [](const Consumer& a, const Consumer& b) { return a.kWh > b.kWh; });
    day.topConsumers.assign(list.begin(), list.begin() + 6);
    return day;
}

// Helper to format date as dd.mm.yyyy
std::wstring formatDate(const SYSTEMTIME& st) {
    std::wostringstream oss;
    oss << std::setfill(L'0') << std::setw(2) << st.wDay << L"." << std::setw(2) << st.wMonth << L"." << st.wYear;
    return oss.str();
}

// Global pointer to hold the simulated data
static EnergyDay g_day;

// Forward declarations of drawing functions
void DrawHeader(HDC hdc, RECT& area);
void DrawLineChart(HDC hdc, RECT& area);
void DrawBarChart(HDC hdc, RECT& area);
void DrawPieChart(HDC hdc, RECT& area);
void DrawTable(HDC hdc, RECT& area);
void DrawChecklist(HDC hdc, RECT& area);

// Forward declaration for printing stub
void PrintReport();

// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        // initialize the data once
        g_day = simulateEnergyDay();
        // create a print button
        CreateWindowEx(0, WC_BUTTON, L"Print Report", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            10, 10, 100, 30, hwnd, (HMENU)1, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {
            PrintReport();
        }
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client;
        GetClientRect(hwnd, &client);
        // define areas for each section (header + 5 charts) separated vertically with margins
        const int margin = 10;
        // header height fixed
        RECT headerArea = { client.left + margin, client.top + margin + 40, client.right - margin, client.top + margin + 40 + 190 };
        RECT lineArea   = { client.left + margin, headerArea.bottom + margin, client.right - margin, headerArea.bottom + margin + 260 };
        RECT barArea    = { client.left + margin, lineArea.bottom + margin, client.right - margin, lineArea.bottom + margin + 250 };
        RECT pieArea    = { client.left + margin, barArea.bottom + margin, client.right - margin, barArea.bottom + margin + 300 };
        RECT tableArea  = { client.left + margin, pieArea.bottom + margin, client.right - margin, pieArea.bottom + margin + 320 };
        RECT checkArea  = { client.left + margin, tableArea.bottom + margin, client.right - margin, tableArea.bottom + margin + 240 };
        // draw background
        FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
        // call draw functions
        DrawHeader(hdc, headerArea);
        DrawLineChart(hdc, lineArea);
        DrawBarChart(hdc, barArea);
        DrawPieChart(hdc, pieArea);
        DrawTable(hdc, tableArea);
        DrawChecklist(hdc, checkArea);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        // request repaint on resize
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// Helper for drawing text
void DrawTextW(HDC hdc, int x, int y, const std::wstring& text, int fontSize = 14, bool bold = false) {
    HFONT hFont = CreateFontW(-MulDiv(fontSize, GetDeviceCaps(hdc, LOGPIXELSY), 72), 0, 0, 0,
        bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT old = (HFONT)SelectObject(hdc, hFont);
    TextOutW(hdc, x, y, text.c_str(), (int)text.size());
    SelectObject(hdc, old);
    DeleteObject(hFont);
}

// Draw header section: title, building, date and summary box
void DrawHeader(HDC hdc, RECT& area) {
    // background white
    HBRUSH white = (HBRUSH)GetStockObject(WHITE_BRUSH);
    FillRect(hdc, &area, white);
    // Title
    DrawTextW(hdc, area.left + 10, area.top + 10, L"Denní energetický report", 18, true);
    // horizontal line
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0,0,0));
    HPEN old = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, area.left + 10, area.top + 36, NULL);
    LineTo(hdc, area.right - 10, area.top + 36);
    SelectObject(hdc, old);
    DeleteObject(pen);
    // Building and date
    DrawTextW(hdc, area.left + 10, area.top + 50, std::wstring(g_day.buildingName.begin(), g_day.buildingName.end()), 14, true);
    std::wstring dateStr = L"Datum: " + formatDate(g_day.date);
    DrawTextW(hdc, area.left + 10, area.top + 72, dateStr, 12);
    // summary box
    RECT box = { area.left + 10, area.top + 96, area.right - 10, area.top + 96 + 78 };
    Rectangle(hdc, box.left, box.top, box.right, box.bottom);
    // compute stats
    double total = 0.0;
    double peak = 0.0;
    int peakHour = 0;
    for (size_t i = 0; i < g_day.hourlyKWh.size(); ++i) {
        total += g_day.hourlyKWh[i];
        if (g_day.hourlyKWh[i] > peak) {
            peak = g_day.hourlyKWh[i];
            peakHour = (int)i;
        }
    }
    double cost = total * g_day.priceCZKPerKWh;
    std::wostringstream oss;
    oss << std::fixed << std::setprecision(1) << total;
    std::wstring totalStr = L"Celkem: " + oss.str() + L" kWh";
    oss.str(L"");
    oss << std::fixed << std::setprecision(0) << cost;
    std::wstring costStr = L"Odhad nákladů: " + oss.str() + L" Kč (";
    oss.str(L"");
    oss << std::fixed << std::setprecision(2) << g_day.priceCZKPerKWh;
    costStr += oss.str() + L" Kč/kWh)";
    oss.str(L"");
    oss << std::fixed << std::setprecision(1) << peak;
    std::wstring peakStr = L"Špička: " + oss.str() + L" kWh @ " + (peakHour < 10 ? L"0" : L"") + std::to_wstring(peakHour) + L":00";
    DrawTextW(hdc, box.left + 10, box.top + 10, totalStr, 14, true);
    DrawTextW(hdc, box.left + 10, box.top + 30, costStr, 12);
    DrawTextW(hdc, box.left + 10, box.top + 50, peakStr, 12);
}

// Draw line chart section
void DrawLineChart(HDC hdc, RECT& area) {
    FillRect(hdc, &area, (HBRUSH)GetStockObject(WHITE_BRUSH));
    DrawTextW(hdc, area.left + 10, area.top + 10, L"Časová osa (kWh/h)", 18, true);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0,0,0));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, area.left + 10, area.top + 36, NULL);
    LineTo(hdc, area.right - 10, area.top + 36);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    // plot area
    RECT plot;
    plot.left = area.left + 36;
    plot.top = area.top + 60;
    plot.right = area.right - 16;
    plot.bottom = area.top + 210;
    // bounding box
    Rectangle(hdc, plot.left, plot.top, plot.right, plot.bottom);
    double maxV = 10.0;
    for (double v : g_day.hourlyKWh) if (v > maxV) maxV = v;
    double yMax = ceil(maxV / 5.0) * 5.0;
    // horizontal grid and labels
    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(200,200,200));
    oldPen = (HPEN)SelectObject(hdc, gridPen);
    SetBkMode(hdc, TRANSPARENT);
    for (int i = 0; i <= 5; ++i) {
        int y = plot.top + (plot.bottom - plot.top) * i / 5;
        MoveToEx(hdc, plot.left, y, NULL);
        LineTo(hdc, plot.right, y);
        double val = yMax * (1.0 - (double)i / 5.0);
        std::wostringstream oss;
        oss << std::fixed << std::setprecision(0) << val;
        std::wstring txt = oss.str();
        TextOutW(hdc, plot.left - 8 - (int)(txt.size()*7), y - 6, txt.c_str(), (int)txt.size());
    }
    SelectObject(hdc, oldPen);
    DeleteObject(gridPen);
    // x ticks
    int ticks[] = {0,6,12,18,23};
    for (int t : ticks) {
        int x = plot.left + (plot.right - plot.left) * t / 23;
        MoveToEx(hdc, x, plot.bottom, NULL);
        LineTo(hdc, x, plot.bottom + 4);
        std::wstring txt;
        txt = (t < 10 ? L"0" : L"") + std::to_wstring(t);
        TextOutW(hdc, x - 8, plot.bottom + 6, txt.c_str(), (int)txt.size());
    }
    // line
    HPEN linePen = CreatePen(PS_SOLID, 2, RGB(0,0,0));
    oldPen = (HPEN)SelectObject(hdc, linePen);
    for (int h = 0; h < (int)g_day.hourlyKWh.size(); ++h) {
        double val = g_day.hourlyKWh[h];
        int x = plot.left + (plot.right - plot.left) * h / 23;
        int y = plot.top + (int)((plot.bottom - plot.top) * (1.0 - val / yMax));
        if (h == 0) MoveToEx(hdc, x, y, NULL);
        else LineTo(hdc, x, y);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(linePen);
    // peak marker
    double peak = 0;
    int peakHour = 0;
    for (int i = 0; i < (int)g_day.hourlyKWh.size(); ++i) {
        if (g_day.hourlyKWh[i] > peak) {
            peak = g_day.hourlyKWh[i];
            peakHour = i;
        }
    }
    int px = plot.left + (plot.right - plot.left) * peakHour / 23;
    int py = plot.top + (int)((plot.bottom - plot.top) * (1.0 - peak / yMax));
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, black);
    Ellipse(hdc, px - 3, py - 3, px + 3, py + 3);
    SelectObject(hdc, oldBrush);
    std::wostringstream oss;
    oss << L"peak " << std::fixed << std::setprecision(1) << peak;
    std::wstring peakTxt = oss.str();
    TextOutW(hdc, px + 6, py - 10, peakTxt.c_str(), (int)peakTxt.size());
}

// Draw bar chart section
void DrawBarChart(HDC hdc, RECT& area) {
    FillRect(hdc, &area, (HBRUSH)GetStockObject(WHITE_BRUSH));
    DrawTextW(hdc, area.left + 10, area.top + 10, L"Top spotřebiče (kWh/den)", 18, true);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0,0,0));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, area.left + 10, area.top + 36, NULL);
    LineTo(hdc, area.right - 10, area.top + 36);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    const int barAreaYStart = area.top + 60;
    double maxV = 1.0;
    for (const auto& c : g_day.topConsumers) if (c.kWh > maxV) maxV = c.kWh;
    int leftLabelX = area.left + 10;
    int barX = area.left + 170;
    int barW = area.right - barX - 10;
    int y = barAreaYStart;
    for (size_t i = 0; i < g_day.topConsumers.size(); ++i) {
        const Consumer& it = g_day.topConsumers[i];
        // name
        std::wstring wname(it.name.begin(), it.name.end());
        DrawTextW(hdc, leftLabelX, y + 2, wname, 12);
        // outline
        Rectangle(hdc, barX, y, barX + barW, y + 18);
        // fill bar proportionally
        double frac = it.kWh / maxV;
        int w = (int)(barW * frac);
        HBRUSH brush = (HBRUSH)GetStockObject(BLACK_BRUSH);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
        RECT fillRect = { barX, y, barX + w, y + 18 };
        FillRect(hdc, &fillRect, brush);
        SelectObject(hdc, oldBrush);
        // value at right
        std::wostringstream oss;
        oss << std::fixed << std::setprecision(1) << it.kWh;
        std::wstring valStr = oss.str();
        SIZE size;
        GetTextExtentPoint32W(hdc, valStr.c_str(), (int)valStr.size(), &size);
        TextOutW(hdc, area.right - 10 - size.cx, y + 2, valStr.c_str(), (int)valStr.size());
        // separator line
        if (i < g_day.topConsumers.size() - 1) {
            HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(210,210,210));
            HPEN old = (HPEN)SelectObject(hdc, sepPen);
            MoveToEx(hdc, area.left + 10, y + 26, NULL);
            LineTo(hdc, area.right - 10, y + 26);
            SelectObject(hdc, old);
            DeleteObject(sepPen);
        }
        y += 28;
    }
}

// Draw pie chart section
void DrawPieChart(HDC hdc, RECT& area) {
    FillRect(hdc, &area, (HBRUSH)GetStockObject(WHITE_BRUSH));
    DrawTextW(hdc, area.left + 10, area.top + 10, L"Rozpad kategorií (podíl)", 18, true);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0,0,0));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, area.left + 10, area.top + 36, NULL);
    LineTo(hdc, area.right - 10, area.top + 36);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    // compute total
    double total = 0.0;
    for (const auto& c : g_day.categoryBreakdown) total += c.kWh;
    int cx = area.left + 100;
    int cy = area.top + 170;
    int radius = 70;
    double startAngle = -3.14159265358979323846 / 2; // -90 deg
    // prepare hatch brushes for patterns
    HBRUSH patterns[4];
    patterns[0] = CreateHatchBrush(HS_FDIAGONAL, RGB(0,0,0));
    patterns[1] = CreateHatchBrush(HS_BDIAGONAL, RGB(0,0,0));
    patterns[2] = CreateHatchBrush(HS_HORIZONTAL, RGB(0,0,0));
    patterns[3] = CreateHatchBrush(HS_VERTICAL, RGB(0,0,0));
    // draw slices
    double currentAngle = startAngle;
    for (size_t i = 0; i < g_day.categoryBreakdown.size(); ++i) {
        const auto& cat = g_day.categoryBreakdown[i];
        double frac = cat.kWh / total;
        double endAngle = currentAngle + frac * 2 * 3.14159265358979323846;
        // select pattern
        HBRUSH hatch = patterns[i % 4];
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, hatch);
        HPEN slicePen = CreatePen(PS_SOLID, 1, RGB(0,0,0));
        HPEN oldP = (HPEN)SelectObject(hdc, slicePen);
        // draw pie slice using Pie function
        Pie(hdc, cx - radius, cy - radius, cx + radius, cy + radius,
            cx + (int)(radius * cos(currentAngle)), cy + (int)(radius * sin(currentAngle)),
            cx + (int)(radius * cos(endAngle)), cy + (int)(radius * sin(endAngle)));
        SelectObject(hdc, oldP);
        DeleteObject(slicePen);
        SelectObject(hdc, oldBrush);
        currentAngle = endAngle;
    }
    // legend
    int legendY = area.top + 90;
    for (size_t i = 0; i < g_day.categoryBreakdown.size(); ++i) {
        const auto& cat = g_day.categoryBreakdown[i];
        double pct = 100.0 * cat.kWh / total;
        // square with pattern number
        Rectangle(hdc, area.left + 200, legendY, area.left + 212, legendY + 12);
        std::wstring idx = std::to_wstring(i + 1);
        TextOutW(hdc, area.left + 202, legendY - 1, idx.c_str(), (int)idx.size());
        // text
        std::wostringstream oss;
        oss << (i + 1) << L") " << std::wstring(cat.name.begin(), cat.name.end()) << L"  " << std::fixed << std::setprecision(0) << pct << L"%";
        std::wstring line = oss.str();
        TextOutW(hdc, area.left + 218, legendY - 1, line.c_str(), (int)line.size());
        legendY += 22;
    }
    TextOutW(hdc, area.left + 200, legendY + 4, L"Pozn.: vzory = index 1..N", 25);
    // cleanup pattern brushes
    for (int i = 0; i < 4; ++i) DeleteObject(patterns[i]);
}

// Draw table section
void DrawTable(HDC hdc, RECT& area) {
    FillRect(hdc, &area, (HBRUSH)GetStockObject(WHITE_BRUSH));
    DrawTextW(hdc, area.left + 10, area.top + 10, L"Tabulka (výběr hodin)", 18, true);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0,0,0));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, area.left + 10, area.top + 36, NULL);
    LineTo(hdc, area.right - 10, area.top + 36);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    double total = 0.0;
    for (double v : g_day.hourlyKWh) total += v;
    double avg = total / 24.0;
    std::wostringstream oss;
    oss << L"Průměr: " << std::fixed << std::setprecision(1) << avg << L" kWh/h   Cena: " << std::fixed << std::setprecision(2) << g_day.priceCZKPerKWh << L" Kč/kWh";
    DrawTextW(hdc, area.left + 10, area.top + 50, oss.str(), 12);
    // top 10 hours
    struct Row { int hour; double v; };
    std::vector<Row> rows;
    for (int i = 0; i < (int)g_day.hourlyKWh.size(); ++i) rows.push_back({ i, g_day.hourlyKWh[i] });
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.v > b.v; });
    if (rows.size() > 10) rows.resize(10);
    // header columns
    int colX[4] = { area.left + 10, area.left + 80, area.left + 160, area.left + 280 };
    DrawTextW(hdc, colX[0], area.top + 78, L"Hod", 12);
    DrawTextW(hdc, colX[1], area.top + 78, L"kWh", 12);
    DrawTextW(hdc, colX[2], area.top + 78, L"Kč", 12);
    DrawTextW(hdc, colX[3], area.top + 78, L"Pozn.", 12);
    // header underline
    pen = CreatePen(PS_SOLID, 1, RGB(0,0,0));
    oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, area.left + 10, area.top + 82, NULL);
    LineTo(hdc, area.right - 10, area.top + 82);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    // rows
    int y = area.top + 90;
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        std::wstring hourStr = (r.hour < 10 ? L"0" : L"") + std::to_wstring(r.hour) + L":00";
        oss.str(L"");
        oss << std::fixed << std::setprecision(1) << r.v;
        std::wstring kWhStr = oss.str();
        oss.str(L"");
        oss << std::fixed << std::setprecision(0) << (r.v * g_day.priceCZKPerKWh);
        std::wstring costStr = oss.str();
        std::wstring noteStr = (r.v > avg * 1.5 ? L"peak" : L"");
        DrawTextW(hdc, colX[0], y, hourStr, 12);
        DrawTextW(hdc, colX[1], y, kWhStr, 12);
        DrawTextW(hdc, colX[2], y, costStr, 12);
        DrawTextW(hdc, colX[3], y, noteStr, 12);
        if (i < rows.size() - 1) {
            HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(220,220,220));
            HPEN old = (HPEN)SelectObject(hdc, sepPen);
            MoveToEx(hdc, area.left + 10, y + 18, NULL);
            LineTo(hdc, area.right - 10, y + 18);
            SelectObject(hdc, old);
            DeleteObject(sepPen);
        }
        y += 20;
    }
    DrawTextW(hdc, area.left + 10, area.bottom - 20, L"Tip: nejvyšší hodiny často souvisí s HVAC/EV.", 12);
}

// Draw checklist section
void DrawChecklist(HDC hdc, RECT& area) {
    FillRect(hdc, &area, (HBRUSH)GetStockObject(WHITE_BRUSH));
    DrawTextW(hdc, area.left + 10, area.top + 10, L"Checklist / Alerts", 18, true);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0,0,0));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, area.left + 10, area.top + 36, NULL);
    LineTo(hdc, area.right - 10, area.top + 36);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    double total = 0.0;
    for (double v : g_day.hourlyKWh) total += v;
    double avg = total / 24.0;
    double peak = 0.0;
    for (double v : g_day.hourlyKWh) if (v > peak) peak = v;
    double nightAvg = 0.0;
    for (int i = 0; i < 6 && i < (int)g_day.hourlyKWh.size(); ++i) nightAvg += g_day.hourlyKWh[i];
    nightAvg /= 6.0;
    bool nightHigh = nightAvg > avg * 0.75;
    struct Alert { std::wstring text; bool ok; };
    std::vector<Alert> alerts = {
        { L"Noční zátěž v normě", !nightHigh },
        { L"Žádná extrémní špička (> 2.0× průměr)", !(peak > avg * 2.0) },
        { L"Křivka bez výpadků (24/24)", g_day.hourlyKWh.size() == 24 },
        { L"Doporučení: zkontrolovat HVAC plán", true },
        { L"Doporučení: audit osvětlení (zóny)", true }
    };
    int y = area.top + 60;
    for (const auto& a : alerts) {
        // draw box
        Rectangle(hdc, area.left + 10, y + 2, area.left + 22, y + 14);
        if (a.ok) {
            HPEN okPen = CreatePen(PS_SOLID, 2, RGB(0,0,0));
            HPEN old = (HPEN)SelectObject(hdc, okPen);
            MoveToEx(hdc, area.left + 12, y + 9, NULL);
            LineTo(hdc, area.left + 15, y + 13);
            LineTo(hdc, area.left + 21, y + 3);
            SelectObject(hdc, old);
            DeleteObject(okPen);
        } else {
            HPEN crossPen = CreatePen(PS_SOLID, 2, RGB(0,0,0));
            HPEN old = (HPEN)SelectObject(hdc, crossPen);
            MoveToEx(hdc, area.left + 12, y + 3, NULL);
            LineTo(hdc, area.left + 21, y + 13);
            MoveToEx(hdc, area.left + 21, y + 3, NULL);
            LineTo(hdc, area.left + 12, y + 13);
            SelectObject(hdc, old);
            DeleteObject(crossPen);
        }
        // text
        DrawTextW(hdc, area.left + 30, y + 2, a.text, 13, !a.ok);
        y += 26;
    }
}

// Stub: generate ESC/POS commands for the report and write to a file
void PrintReport() {
    MessageBox(NULL, L"Print functionality is not fully implemented in this demo.\nThis stub would send ESC/POS commands to your printer.", L"Print", MB_OK | MB_ICONINFORMATION);
    // You could generate the ESC/POS data here by rasterizing each chart to a monochrome bitmap
    // and writing the ESC/POS commands to a file or serial port. See the JS version for reference.
}

// Main entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, PWSTR pCmdLine, int nCmdShow) {
    // Initialize common controls (for button styles)
    INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    const wchar_t CLASS_NAME[] = L"EnergyReportWindow";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClass(&wc);
    // Create window
    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"Energetický report",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        600, 1100,
        NULL,
        NULL,
        hInstance,
        NULL
    );
    if (!hwnd) return 0;
    ShowWindow(hwnd, nCmdShow);
    // message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}