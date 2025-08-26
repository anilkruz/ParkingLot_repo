#include<iostream>
#include<vector>

using namespace std;
enum struct VehicleTyepe {Bike,Car,Truck};
enum struct SlotType {TwoWheeler,FourWheeler,Heavy};
struct Slot{
    int slotid;
    SlotType slotType; // F1-s2
    bool isFalse;
};
struct Floor{
    int floorid;
    vector<Slot> slots;
};
class ParkingLot{
    public:
    vector<Floor> floors;
    void configure(vector<Floor> s){
        floors = std::move(s);
    }
    void occupancy(){
        int bike_slot=0,car_slot=0,heavt_slot=0;
            for(auto& s:floors)
            {       
                for(auto& j:s.slots)
                {
                if(SlotType::TwoWheeler == j.slotType) bike_slot++;
                else if(SlotType::FourWheeler == j.slotType) car_slot++;else heavt_slot++;
                }
            cout<<"Floor:"<<s.floorid<<endl<<"Number Car slots Avaible:"<<car_slot<<endl<<"Number of Bike Slots Avaible: "<<bike_slot<<endl<<"Number of heavt "<<heavt_slot;
        }
            
}
};
int main(){
    ParkingLot lot;
     Floor fs;
        fs.floorid = 1;
            fs.slots.push_back({0,SlotType::TwoWheeler,true});
            fs.slots.push_back({1,SlotType::TwoWheeler,true});
            fs.slots.push_back({2,SlotType::FourWheeler,true});
            fs.slots.push_back({3,SlotType::FourWheeler,true});
            fs.slots.push_back({4,SlotType::Heavy,true});
    lot.configure({fs});
    lot.occupancy();
}
