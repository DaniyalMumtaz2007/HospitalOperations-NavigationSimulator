#ifndef TYPES_HPP
#define TYPES_HPP

#include <string>
#include <vector>

enum class TriageLevel{
    RESUSCITATION = 1,
    EMERGENT = 2,
    URGENT = 3,
    SEMI_URGENT = 4,
    NON_URGENT = 5
};
struct Patient{
    int id;
    std::string name;
    int arrival_time;
    int acuity_score;  // 1 to 100
    TriageLevel level;
    int assigned_bed_id;
};
struct Coordinate {
    int x;
    int y;
    int floor;
};
struct Path {
    std::vector<int> node_ids;
    double total_cost;
    double estimated_transit_time;
};
enum class EventType {
    PATIENT_ARRIVAL,
    BED_ASSIGNED,
    EQUIPMENT_DISPATCH,
    BOT_ARRIVED,
    PATIENT_DISCHARGED
};
struct Event {
    int event_time;
    int event_id;
    EventType type;
    int target_id; // patient_id, bot_id, etc.
};
#endif