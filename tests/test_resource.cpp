#include "ResourceManager.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int failures = 0;
int checksRun = 0;

void check(bool condition, const std::string& label) {
    checksRun = checksRun + 1;
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        std::cout << "FAIL: " << label << "\n";
        failures = failures + 1;
    }
}

void printSnapshot(const std::string& title, const std::vector<int>& snapshot) {
    std::cout << title;
    for (int i = 0; i < static_cast<int>(snapshot.size()); i++) {
        std::cout << " " << snapshot[static_cast<std::size_t>(i)];
    }
    std::cout << "\n";
}

void testTimeHelper() {
    std::cout << "\nTime Helper Test:\n";
    check(resource::timeToSlot(0) == 0, "minute 0 is slot 0");
    check(resource::timeToSlot(59) == 0, "minute 59 is still slot 0");
    check(resource::timeToSlot(60) == 1, "minute 60 is slot 1");
    check(resource::timeToSlot(1439) == 23, "minute 1439 is the last slot 23");
    check(resource::timeToSlot(1440) == -1, "minute 1440 is outside the day");
    check(resource::timeToSlot(-1) == -1, "negative minute is outside the day");
    check(resource::statusToString(resource::EquipmentStatus::AVAILABLE) == "AVAILABLE",
          "status label for AVAILABLE");
    check(resource::statusToString(resource::EquipmentStatus::IN_TRANSIT) == "IN_TRANSIT",
          "status label for IN_TRANSIT");
}

void testSegmentTreeBruteForce() {
    std::cout << "\nSegment Tree vs Brute Force Test:\n";
    resource::SegmentTree tree;
    std::vector<int> expected(resource::NUM_SLOTS, 0);
    int mismatches = 0;

    // 200 random updates, some negative, and after each one every window is compared
    for (int step = 0; step < 200; step++) {
        int slot = std::rand() % resource::NUM_SLOTS;
        int delta = (std::rand() % 11) - 5;
        tree.pointUpdate(slot, delta);

        // the plain array clamps at zero the same way the tree does
        expected[static_cast<std::size_t>(slot)] = expected[static_cast<std::size_t>(slot)] + delta;
        if (expected[static_cast<std::size_t>(slot)] < 0) {
            expected[static_cast<std::size_t>(slot)] = 0;
        }

        for (int left = 0; left < resource::NUM_SLOTS; left++) {
            for (int right = left; right < resource::NUM_SLOTS; right++) {
                int bruteMax = 0;
                for (int i = left; i <= right; i++) {
                    if (expected[static_cast<std::size_t>(i)] > bruteMax) {
                        bruteMax = expected[static_cast<std::size_t>(i)];
                    }
                }
                if (tree.queryMax(left, right) != bruteMax) {
                    mismatches = mismatches + 1;
                }
            }
        }
    }
    std::cout << "Mismatches against the plain array: " << mismatches << "\n";
    check(mismatches == 0, "every (left, right) window matches brute force after 200 updates");
    check(tree.snapshot() == expected, "snapshot equals the plain array");
}

