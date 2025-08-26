#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <chrono>
#include <atomic>
#include <stdexcept>
#include <fstream>
#include <mutex>
#include <optional>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
using namespace std;

// ===================== Common =====================
using TicketId = unsigned long long;
using BillId   = unsigned long long;

enum class VehicleType { Bike, Car, Truck };
enum class SlotType    { TwoWheeler, FourWheeler, Heavy };

// ---- Vehicle ----
struct Vehicle {
    string regNo;
    VehicleType type;
    explicit Vehicle(string r, VehicleType t) : regNo(std::move(r)), type(t) {}
};
struct Bike  : Vehicle { using Vehicle::Vehicle; };
struct Car   : Vehicle { using Vehicle::Vehicle; };
struct Truck : Vehicle { using Vehicle::Vehicle; };

static SlotType slotFor(VehicleType t) {
    switch (t) {
        case VehicleType::Bike:  return SlotType::TwoWheeler;
        case VehicleType::Car:   return SlotType::FourWheeler;
        case VehicleType::Truck: return SlotType::Heavy;
    }
    return SlotType::FourWheeler;
}

// ---- Core model ----
struct ParkingSlot {
    string id;
    SlotType type;
    bool isFree = true;
};

struct Floor {
    int floorNo = 0;
    vector<ParkingSlot> slots;

    // not thread-safe alone; caller must hold lot mutex
    int findFreeIndex(SlotType t) {
        for (int i = 0; i < (int)slots.size(); ++i)
            if (slots[i].type == t && slots[i].isFree) return i;
        return -1;
    }
};

struct Ticket {
    TicketId id = 0;
    string entryGateId;
    std::chrono::system_clock::time_point inTime;
    string slotId;
    VehicleType vtype;
    SlotType stype;
    string vehicleReg;
};

struct TicketingService {
    std::atomic<TicketId> nextId{1};
    Ticket openTicket(const string& gate, const ParkingSlot& slot, const Vehicle& v) {
        Ticket tk;
        tk.id = nextId.fetch_add(1, std::memory_order_relaxed);
        tk.entryGateId = gate;
        tk.inTime = std::chrono::system_clock::now();
        tk.slotId = slot.id;
        tk.vtype = v.type;
        tk.stype = slot.type;
        tk.vehicleReg = v.regNo;
        return tk;
    }
};

// ---------- Pricing (Strategy from Stage 3) ----------
struct FeeBreakup {
    unsigned long long amount = 0;   // INR
    unsigned long long billedHours = 0;
    unsigned long long parkedMinutes = 0;
};

struct IFeeStrategy {
    virtual ~IFeeStrategy() = default;
    virtual FeeBreakup compute(unsigned long long parkedMinutes) const = 0;
protected:
    static unsigned long long ceilHours(unsigned long long minutes) {
        if (minutes == 0) return 0;
        return (minutes + 59) / 60;
    }
};

static constexpr unsigned long long GRACE_MINUTES = 10; // Stage 5 add-on

struct TwoWheelerFee final : IFeeStrategy {
    FeeBreakup compute(unsigned long long minutes) const override {
        FeeBreakup r; r.parkedMinutes = minutes;
        if (minutes <= GRACE_MINUTES) { r.billedHours = 0; r.amount = 0; return r; }
        auto hours = ceilHours(minutes);
        r.billedHours = hours;
        r.amount = hours * 10ULL;
        return r;
    }
};
struct FourWheelerFee final : IFeeStrategy {
    FeeBreakup compute(unsigned long long minutes) const override {
        FeeBreakup r; r.parkedMinutes = minutes;
        if (minutes <= GRACE_MINUTES) { r.billedHours = 0; r.amount = 0; return r; }
        auto hours = ceilHours(minutes);
        r.billedHours = hours;
        r.amount = hours * 20ULL;
        return r;
    }
};
struct HeavyFee final : IFeeStrategy {
    FeeBreakup compute(unsigned long long minutes) const override {
        FeeBreakup r; r.parkedMinutes = minutes;
        if (minutes <= GRACE_MINUTES) { r.billedHours = 0; r.amount = 0; return r; }
        auto hours = ceilHours(minutes);
        r.billedHours = hours;
        r.amount = hours * 50ULL;
        return r;
    }
};

