#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace resource {

constexpr int NUM_SLOTS = 24;          // one slot per hour of the simulated day
constexpr int MINUTES_PER_SLOT = 60;
constexpr int NO_BOT = -1;
constexpr int NO_ROOM = -1;
constexpr int NO_BED = -1;

// converts event_time (minutes since midnight) into a slot 0 to 23, returns -1 if outside the day
int timeToSlot(int eventTime);

enum class EquipmentStatus {
    AVAILABLE,
    IN_USE,
    IN_TRANSIT,
    MAINTENANCE
};

std::string statusToString(EquipmentStatus status);

struct EquipmentRecord {
    int equipment_id;
    EquipmentStatus status;
    int room_node_id;       // node id in HospitalGraph, NO_ROOM if unknown
    int assigned_bot_id;    // NO_BOT if nobody is carrying it
};

// max segment tree: every node stores the largest occupancy in its range of slots
class SegmentTree {
public:
    explicit SegmentTree(int numSlots = NUM_SLOTS);
    bool pointUpdate(int slot, int delta);        // false if slot is out of range
    int queryMax(int left, int right) const;      // -1 if the range is invalid
    int valueAt(int slot) const;
    int size() const;
    std::vector<int> snapshot() const;            // occupancy of every slot, for the GUI
private:
    int n_;
    std::vector<int> tree_;
    void update(int node, int lo, int hi, int slot, int delta);
    int query(int node, int lo, int hi, int left, int right) const;
};

// custom hash map, separate chaining, equipment_id -> EquipmentRecord
class EquipmentMap {
public:
    EquipmentMap();
    ~EquipmentMap();
    EquipmentMap(const EquipmentMap&) = delete;
    EquipmentMap& operator=(const EquipmentMap&) = delete;

    bool insert(const EquipmentRecord& record);   // false if id is negative or already exists
    EquipmentRecord* find(int equipmentId);       // nullptr if missing
    const EquipmentRecord* find(int equipmentId) const;
    bool erase(int equipmentId);
    std::size_t size() const;
    std::size_t bucketCount() const;
    double loadFactor() const;
    std::vector<EquipmentRecord> all() const;     // sorted by equipment_id, for the GUI
private:
    struct Node {
        EquipmentRecord record;
        Node* next;
    };
    std::vector<Node*> buckets_;
    std::size_t count_;
    std::size_t indexFor(int equipmentId) const;
    void rehash(std::size_t newBucketCount);
};

// the one class the event loop talks to
class ResourceManager {
public:
    explicit ResourceManager(int bedCapacity);

    // bed forecasting
    bool bookBed(int timeSlot, int count);                 // count < 0 releases, false if slot invalid
    bool bookStay(int startSlot, int endSlot, int count = 1);
    int queryMaxOccupancy(int startSlot, int endSlot) const;
    bool isOverCapacity(int startSlot, int endSlot) const; // peak >= capacity
    bool canAdmit(int startSlot, int endSlot) const;       // peak < capacity
    bool hasAlert() const;
    void clearAlert();
    int getBedCapacity() const;
    std::vector<int> occupancySnapshot() const;
    std::vector<int> getAlertSlots() const;

    // bed identity (Week 2)
    int admit(int startSlot, int endSlot);                 // bed id, or NO_BED if it cannot fit
    bool discharge(int bedId, int fromSlot);

    // equipment index
    bool registerEquipment(int equipmentId, int roomNodeId);
    const EquipmentRecord* getEquipment(int equipmentId) const;
    bool setEquipmentStatus(int equipmentId, EquipmentStatus status);
    bool setEquipmentRoom(int equipmentId, int roomNodeId);
    bool assignBot(int equipmentId, int botId);
    bool releaseBot(int equipmentId);
    int findEquipmentByBot(int botId) const;               // O(n) scan, -1 if none
    std::vector<EquipmentRecord> getAllEquipment() const;

private:
    int bedCapacity_;
    bool alertRaised_;
    SegmentTree occupancy_;
    EquipmentMap equipment_;
    std::vector<int> freeBedIds_;
    std::vector<bool> bedInUse_;
    std::vector<int> stayStart_;
    std::vector<int> stayEnd_;
    void checkAlert(int startSlot, int endSlot);
};

} // namespace resource

#endif