void testSegmentTreeEdges() {
    std::cout << "\nSegment Tree Edge Test:\n";
    resource::SegmentTree tree;
    tree.pointUpdate(0, 2);
    tree.pointUpdate(1, 5);
    tree.pointUpdate(2, 1);
    tree.pointUpdate(3, 4);

    // worked example from the plan, slots 0 to 3 hold 2 5 1 4
    check(tree.queryMax(1, 2) == 5, "worked example: max of slots 1 to 2 is 5");
    tree.pointUpdate(2, 6);
    check(tree.queryMax(1, 2) == 7, "after +6 on slot 2 the max of slots 1 to 2 is 7");
    check(tree.queryMax(0, 3) == 7, "after +6 on slot 2 the max of slots 0 to 3 is 7");

    check(tree.queryMax(3, 3) == 4, "single slot query");
    check(tree.valueAt(3) == 4, "valueAt matches a single slot query");
    check(tree.queryMax(0, 23) == 7, "full range 0 to 23");
    check(tree.queryMax(5, 2) == -1, "left bigger than right gives -1");
    check(tree.queryMax(-1, 3) == -1, "negative left gives -1");
    check(tree.queryMax(0, 24) == -1, "right past the last slot gives -1");
    check(!tree.pointUpdate(-1, 1), "slot -1 is rejected");
    check(!tree.pointUpdate(24, 1), "slot 24 is rejected");
    check(tree.size() == resource::NUM_SLOTS, "size is 24");

    // releasing more than was booked must stop at zero
    tree.pointUpdate(10, 3);
    tree.pointUpdate(10, -10);
    check(tree.valueAt(10) == 0, "over-release clamps at 0");
    tree.pointUpdate(10, 2);
    check(tree.valueAt(10) == 2, "slot works normally after a clamped release");

    // odd sized trees must work too
    resource::SegmentTree tiny(1);
    tiny.pointUpdate(0, 3);
    check(tiny.queryMax(0, 0) == 3, "tree with one slot works");
    resource::SegmentTree clamped(0);
    check(clamped.size() == 1, "zero slots is clamped to one");
}

void testBedBooking() {
    std::cout << "\nBed Booking Test (20 beds over shifts):\n";
    resource::ResourceManager manager(20);

    // 6 beds at night, 10 by day, 4 in the evening makes 20 beds booked in total
    manager.bookStay(0, 7, 6);
    manager.bookStay(8, 15, 10);
    manager.bookStay(16, 23, 4);

    // a midday surge stacks on top of the day shift
    manager.bookStay(10, 13, 3);

    printSnapshot("Snapshot after bookings:", manager.occupancySnapshot());
    std::cout << "Night peak 0..7: " << manager.queryMaxOccupancy(0, 7) << "\n";
    std::cout << "Day peak 8..15: " << manager.queryMaxOccupancy(8, 15) << "\n";
    std::cout << "Evening peak 16..23: " << manager.queryMaxOccupancy(16, 23) << "\n";
    std::cout << "Overlap peak 6..10: " << manager.queryMaxOccupancy(6, 10) << "\n";

    check(manager.getBedCapacity() == 20, "capacity is 20");
    check(manager.queryMaxOccupancy(0, 7) == 6, "night shift peak is 6");
    check(manager.queryMaxOccupancy(8, 15) == 13, "day shift peak is 13");
    check(manager.queryMaxOccupancy(16, 23) == 4, "evening shift peak is 4");
    check(manager.queryMaxOccupancy(6, 10) == 13, "window across night and day peaks at 13");
    check(manager.queryMaxOccupancy(0, 23) == 13, "whole day peak is 13");
    check(!manager.hasAlert(), "no alert while every hour is under capacity");

    // invalid input must be refused without changing anything
    std::vector<int> before = manager.occupancySnapshot();
    check(!manager.bookBed(24, 1), "bookBed on slot 24 is refused");
    check(!manager.bookBed(-1, 1), "bookBed on slot -1 is refused");
    check(!manager.bookStay(20, 25, 1), "bookStay running past the day is refused");
    check(!manager.bookStay(5, 2, 1), "bookStay with start after end is refused");
    check(manager.occupancySnapshot() == before, "refused bookings changed nothing");
    check(manager.queryMaxOccupancy(5, 2) == -1, "invalid window query gives -1");
}

