#include "ResourceManager.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace resource {

int timeToSlot(int eventTime) {
    if (eventTime < 0) {
        // negative minutes are before midnight so there is no slot for them
        return -1;
    }

    int slot = eventTime / MINUTES_PER_SLOT;
    if (slot >= NUM_SLOTS) {
        // past the end of the 24 hour day
        return -1;
    }
    return slot;
}

std::string statusToString(EquipmentStatus status) {
    if (status == EquipmentStatus::AVAILABLE) {
        return "AVAILABLE";
    } else if (status == EquipmentStatus::IN_USE) {
        return "IN_USE";
    } else if (status == EquipmentStatus::IN_TRANSIT) {
        return "IN_TRANSIT";
    } else if (status == EquipmentStatus::MAINTENANCE) {
        return "MAINTENANCE";
    }

    // only reachable if someone casts a bad number into the enum
    return "UNKNOWN";
}

// ---------- SegmentTree ----------

SegmentTree::SegmentTree(int numSlots)
    : n_(numSlots < 1 ? 1 : numSlots),
      tree_(static_cast<std::size_t>(4 * n_), 0) {
    // n_ is at least 1 so the tree always has a root, and 4 * n_ leaves room for the
    // recursive layout even when n_ is not a power of two (n_ is declared first so it is ready)
}

void SegmentTree::update(int node, int lo, int hi, int slot, int delta) {
    if (lo == hi) {
        // we reached the leaf for this hour, occupancy can never go below zero
        int newValue = tree_[node] + delta;
        if (newValue < 0) {
            newValue = 0;
        }
        tree_[node] = newValue;
        return;
    }

    int mid = (lo + hi) / 2;
    if (slot <= mid) {
        // slot lives in the left half
        update(node * 2, lo, mid, slot, delta);
    } else {
        // slot lives in the right half
        update(node * 2 + 1, mid + 1, hi, slot, delta);
    }

    // on the way back up every parent keeps the larger of its two children
    tree_[node] = std::max(tree_[node * 2], tree_[node * 2 + 1]);
}

int SegmentTree::query(int node, int lo, int hi, int left, int right) const {
    if (left > hi || right < lo) {
        // this node's range is completely outside the query so it cannot change the max
        return 0;
    }
    if (left <= lo && hi <= right) {
        // this node's range is completely inside the query so its stored max is the answer
        return tree_[node];
    }

    // partial overlap means the answer is split between the two children
    int mid = (lo + hi) / 2;
    int leftMax = query(node * 2, lo, mid, left, right);
    int rightMax = query(node * 2 + 1, mid + 1, hi, left, right);
    return std::max(leftMax, rightMax);
}

bool SegmentTree::pointUpdate(int slot, int delta) {
    if (slot < 0 || slot >= n_) {
        // slot is outside the day so nothing is changed
        return false;
    }
    update(1, 0, n_ - 1, slot, delta);
    return true;
}

int SegmentTree::queryMax(int left, int right) const {
    if (left > right || left < 0 || right >= n_) {
        // a backwards or out of range window has no answer
        return -1;
    }
    return query(1, 0, n_ - 1, left, right);
}

int SegmentTree::valueAt(int slot) const {
    return queryMax(slot, slot);
}

int SegmentTree::size() const {
    return n_;
}

std::vector<int> SegmentTree::snapshot() const {
    std::vector<int> values;
    for (int i = 0; i < n_; i++) {
        values.push_back(valueAt(i));
    }
    return values;
}

// ---------- EquipmentMap ----------

namespace {

// std::sort needs a way to compare two records, we order by equipment id so the GUI never flickers
bool recordComesBefore(const EquipmentRecord& first, const EquipmentRecord& second) {
    return first.equipment_id < second.equipment_id;
}

} // namespace

EquipmentMap::EquipmentMap()
    : buckets_(11, nullptr),
      count_(0) {
    // 11 starting buckets, a small prime so ids spread out when we use modulus
}

EquipmentMap::~EquipmentMap() {
    // every node was made with new so every node must be deleted here
    for (std::size_t i = 0; i < buckets_.size(); i++) {
        Node* current = buckets_[i];
        while (current != nullptr) {
            Node* nextNode = current->next;
            delete current;
            current = nextNode;
        }
    }
}