struct FeeStrategyFactory {
    static unique_ptr<IFeeStrategy> make(SlotType s) {
        switch (s) {
            case SlotType::TwoWheeler:  return make_unique<TwoWheelerFee>();
            case SlotType::FourWheeler: return make_unique<FourWheelerFee>();
            case SlotType::Heavy:       return make_unique<HeavyFee>();
        }
        throw runtime_error("Unknown SlotType for fee strategy");
    }
};

// ---- Billing (Stage 4) ----
enum class BillStatus { Pending, Paid, Failed, Cancelled };

struct Bill {
    BillId id{};
    TicketId ticket{};
    string vehicleReg;
    string slotId;
    string entryGateId;
    string exitGateId;
    std::chrono::system_clock::time_point inTime;
    std::chrono::system_clock::time_point outTime;
    unsigned long long parkedMinutes{};
    unsigned long long billedHours{};
    unsigned long long amount{}; // INR
    BillStatus status{BillStatus::Pending};
};

// ---- Receipt (after payment) ----
struct Receipt {
    BillId bill{};
    TicketId ticket{};
    unsigned long long amount{};
    string method;
    std::chrono::system_clock::time_point paidAt;
};

// ---- Payment interfaces ----
enum class PaymentMethod { Cash, Card, UPI };

struct PaymentRequest {
    BillId bill{};
    unsigned long long amount{};
    PaymentMethod method{};
    // Optional data
    string cardNumber; // masked/last4 in real code
    string upiVPA;     // e.g., "user@bank"
};

struct IPaymentProcessor {
    virtual ~IPaymentProcessor() = default;
    virtual bool charge(const PaymentRequest& req, string& failureReason) = 0;
    virtual const char* name() const = 0;
};

struct CashProcessor : IPaymentProcessor {
    bool charge(const PaymentRequest& /*req*/, string& /*failureReason*/) override {
        return true; // always succeeds
    }
    const char* name() const override { return "Cash"; }
};
struct CardProcessor : IPaymentProcessor {
    // super-simplified: accept if card length >= 8
    bool charge(const PaymentRequest& req, string& failureReason) override {
        if (req.cardNumber.size() < 8) {
            failureReason = "Card declined (invalid number)";
            return false;
        }
        return true;
    }
    const char* name() const override { return "Card"; }
};
struct UPIProcessor : IPaymentProcessor {
    // super-simplified: accept if contains '@'
    bool charge(const PaymentRequest& req, string& failureReason) override {
        if (req.upiVPA.find('@') == string::npos) {
            failureReason = "UPI failed (invalid VPA)";
            return false;
        }
        return true;
    }
    const char* name() const override { return "UPI"; }
};

static unique_ptr<IPaymentProcessor> makeProcessor(PaymentMethod m) {
    switch (m) {
        case PaymentMethod::Cash: return make_unique<CashProcessor>();
        case PaymentMethod::Card: return make_unique<CardProcessor>();
        case PaymentMethod::UPI:  return make_unique<UPIProcessor>();
    }
    throw runtime_error("Unknown PaymentMethod");
}

// ---- Services ----
class PaymentService {
    unordered_map<BillId, Bill> bills_;
    std::atomic<BillId> nextBill_{1};
    mutable std::mutex mu_; // guards bills_

public:
    Bill createBill(const Ticket& tk,
                    const string& exitGate,
                    const FeeBreakup& fb) {
        Bill b;
        b.id = nextBill_.fetch_add(1, std::memory_order_relaxed);
        b.ticket = tk.id;
        b.vehicleReg = tk.vehicleReg;
        b.slotId = tk.slotId;
        b.entryGateId = tk.entryGateId;
        b.exitGateId = exitGate;
        b.inTime = tk.inTime;
        b.outTime = std::chrono::system_clock::now();
        b.parkedMinutes = fb.parkedMinutes;
        b.billedHours = fb.billedHours;
        b.amount = fb.amount;
        b.status = BillStatus::Pending;

        std::lock_guard<std::mutex> lk(mu_);
        bills_.emplace(b.id, b);
        return b;
    }