void testAlertThreshold() {
    std::cout << "\nAlert Threshold Test:\n";
    resource::ResourceManager manager(20);

    manager.bookBed(5, 19);
    check(!manager.hasAlert(), "capacity - 1 beds does not raise the alert");
    check(manager.getAlertSlots().empty(), "no alert slots at capacity - 1");
    check(!manager.isOverCapacity(0, 23), "not over capacity at capacity - 1");
    check(manager.canAdmit(0, 23), "can still admit at capacity - 1");

    manager.bookBed(5, 1);
    check(manager.hasAlert(), "reaching capacity exactly raises the alert (>=)");

    std::vector<int> alertSlots = manager.getAlertSlots();
    check(alertSlots.size() == 1 && alertSlots[0] == 5, "alert slots lists only slot 5");
    check(manager.isOverCapacity(0, 23), "whole day window is over capacity");
    check(!manager.canAdmit(0, 23), "cannot admit across the full slot");
    check(manager.isOverCapacity(4, 6) == !manager.canAdmit(4, 6),
          "isOverCapacity and canAdmit agree");
    check(!manager.isOverCapacity(6, 10), "window after the full slot is fine");
    check(manager.canAdmit(6, 10), "can admit in the window after the full slot");
    check(manager.isOverCapacity(6, 10) == !manager.canAdmit(6, 10),
          "both agree on a clear window");

    // the flag is latched, it stays on even if the occupancy drops again
    manager.bookBed(5, -1);
    check(manager.hasAlert(), "alert stays latched after occupancy drops");
    check(manager.getAlertSlots().empty(), "alert slots list is live and now empty");
    manager.clearAlert();
    check(!manager.hasAlert(), "clearAlert resets the flag");

    // overbooking is allowed on purpose
    manager.bookBed(5, 3);
    check(manager.queryMaxOccupancy(5, 5) == 22, "overbooking is applied past capacity");
    check(manager.hasAlert(), "overbooking raises the alert");
}

void testAdmitDischarge() {
    std::cout << "\nAdmit and Discharge Test:\n";
    resource::ResourceManager manager(20);

    // fill the hospital for hours 2 to 5
    std::vector<int> bedIds;
    bool allAdmitted = true;
    for (int i = 0; i < 20; i++) {
        int bedId = manager.admit(2, 5);
        if (bedId == resource::NO_BED) {
            allAdmitted = false;
        }
        bedIds.push_back(bedId);
    }
    check(allAdmitted, "20 admissions all got a bed");
    check(bedIds[0] == 0 && bedIds[19] == 19, "bed ids are handed out smallest first");

    bool allDistinct = true;
    for (int i = 0; i < 20; i++) {
        for (int j = i + 1; j < 20; j++) {
            if (bedIds[static_cast<std::size_t>(i)] == bedIds[static_cast<std::size_t>(j)]) {
                allDistinct = false;
            }
        }
    }
    check(allDistinct, "no bed id was given out twice");
    check(manager.queryMaxOccupancy(2, 5) == 20, "hours 2 to 5 are full");

    // filling the last bed latched the alert already, clear it to see the refusal latch it again
    manager.clearAlert();
    check(!manager.hasAlert(), "alert cleared before the refusal");
    check(manager.admit(2, 5) == resource::NO_BED, "21st admission is refused");
    check(manager.hasAlert(), "refusal latches the alert");
    check(manager.admit(8, 9) == resource::NO_BED,
          "no bed id is free even though hours 8 to 9 are empty");
    check(manager.admit(5, 2) == resource::NO_BED, "backwards window is refused");

    // discharge frees the id and the future hours
    check(manager.discharge(7, 2), "discharge of bed 7 works");
    check(manager.queryMaxOccupancy(2, 5) == 19, "hours 2 to 5 dropped to 19");
    check(manager.canAdmit(2, 5), "room for one more after the discharge");
    check(!manager.discharge(7, 2), "double discharge returns false");
    check(manager.queryMaxOccupancy(2, 5) == 19, "double discharge did not release hours twice");
    check(!manager.discharge(-1, 2), "discharge of bed -1 returns false");
    check(!manager.discharge(20, 2), "discharge of bed 20 returns false");
    check(manager.admit(2, 5) == 7, "the freed bed id is reused");

    // early discharge frees only the later hours
    resource::ResourceManager earlyManager(20);
    int earlyBed = earlyManager.admit(2, 5);
    earlyManager.discharge(earlyBed, 4);
    printSnapshot("Snapshot after early discharge at slot 4:", earlyManager.occupancySnapshot());
    check(earlyManager.queryMaxOccupancy(2, 2) == 1, "slot 2 is still counted");
    check(earlyManager.queryMaxOccupancy(3, 3) == 1, "slot 3 is still counted");
    check(earlyManager.queryMaxOccupancy(4, 5) == 0, "slots 4 and 5 were freed");

    // smallest free bed is always next, even if the newest discharge was a bigger id
    resource::ResourceManager smallManager(3);
    smallManager.admit(0, 1);
    smallManager.admit(0, 1);
    smallManager.admit(0, 1);
    smallManager.discharge(2, 0);
    smallManager.discharge(0, 0);
    check(smallManager.admit(0, 1) == 0, "smallest free bed id is handed out first");
    check(smallManager.admit(0, 1) == 2, "then the next smallest");
}