std::size_t EquipmentMap::indexFor(int equipmentId) const {
    // modulus makes sure the index never goes past the last bucket but wraps around
    // (ids are never negative here so the cast to unsigned is safe)
    return static_cast<std::size_t>(equipmentId) % buckets_.size();
}

bool EquipmentMap::insert(const EquipmentRecord& record) {
    if (record.equipment_id < 0) {
        // negative ids are not allowed so modulus can never give a negative index
        return false;
    }
    if (find(record.equipment_id) != nullptr) {
        // each equipment id can only be stored once
        return false;
    }

    // new nodes go at the head of the chain because that is O(1)
    std::size_t index = indexFor(record.equipment_id);
    Node* newNode = new Node;
    newNode->record = record;
    newNode->next = buckets_[index];
    buckets_[index] = newNode;
    count_ = count_ + 1;

    // too many items per bucket makes chains long and slow, so grow when above 0.75
    if (loadFactor() > 0.75) {
        rehash(buckets_.size() * 2 + 1);
    }
    return true;
}

const EquipmentRecord* EquipmentMap::find(int equipmentId) const {
    if (equipmentId < 0) {
        // negative ids can never have been inserted
        return nullptr;
    }

    // only the one chain this id hashes to has to be walked
    Node* current = buckets_[indexFor(equipmentId)];
    while (current != nullptr) {
        if (current->record.equipment_id == equipmentId) {
            return &current->record;
        }
        current = current->next;
    }
    return nullptr;
}

EquipmentRecord* EquipmentMap::find(int equipmentId) {
    // reuse the const version so the chain walking code only exists once
    const EquipmentMap* constThis = this;
    return const_cast<EquipmentRecord*>(constThis->find(equipmentId));
}

bool EquipmentMap::erase(int equipmentId) {
    if (equipmentId < 0) {
        // negative ids can never have been inserted
        return false;
    }

    std::size_t index = indexFor(equipmentId);
    Node* previous = nullptr;
    Node* current = buckets_[index];
    while (current != nullptr) {
        if (current->record.equipment_id == equipmentId) {
            if (previous == nullptr) {
                // head of the chain means the bucket itself must point to the next node
                buckets_[index] = current->next;
            } else {
                // middle or tail means the node before it skips over this one
                previous->next = current->next;
            }
            delete current;
            count_ = count_ - 1;
            return true;
        }
        previous = current;
        current = current->next;
    }
    return false;
}

void EquipmentMap::rehash(std::size_t newBucketCount) {
    if (newBucketCount < 1) {
        // a map with no buckets could not store anything
        return;
    }

    // existing nodes are relinked into the new array so no record is copied or moved in memory
    std::vector<Node*> newBuckets(newBucketCount, nullptr);
    for (std::size_t i = 0; i < buckets_.size(); i++) {
        Node* current = buckets_[i];
        while (current != nullptr) {
            // remember the next node first because relinking overwrites current->next
            Node* nextNode = current->next;
            std::size_t idAsIndex = static_cast<std::size_t>(current->record.equipment_id);
            std::size_t newIndex = idAsIndex % newBucketCount;
            current->next = newBuckets[newIndex];
            newBuckets[newIndex] = current;
            current = nextNode;
        }
    }
    buckets_ = newBuckets;
}

std::size_t EquipmentMap::size() const {
    return count_;
}

std::size_t EquipmentMap::bucketCount() const {
    return buckets_.size();
}

double EquipmentMap::loadFactor() const {
    // items per bucket, buckets_ is never empty so there is no divide by zero
    return static_cast<double>(count_) / static_cast<double>(buckets_.size());
}

std::vector<EquipmentRecord> EquipmentMap::all() const {
    // collect every record from every chain, they come out in hash order
    std::vector<EquipmentRecord> records;
    for (std::size_t i = 0; i < buckets_.size(); i++) {
        Node* current = buckets_[i];
        while (current != nullptr) {
            records.push_back(current->record);
            current = current->next;
        }
    }

    // then sort by id so the order is always the same
    std::sort(records.begin(), records.end(), recordComesBefore);
    return records;
}