    optional<Bill> get(BillId id) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = bills_.find(id);
        if (it == bills_.end()) return nullopt;
        return it->second;
    }

    Receipt pay(const PaymentRequest& req) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = bills_.find(req.bill);
        if (it == bills_.end()) throw runtime_error("Bill not found");
        Bill& b = it->second;

        if (b.status == BillStatus::Paid) {
            // idempotent: return a “paid” receipt again
            return Receipt{b.id, b.ticket, b.amount, "ALREADY_PAID", std::chrono::system_clock::now()};
        }
        if (b.status != BillStatus::Pending)
            throw runtime_error("Bill is not payable (status != Pending)");

        string reason;
        auto proc = makeProcessor(req.method);
        bool ok = proc->charge(req, reason);
        if (!ok) {
            b.status = BillStatus::Failed;
            throw runtime_error("Payment failed: " + reason);
        }

        b.status = BillStatus::Paid;
        return Receipt{b.id, b.ticket, b.amount, proc->name(), std::chrono::system_clock::now()};
    }

    void cancel(BillId id) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = bills_.find(id);
        if (it == bills_.end()) throw runtime_error("Bill not found");
        if (it->second.status == BillStatus::Paid)
            throw runtime_error("Cannot cancel a paid bill");
        it->second.status = BillStatus::Cancelled;
    }
       void reset() {
        std::lock_guard<std::mutex> lk(mu_);
        bills_.clear();
        nextBill_.store(1, std::memory_order_relaxed);
    }
};

class ParkingLot {
    vector<Floor> floors_;
    unordered_map<TicketId, Ticket> active_; // open tickets
    TicketingService ticketSvc_;
    PaymentService paymentSvc_;
    mutable std::mutex mu_; // Stage 5: coarse-grained safety

public:
    static ParkingLot& instance() { static ParkingLot inst; return inst; }
    ParkingLot() = default;  
    ParkingLot(const ParkingLot&) = delete;
    ParkingLot& operator=(const ParkingLot&) = delete;

    // ---------- Stage 1 ----------
void configure(vector<Floor> fs) {
    floors_ = std::move(fs);
    active_.clear();

    // TicketingService reset
    ticketSvc_.nextId.store(1, std::memory_order_relaxed);

    // PaymentService reset (helper function niche diya)
    paymentSvc_.reset();
}

    // ---------- Stage 2 ----------
    TicketId enterVehicle(const string& entryGate, Vehicle& v) {
        std::lock_guard<std::mutex> lk(mu_);
        SlotType need = slotFor(v.type);

        int chosenFloor = -1, idx = -1;
        for (int f = 0; f < (int)floors_.size(); ++f) {
            idx = floors_[f].findFreeIndex(need);
            if (idx != -1) { chosenFloor = f; break; }
        }
        if (chosenFloor == -1) throw runtime_error("No free slot available");

        ParkingSlot& slot = floors_[chosenFloor].slots[idx];
        slot.isFree = false;

        Ticket tk = ticketSvc_.openTicket(entryGate, slot, v);
        TicketId tid = tk.id;
        active_.emplace(tid, std::move(tk));
        return tid;
    }