void testHashMapBasics() {
    std::cout << "\nHash Map Basics Test:\n";
    resource::EquipmentMap map;
    for (int i = 0; i < 5; i++) {
        resource::EquipmentRecord record;
        record.equipment_id = 100 + i;
        record.status = resource::EquipmentStatus::AVAILABLE;
        record.room_node_id = i * 10;
        record.assigned_bot_id = resource::NO_BOT;
        map.insert(record);
    }
    check(map.size() == 5, "5 records stored");

    bool allFound = true;
    for (int i = 0; i < 5; i++) {
        resource::EquipmentRecord* found = map.find(100 + i);
        if (found == nullptr || found->room_node_id != i * 10) {
            allFound = false;
        }
    }
    check(allFound, "every record is found with the right room");

    // changing the record through the pointer changes the stored one
    resource::EquipmentRecord* changing = map.find(102);
    changing->status = resource::EquipmentStatus::IN_USE;
    check(map.find(102)->status == resource::EquipmentStatus::IN_USE,
          "status updated through pointer");

    const resource::EquipmentMap& constMap = map;
    check(constMap.find(102) != nullptr, "const find works");

    check(map.erase(103), "erase of 103 works");
    check(map.find(103) == nullptr, "103 is gone after erase");
    check(map.size() == 4, "size dropped to 4");
}

void testHashMapCollisions() {
    std::cout << "\nHash Map Collision Test:\n";
    resource::EquipmentMap map;
    int collidingIds[4] = {0, 11, 22, 33};

    // with 11 buckets, 0 11 22 and 33 all hash to bucket 0 so they share one chain
    for (int i = 0; i < 4; i++) {
        resource::EquipmentRecord record;
        record.equipment_id = collidingIds[i];
        record.status = resource::EquipmentStatus::AVAILABLE;
        record.room_node_id = i;
        record.assigned_bot_id = resource::NO_BOT;
        map.insert(record);
    }
    check(map.bucketCount() == 11, "still 11 buckets, no rehash yet");
    check(map.size() == 4, "4 records in one chain");

    bool allFound = true;
    for (int i = 0; i < 4; i++) {
        if (map.find(collidingIds[i]) == nullptr) {
            allFound = false;
        }
    }
    check(allFound, "all four colliding ids are found");

    // newest insert is the head of the chain, so the chain is 33 22 11 0
    check(map.erase(33), "erase head of chain");
    check(map.find(22) != nullptr && map.find(11) != nullptr && map.find(0) != nullptr,
          "rest of the chain survives a head erase");
    check(map.erase(11), "erase middle of chain");
    check(map.find(22) != nullptr && map.find(0) != nullptr,
          "chain still linked after a middle erase");
    check(map.erase(0), "erase tail of chain");
    check(map.find(22) != nullptr, "only element left is intact after a tail erase");
    check(map.erase(22), "erase the only element of the chain");
    check(map.size() == 0, "map is empty again");
    check(map.find(22) == nullptr, "nothing found in an empty map");
}

