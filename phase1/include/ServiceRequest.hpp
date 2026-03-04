#pragma once
#include "StringPool.hpp"
#include <cstdint>

struct TimeInfo
{
    uint32_t created;
    uint32_t closed;
    uint32_t due;
    uint32_t resolution;
};

struct LocationInfo
{
    uint32_t zip;
    double latitude;
    double longitude;
    int32_t x_coord;
    int32_t y_coord;
    uint8_t borough;
    uint8_t address_type;
    StringRef address;
    StringRef street_name;
    StringRef cross_street_1;
    StringRef cross_street_2;
    StringRef intersection_1;
    StringRef intersection_2;
    StringRef city;
    StringRef landmark;
    StringRef location_point;
};

struct ServiceInfo
{
    uint8_t agency;
    uint16_t problem;
    uint16_t  problem_detail;
    uint8_t   location_type;
    uint8_t   status;
    StringRef agency_name;
    StringRef additional_details;
    StringRef facility_type;
    StringRef resolution_desc;
};

struct AdminInfo
{
    uint16_t  community_board;
    uint16_t  council_district;
    uint16_t  police_precinct;
    uint64_t  bbl;
    uint8_t   channel_type;
    uint8_t   park_borough;
    StringRef park_facility;
};

struct MiscInfo
{
    StringRef vehicle_type;
    StringRef taxi_borough;
    StringRef taxi_pickup;
    StringRef bridge_name;
    StringRef bridge_direction;
    StringRef road_ramp;
    StringRef bridge_segment;
};

struct ServiceRequest
{
    uint64_t    unique_key;
    TimeInfo    time;
    LocationInfo location;
    ServiceInfo  service;
    AdminInfo    admin;
    MiscInfo     misc;
};