    // ---------- Stage 3 (modified for Stage 4) ----------
    // exit -> compute fee -> create Bill (Pending) -> free slot
    Bill exitVehicle(TicketId tid, const string& exitGate,
                     bool lostTicket = false) {
        using namespace std::chrono;

        std::lock_guard<std::mutex> lk(mu_);
        auto it = active_.find(tid);
        if (it == active_.end())
            throw runtime_error("Invalid or already-closed ticket");

        Ticket tk = std::move(it->second);
        active_.erase(it);

        ParkingSlot* slotPtr = findSlotById_nolock(tk.slotId);
        if (!slotPtr)
            throw runtime_error("Slot referenced by ticket not found: " + tk.slotId);
        slotPtr->isFree = true;

        auto now = system_clock::now();
        auto mins = duration_cast<minutes>(now - tk.inTime).count();
        if (mins < 0) mins = 0;

        unique_ptr<IFeeStrategy> strategy = FeeStrategyFactory::make(tk.stype);
        FeeBreakup fb = strategy->compute(static_cast<unsigned long long>(mins));

        if (lostTicket) {
            // Stage 5 add-on: flat penalty on top (configurable)
            static constexpr unsigned long long LOST_TICKET_PENALTY = 200;
            fb.amount += LOST_TICKET_PENALTY;
        }

        // Create pending bill (Payment stage)
        Bill bill = paymentSvc_.createBill(tk, exitGate, fb);
        return bill;
    }

    // ---------- Stage 4 ----------
    Receipt payBill(const PaymentRequest& req) {
        // Payment service is internally locked, no lot-wide lock needed here.
        return paymentSvc_.pay(req);
    }

    // ---------- Utility ----------
    void adjustInTimeForTest(TicketId tid, long long minutesBack) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = active_.find(tid);
        if (it == active_.end()) throw runtime_error("Ticket not found for adjustInTime");
        it->second.inTime -= std::chrono::minutes(minutesBack);
    }

    void occupancy(int& freeCnt, int& usedCnt, int& total) const {
        std::lock_guard<std::mutex> lk(mu_);
        freeCnt = usedCnt = total = 0;
        for (const auto& f : floors_) {
            for (const auto& s : f.slots) {
                ++total;
                if (s.isFree) ++freeCnt; else ++usedCnt;
            }
        }
    }

    size_t activeCount() const {
        std::lock_guard<std::mutex> lk(mu_);
        return active_.size();
    }

private:
    ParkingSlot* findSlotById_nolock(const string& sid) {
        for (auto& f : floors_)
            for (auto& s : f.slots)
                if (s.id == sid) return &s;
        return nullptr;
    }
};

// ---------- JSON helpers ----------
static SlotType slotTypeFromString(const string& s) {
    if (s == "TwoWheeler")  return SlotType::TwoWheeler;
    if (s == "FourWheeler") return SlotType::FourWheeler;
    if (s == "Heavy")       return SlotType::Heavy;
    throw runtime_error("Invalid SlotType in config: " + s);
}
static const json& must(const json& j, const char* key) {
    if (!j.contains(key))
        throw runtime_error(string("Config missing key: ") + key);
    return j.at(key);
}
static vector<Floor> loadConfigFromJson(const string& path) {
    ifstream f(path);
    if (!f) throw runtime_error("Could not open config file: " + path);

    json j; f >> j;

    const auto& jfloors = must(j, "floors");
    if (!jfloors.is_array()) throw runtime_error("Config 'floors' must be an array");

    vector<Floor> fs; fs.reserve(jfloors.size());
    for (const auto& jf : jfloors) {
        Floor fl;
        fl.floorNo = must(jf, "floorNo").get<int>();

        const auto& jslots = must(jf, "slots");
        if (!jslots.is_array()) throw runtime_error("Config 'slots' must be an array for floor " + to_string(fl.floorNo));

        for (const auto& js : jslots) {
            ParkingSlot ps;
            ps.id   = must(js, "id").get<string>();
            ps.type = slotTypeFromString(must(js, "type").get<string>());
            ps.isFree = true;
            fl.slots.push_back(std::move(ps));
        }
        if (fl.slots.empty())
            throw runtime_error("Floor " + to_string(fl.floorNo) + " has no slots in config");

        fs.push_back(std::move(fl));
    }
    if (fs.empty()) throw runtime_error("Config has zero floors");
    return fs;
}