void testHashMapRehash() {
    std::cout << "\nHash Map Rehash Test:\n";
    resource::EquipmentMap map;
    std::size_t startBuckets = map.bucketCount();

    for (int i = 0; i < 200; i++) {
        resource::EquipmentRecord record;
        record.equipment_id = i;
        record.status = resource::EquipmentStatus::IN_USE;
        record.room_node_id = i * 2;
        record.assigned_bot_id = i + 1000;
        map.insert(record);
    }
    std::cout << "Buckets: " << startBuckets << " -> " << map.bucketCount()
              << ", load factor " << map.loadFactor() << "\n";

    check(map.bucketCount() > startBuckets, "bucket count grew");
    check(map.loadFactor() <= 0.75, "load factor is at most 0.75");
    check(map.size() == 200, "all 200 records counted");

    bool allCorrect = true;
    for (int i = 0; i < 200; i++) {
        const resource::EquipmentRecord* found = map.find(i);
        if (found == nullptr || found->room_node_id != i * 2 || found->assigned_bot_id != i + 1000
            || found->status != resource::EquipmentStatus::IN_USE) {
            allCorrect = false;
        }
    }
    check(allCorrect, "all 200 records survived with the right fields");
    check(map.all().size() == 200, "all() returns every record");
}

void testHashMapEdgeCases() {
    std::cout << "\nHash Map Edge Case Test:\n";
    resource::EquipmentMap map;
    resource::EquipmentRecord record;
    record.equipment_id = 5;
    record.status = resource::EquipmentStatus::AVAILABLE;
    record.room_node_id = 1;
    record.assigned_bot_id = resource::NO_BOT;

    check(map.insert(record), "first insert works");
    check(!map.insert(record), "duplicate insert returns false");
    check(map.size() == 1, "duplicate did not change the size");

    record.equipment_id = -3;
    check(!map.insert(record), "negative id is rejected");
    check(map.find(-3) == nullptr, "find on a negative id gives nullptr");
    check(!map.erase(-3), "erase on a negative id gives false");

    check(map.find(999) == nullptr, "find on a missing id gives nullptr");
    check(!map.erase(999), "erase on a missing id gives false");

    resource::ResourceManager manager(5);
    check(!manager.setEquipmentStatus(999, resource::EquipmentStatus::IN_USE),
          "setEquipmentStatus on a missing id gives false");
    check(!manager.setEquipmentRoom(999, 3), "setEquipmentRoom on a missing id gives false");
    check(!manager.assignBot(999, 1), "assignBot on a missing id gives false");
    check(!manager.releaseBot(999), "releaseBot on a missing id gives false");
    check(manager.getEquipment(999) == nullptr, "getEquipment on a missing id gives nullptr");
}

void testEquipmentFacade() {
    std::cout << "\nEquipment Facade Test:\n";
    resource::ResourceManager manager(20);

    // registered out of order on purpose to prove getAllEquipment sorts
    check(manager.registerEquipment(42, 10), "register 42");
    check(manager.registerEquipment(7, 20), "register 7");
    check(manager.registerEquipment(19, 30), "register 19");
    check(manager.registerEquipment(3, 40), "register 3");
    check(!manager.registerEquipment(7, 99), "duplicate register is refused");
    check(!manager.registerEquipment(-1, 99), "negative register is refused");

    const resource::EquipmentRecord* fresh = manager.getEquipment(19);
    check(fresh != nullptr && fresh->status == resource::EquipmentStatus::AVAILABLE
          && fresh->assigned_bot_id == resource::NO_BOT && fresh->room_node_id == 30,
          "new equipment starts AVAILABLE, in its room, with no bot");

    check(manager.assignBot(19, 8), "assign bot 8 to equipment 19");
    const resource::EquipmentRecord* moving = manager.getEquipment(19);
    check(moving->status == resource::EquipmentStatus::IN_TRANSIT, "status became IN_TRANSIT");
    check(moving->assigned_bot_id == 8, "bot id is stored");
    check(manager.findEquipmentByBot(8) == 19, "findEquipmentByBot returns the equipment id");
    check(manager.findEquipmentByBot(99) == -1, "unknown bot finds nothing");
    check(manager.findEquipmentByBot(resource::NO_BOT) == -1, "NO_BOT finds nothing");
    check(!manager.assignBot(19, -4), "negative bot id is refused");

    // the bot arrives, the glue code updates room, status and releases the bot
    check(manager.setEquipmentRoom(19, 55), "move equipment 19 to room 55");
    check(manager.setEquipmentStatus(19, resource::EquipmentStatus::IN_USE), "mark 19 IN_USE");
    check(manager.releaseBot(19), "release the bot");
    check(manager.getEquipment(19)->assigned_bot_id == resource::NO_BOT, "bot id cleared");
    check(manager.getEquipment(19)->room_node_id == 55, "room updated");
    check(manager.getEquipment(19)->status == resource::EquipmentStatus::IN_USE,
          "release left the status alone");
    check(manager.findEquipmentByBot(8) == -1, "bot 8 no longer has equipment");

    std::vector<resource::EquipmentRecord> allEquipment = manager.getAllEquipment();
    check(allEquipment.size() == 4, "getAllEquipment returns 4 records");
    bool sorted = true;
    for (int i = 1; i < static_cast<int>(allEquipment.size()); i++) {
        if (allEquipment[static_cast<std::size_t>(i - 1)].equipment_id
            >= allEquipment[static_cast<std::size_t>(i)].equipment_id) {
            sorted = false;
        }
    }
    check(sorted, "getAllEquipment is sorted by equipment id");
}