// ---------- ResourceManager ----------

ResourceManager::ResourceManager(int bedCapacity)
    : bedCapacity_(bedCapacity < 1 ? 1 : bedCapacity),
      alertRaised_(false),
      occupancy_(NUM_SLOTS),
      equipment_(),
      freeBedIds_(),
      bedInUse_(static_cast<std::size_t>(bedCapacity_), false),
      stayStart_(static_cast<std::size_t>(bedCapacity_), 0),
      stayEnd_(static_cast<std::size_t>(bedCapacity_), 0) {
    // a hospital with zero beds makes no sense so capacity is clamped to at least 1 above

    // free ids are used like a stack so we push the biggest first, then the first pop gives bed 0
    for (int bedId = bedCapacity_ - 1; bedId >= 0; bedId--) {
        freeBedIds_.push_back(bedId);
    }
}

// ----- bed forecasting -----

void ResourceManager::checkAlert(int startSlot, int endSlot) {
    // the alert is latched, once any window reaches capacity it stays on until clearAlert
    int peak = occupancy_.queryMax(startSlot, endSlot);
    if (peak >= bedCapacity_) {
        alertRaised_ = true;
    }
}

bool ResourceManager::bookBed(int timeSlot, int count) {
    if (!occupancy_.pointUpdate(timeSlot, count)) {
        // slot is outside the day so nothing was booked
        return false;
    }

    // overbooking is allowed on purpose so the stress test can breach capacity, we only warn
    checkAlert(timeSlot, timeSlot);
    return true;
}

bool ResourceManager::bookStay(int startSlot, int endSlot, int count) {
    if (startSlot > endSlot || startSlot < 0 || endSlot >= occupancy_.size()) {
        // check the whole window first so a bad window never leaves a half booked stay
        return false;
    }

    // a patient keeps the bed for every hour from start to end, both included
    for (int slot = startSlot; slot <= endSlot; slot++) {
        bookBed(slot, count);
    }
    return true;
}

int ResourceManager::queryMaxOccupancy(int startSlot, int endSlot) const {
    return occupancy_.queryMax(startSlot, endSlot);
}

bool ResourceManager::isOverCapacity(int startSlot, int endSlot) const {
    // at capacity already counts as over because there is no bed left to give
    return queryMaxOccupancy(startSlot, endSlot) >= bedCapacity_;
}

bool ResourceManager::canAdmit(int startSlot, int endSlot) const {
    int peak = queryMaxOccupancy(startSlot, endSlot);
    if (peak < 0) {
        // invalid window so we cannot say yes
        return false;
    }
    return peak < bedCapacity_;
}

bool ResourceManager::hasAlert() const {
    return alertRaised_;
}

void ResourceManager::clearAlert() {
    alertRaised_ = false;
}

int ResourceManager::getBedCapacity() const {
    return bedCapacity_;
}

std::vector<int> ResourceManager::occupancySnapshot() const {
    return occupancy_.snapshot();
}

std::vector<int> ResourceManager::getAlertSlots() const {
    // unlike the latched flag this list is live, it only shows slots at capacity right now
    std::vector<int> snapshot = occupancy_.snapshot();
    std::vector<int> alertSlots;
    for (int slot = 0; slot < static_cast<int>(snapshot.size()); slot++) {
        if (snapshot[static_cast<std::size_t>(slot)] >= bedCapacity_) {
            alertSlots.push_back(slot);
        }
    }
    return alertSlots;
}

// ----- bed identity -----

int ResourceManager::admit(int startSlot, int endSlot) {
    if (queryMaxOccupancy(startSlot, endSlot) < 0) {
        // invalid window is a bad request and not a full hospital, so no alert is latched
        return NO_BED;
    }
    if (!canAdmit(startSlot, endSlot)) {
        // some hour in the window is already full so refuse and warn
        alertRaised_ = true;
        return NO_BED;
    }
    if (freeBedIds_.empty()) {
        // every bed id is still held by a patient who was not discharged yet, even if their
        // hours do not overlap this window, so there is no id to hand out
        alertRaised_ = true;
        return NO_BED;
    }

    // take the smallest free id, it sits at the back of the list
    int bedId = freeBedIds_.back();
    freeBedIds_.pop_back();
    bedInUse_[static_cast<std::size_t>(bedId)] = true;
    stayStart_[static_cast<std::size_t>(bedId)] = startSlot;
    stayEnd_[static_cast<std::size_t>(bedId)] = endSlot;

    bookStay(startSlot, endSlot, 1);
    return bedId;
}