// ---------- Demo helpers ----------
static void printBill(const Bill& b) {
    using std::chrono::system_clock;
    auto tin  = system_clock::to_time_t(b.inTime);
    auto tout = system_clock::to_time_t(b.outTime);
    cout << "------ BILL ------\n";
    cout << "Bill: " << b.id << " | Ticket: " << b.ticket << "\n";
    cout << "Vehicle: " << b.vehicleReg << " | Slot: " << b.slotId << "\n";
    cout << "Entry: " << b.entryGateId << " | Exit: " << b.exitGateId << "\n";
    cout << "In : " << std::ctime(&tin);
    cout << "Out: " << std::ctime(&tout);
    cout << "Parked: " << b.parkedMinutes << " mins, Billed: " << b.billedHours << " hour(s)\n";
    cout << "Amount: INR " << b.amount << " | Status: "
         << (b.status==BillStatus::Pending ? "Pending" :
             b.status==BillStatus::Paid ? "Paid" :
             b.status==BillStatus::Failed ? "Failed" : "Cancelled")
         << "\n";
    cout << "------------------\n";
}
static void printReceipt(const Receipt& r) {
    auto t = std::chrono::system_clock::to_time_t(r.paidAt);
    cout << "==== RECEIPT ====\n";
    cout << "Bill: " << r.bill << " | Ticket: " << r.ticket << "\n";
    cout << "Amount: INR " << r.amount << " | Method: " << r.method << "\n";
    cout << "PaidAt: " << std::ctime(&t);
    cout << "=================\n";
}

int main() {
    try {
        // Bootstrap
        vector<Floor> fs = loadConfigFromJson("parking_config.json");
        auto& lot = ParkingLot::instance();
        lot.configure(std::move(fs));

        // Stage 2: entries
        Bike  b("UP80 HM 8086", VehicleType::Bike);
        Car   c("DL8CAF1234",   VehicleType::Car);

        auto tb = lot.enterVehicle("E1", b);
        auto tc = lot.enterVehicle("E2", c);

        // Simulate durations
        lot.adjustInTimeForTest(tb, 95); // 1h35m -> billed 2h for 2W
        lot.adjustInTimeForTest(tc, 7);  // 7m -> within grace -> ₹0

        int freeC, usedC, total;
        lot.occupancy(freeC, usedC, total);
        cout << "Before exit -> Active: " << lot.activeCount()
             << " | free/used/total: " << freeC << "/" << usedC << "/" << total << "\n";

        // Stage 3/4: exit -> bill (pending)
        Bill bb = lot.exitVehicle(tb, "X1");
        Bill bc = lot.exitVehicle(tc, "X2");

        printBill(bb);
        printBill(bc);

        // Stage 4: pay
        PaymentRequest pr1{bb.id, bb.amount, PaymentMethod::Card, "42424242", ""};
        auto r1 = lot.payBill(pr1);
        printReceipt(r1);

        // Free one with zero amount can be cash/upi/card — still mark Paid to close the book
        PaymentRequest pr2{bc.id, bc.amount, PaymentMethod::Cash, "", ""};
        auto r2 = lot.payBill(pr2);
        printReceipt(r2);

        lot.occupancy(freeC, usedC, total);
        cout << "After exit  -> Active: " << lot.activeCount()
             << " | free/used/total: " << freeC << "/" << usedC << "/" << total << "\n";

        // Stage 5 add-on demo: lost-ticket penalty
        auto td = lot.enterVehicle("E3", c);
        lot.adjustInTimeForTest(td, 30); // 30m
        Bill bd = lot.exitVehicle(td, "X3", /*lostTicket*/true);
        printBill(bd);
        auto rd = lot.payBill(PaymentRequest{bd.id, bd.amount, PaymentMethod::UPI, "", "anil@upi"});
        printReceipt(rd);

    } catch (const std::exception& e) {
        cerr << "[FATAL] " << e.what() << "\n";
        return 1;
    }
}


