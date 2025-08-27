# Parking Lot (C++)

> OOP + Design Patterns (Factory, Strategy, Singleton) with unit tests and a simple CLI.

![Build](https://github.com/anilkruz/ParkingLot/actions/workflows/build.yml/badge.svg) ![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)

## Overview

A clean, production‑style implementation of a Parking Lot in modern C++ (C++17). The goal is to demonstrate solid OOP design, clear separation of concerns, and testability. The system supports multiple floors, typed slots, ticketing, fee calculation strategies, and simple reporting.

## Features

* Multiple floors with typed slots (TwoWheeler, FourWheeler, Heavy).
* Vehicle in/out with **Ticket** generation.
* **Fee Strategy**: interchangeable pricing rules (per hour, slab, weekend/holiday, etc.).
* **Factory** to create vehicles/slots from type enums.
* **Singleton** for central `ParkingLot` registry or `Clock` (pluggable time source for tests).
* Basic reporting: free/occupied counts per floor/type.
* JSON sample layout for quick bootstrapping (optional).

## High‑Level Design

* **Domain**: `Vehicle`, `Ticket`, `Slot`, `Floor`, `ParkingLot`.
* **Factories**: `VehicleFactory`, `SlotFactory`.
* **Strategy**: `IFeeStrategy` → `PerHourFee`, `SlabFee`, etc.
* **Services**: `TicketingService`, `FeeService`.
* **Infra**: `Clock` (prod/test), `IdGenerator`.

```text
+------------------+       +------------------+
|  ParkingLot      |<>-----|  Floor           |
|  - floors[]      |       |  - slots[]       |
+------------------+       +------------------+
         ^                            ^
         | uses                       | uses
         |                            |
+------------------+        +------------------+      +------------------+
| TicketingService |------->|  Slot            |<-----|  SlotFactory     |
| FeeService       |        |  - type         |      +------------------+
+------------------+        |  - occupied     |
                            +------------------+
             ^                            ^
             | creates                    | holds
             |                             
+------------------+        +------------------+      +------------------+
| VehicleFactory   |------->|  Vehicle         |      | IFeeStrategy     |
+------------------+        +------------------+      +------------------+
                                                     ^        ^
                                                     |        |
                                              +------------+ +-------------+
                                              | PerHourFee | |  SlabFee    |
                                              +------------+ +-------------+
```

## Directory Layout

```
ParkingLot/
├─ src/
│  ├─ main.cpp
│  ├─ parking_lot.cpp
│  ├─ ticketing_service.cpp
│  ├─ fee_strategies.cpp
│  ├─ factories.cpp
│  └─ ...
├─ include/
│  ├─ parking_lot.hpp
│  ├─ ticketing_service.hpp
│  ├─ fee_strategies.hpp
│  ├─ factories.hpp
│  └─ types.hpp
├─ tests/
│  ├─ ticket_tests.cpp
│  └─ fee_tests.cpp
├─ data/
│  └─ layout.json   # optional sample layout
├─ CMakeLists.txt
└─ README.md
```

## Build & Run

### Prerequisites

* C++17 compiler (g++/clang++)
* CMake ≥ 3.16 (recommended)
* (Optional) GoogleTest for unit tests

### Build (CMake)

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

### Run

```bash
./parking_lot \
  --init data/layout.json \
  --strategy per_hour \
  --in CAR UP80HM8086 \
  --out UP80HM8086
```

### Quick Compile (single file demo)

```bash
g++ -std=cpp17 -O2 -Iinclude src/main.cpp src/*.cpp -o parking_lot
```

## Sample JSON Layout (optional)

```json
{
  "floors": [
    {
      "floorNo": 1,
      "slots": [
        { "id": "F1-S1", "type": "TwoWheeler" },
        { "id": "F1-S2", "type": "TwoWheeler" },
        { "id": "F1-S3", "type": "FourWheeler" },
        { "id": "F1-S4", "type": "FourWheeler" },
        { "id": "F1-S5", "type": "FourWheeler" }
      ]
    },
    {
      "floorNo": 2,
      "slots": [
        { "id": "F2-S1", "type": "TwoWheeler" },
        { "id": "F2-S2", "type": "FourWheeler" },
        { "id": "F2-S3", "type": "FourWheeler" },
        { "id": "F2-S4", "type": "Heavy" },
        { "id": "F2-S5", "type": "Heavy" }
      ]
    }
  ]
}
```

## CLI Examples

```bash
# Park a car
./parking_lot --in CAR HR26AB1234

# Park a bike with explicit floor preference
./parking_lot --in BIKE DL5S9999 --floor 1

# Exit and pay
./parking_lot --out HR26AB1234

# Show availability
./parking_lot --status
```

## Fee Strategy

* **PerHourFee**: `fee = base + rate_per_hour * ceil(duration_hours)`
* **SlabFee**: define slabs like `[0–2h]=₹50`, `[2–6h]=₹150`, `>6h = ₹150 + ₹30/h`.
* Strategy can be selected via CLI flag or config.

## Testing

```bash
# build with tests
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## How to Extend

* Add a new vehicle type → update `VehicleType` enum + `VehicleFactory`.
* Add a new fee policy → implement `IFeeStrategy` and wire through CLI/config.
* Plug custom time source in tests → inject `Clock` into services.

## Performance Notes

* Use `std::vector` for contiguous slot storage and O(1) index lookups.
* Keep `Slot` as a lightweight struct; avoid polymorphism per-slot.
* Use `std::optional<size_t>` for free-slot index discovery.
* Consider `std::unordered_map<Plate, Ticket>` for O(1) active tickets.

## Linting & CI

* `clang-tidy`, `cppcheck` targets provided via CMake options.
* GitHub Actions workflow at `.github/workflows/build.yml` builds, runs tests, and publishes the badge.

## License

MIT

---

### Quick Start for Contributors

1. Fork → Clone → Create feature branch
2. Commit with conventional messages (e.g., `feat(ticket): add weekend discount`)
3. Open PR with before/after behavior in the description