bool ResourceManager::discharge(int bedId, int fromSlot) {
    if (bedId < 0 || bedId >= bedCapacity_) {
        // there is no bed with this id
        return false;
    }
    if (!bedInUse_[static_cast<std::size_t>(bedId)]) {
        // bed is already free so a second discharge must not release hours again
        return false;
    }

    // early discharge frees only the hours from now on, hours already lived stay counted
    int releaseStart = std::max(fromSlot, stayStart_[static_cast<std::size_t>(bedId)]);
    int releaseEnd = stayEnd_[static_cast<std::size_t>(bedId)];
    if (releaseStart <= releaseEnd) {
        bookStay(releaseStart, releaseEnd, -1);
    }
    bedInUse_[static_cast<std::size_t>(bedId)] = false;

    // keep the free list sorted with the biggest id first so the smallest free bed is
    // always handed out next (pushing to the back would hand out the newest one instead)
    std::vector<int>::iterator position = freeBedIds_.begin();
    while (position != freeBedIds_.end() && *position > bedId) {
        ++position;
    }
    freeBedIds_.insert(position, bedId);
    return true;
}

// ----- equipment index -----

bool ResourceManager::registerEquipment(int equipmentId, int roomNodeId) {
    EquipmentRecord record;
    record.equipment_id = equipmentId;
    record.status = EquipmentStatus::AVAILABLE;
    record.room_node_id = roomNodeId;
    record.assigned_bot_id = NO_BOT;

    // insert already refuses negative and duplicate ids
    return equipment_.insert(record);
}

const EquipmentRecord* ResourceManager::getEquipment(int equipmentId) const {
    return equipment_.find(equipmentId);
}

bool ResourceManager::setEquipmentStatus(int equipmentId, EquipmentStatus status) {
    EquipmentRecord* record = equipment_.find(equipmentId);
    if (record == nullptr) {
        // unknown equipment id
        return false;
    }
    record->status = status;
    return true;
}

bool ResourceManager::setEquipmentRoom(int equipmentId, int roomNodeId) {
    EquipmentRecord* record = equipment_.find(equipmentId);
    if (record == nullptr) {
        // unknown equipment id
        return false;
    }
    record->room_node_id = roomNodeId;
    return true;
}

bool ResourceManager::assignBot(int equipmentId, int botId) {
    if (botId < 0) {
        // a real bot id is never negative, and NO_BOT must keep meaning nobody
        return false;
    }
    EquipmentRecord* record = equipment_.find(equipmentId);
    if (record == nullptr) {
        // unknown equipment id
        return false;
    }

    // a bot carrying the equipment means it is on the move
    record->assigned_bot_id = botId;
    record->status = EquipmentStatus::IN_TRANSIT;
    return true;
}

bool ResourceManager::releaseBot(int equipmentId) {
    EquipmentRecord* record = equipment_.find(equipmentId);
    if (record == nullptr) {
        // unknown equipment id
        return false;
    }

    // status is left alone here, the caller decides what the equipment becomes next
    record->assigned_bot_id = NO_BOT;
    return true;
}

int ResourceManager::findEquipmentByBot(int botId) const {
    if (botId < 0) {
        // NO_BOT would match every free piece of equipment, so a negative id finds nothing
        return -1;
    }

    // the map is keyed by equipment id so finding by bot id has to scan everything, O(n)
    std::vector<EquipmentRecord> records = equipment_.all();
    for (std::size_t i = 0; i < records.size(); i++) {
        if (records[i].assigned_bot_id == botId) {
            return records[i].equipment_id;
        }
    }
    return -1;
}

std::vector<EquipmentRecord> ResourceManager::getAllEquipment() const {
    return equipment_.all();
}

} // namespace resource
