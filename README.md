# ⚡ Energy Report GUI - Windows 10 BLE Thermal Printer Integration

A **self-contained C++ WinAPI application** that generates beautiful daily energy consumption reports with multiple chart types and thermal printer output support.

## 🎯 Features

- **Multi-Chart Visualization** - Display energy data in 5 different ways:
  - 📈 **Line Chart** - Hourly consumption timeline with peak detection
  - 📊 **Bar Chart** - Top 6 power consumers ranked by consumption
  - 🥧 **Pie Chart** - Category breakdown with patterned slices
  - 📋 **Data Table** - Top 10 hours with costs and annotations
  - ✅ **Checklist/Alerts** - Energy health indicators & recommendations

- **Realistic Simulation** - Generates reproducible daily energy data:
  - 24-hour consumption curves with natural patterns
  - Night, work, and evening load profiles
  - HVAC, lighting, IT, and EV charging distributions
  - Random noise and demand spikes

- **Thermal Printer Ready** - ESC/POS command generation (stub):
  - Framework for Bluetooth LE printer integration
  - Easy to extend for actual device printing

- **Czech Localization** - All UI text in Czech with support for CZK pricing

## 🏗️ Architecture

### Core Components

| Component | Purpose |
|-----------|---------|
| `SeededRNG` | Linear congruential generator for reproducible randomness |
| `EnergyDay` | Data structure holding hourly consumption, consumers, categories |
| `simulateEnergyDay()` | Generates realistic daily energy patterns |
| Drawing Functions | WinAPI rendering for each chart type |
| `PrintReport()` | ESC/POS stub for thermal printer output |

### Data Structure

```cpp
struct EnergyDay {
    std::string buildingName;          // Building identifier
    SYSTEMTIME date;                   // Report date
    std::vector<double> hourlyKWh;     // 24 hourly readings
    std::vector<Consumer> topConsumers; // Top 6 power users
    std::vector<Category> categoryBreakdown; // 5 energy categories
    double priceCZKPerKWh;             // Energy pricing
};