// builds a manager with itemCount pieces of equipment and times many lookups
long long timeLookups(int itemCount, int lookupCount, long long& checksum) {
    resource::ResourceManager manager(20);
    for (int i = 0; i < itemCount; i++) {
        manager.registerEquipment(i, i);
    }

    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
    for (int i = 0; i < lookupCount; i++) {
        const resource::EquipmentRecord* found = manager.getEquipment(i % itemCount);
        if (found != nullptr) {
            // adding up a field stops the compiler from deleting the whole loop
            checksum = checksum + found->room_node_id;
        }
    }
    std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
}

void testConstantTimeLookup() {
    std::cout << "\nO(1) Lookup Demonstration:\n";
    resource::ResourceManager manager(20);
    manager.registerEquipment(1, 12);
    manager.registerEquipment(2, 34);
    manager.registerEquipment(3, 56);
    manager.assignBot(2, 9);
    manager.setEquipmentStatus(3, resource::EquipmentStatus::MAINTENANCE);

    int lookupIds[4] = {1, 2, 3, 4};
    for (int i = 0; i < 4; i++) {
        const resource::EquipmentRecord* found = manager.getEquipment(lookupIds[i]);
        if (found == nullptr) {
            std::cout << "Equipment " << lookupIds[i] << ": not registered\n";
        } else {
            std::cout << "Equipment " << found->equipment_id
                      << ": status " << resource::statusToString(found->status)
                      << ", room " << found->room_node_id
                      << ", bot " << found->assigned_bot_id << "\n";
        }
    }
    check(manager.getEquipment(2)->assigned_bot_id == 9, "fetched equipment 2 with its bot");
    check(manager.getEquipment(4) == nullptr, "unregistered equipment 4 gives nullptr");

    // the time for 100000 lookups should stay about the same when the map is 100 times bigger
    long long checksum = 0;
    long long smallMicros = timeLookups(100, 100000, checksum);
    long long largeMicros = timeLookups(10000, 100000, checksum);
    std::cout << "100000 lookups with m = 100: " << smallMicros << " microseconds\n";
    std::cout << "100000 lookups with m = 10000: " << largeMicros << " microseconds\n";
    std::cout << "(checksum " << checksum << ")\n";
    check(checksum > 0, "timing loops really ran");
}

int main() {
    // fixed seed so a failure can be reproduced exactly
    std::srand(12345);

    testTimeHelper();
    testSegmentTreeBruteForce();
    testSegmentTreeEdges();
    testBedBooking();
    testAlertThreshold();
    testAdmitDischarge();
    testHashMapBasics();
    testHashMapCollisions();
    testHashMapRehash();
    testHashMapEdgeCases();
    testEquipmentFacade();
    testConstantTimeLookup();

    std::cout << "\nChecks run: " << checksRun << "\n";
    std::cout << "\nFailures: " << failures << "\n";
    return failures == 0 ? 0 : 1;
}
