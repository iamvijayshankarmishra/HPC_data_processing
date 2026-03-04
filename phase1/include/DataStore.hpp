#pragma once
#include "ServiceRequest.hpp"
#include "CSVParser.hpp"
#include "StringPool.hpp"
#include "StringRegistry.hpp"
#include "DateParser.hpp"

#include <vector>
#include <string>
#include <string_view>
#include <cstdint>

class DataStore
{
public:
    DataStore();
    
    void load(const std::string& path);

    std::vector<const ServiceRequest*> searchByZip(uint32_t zip_min, uint32_t zip_max) const;

    std::vector<const ServiceRequest*> searchByDate(uint32_t from, uint32_t to) const;

    std::vector<const ServiceRequest*> searchByBoundingBox(double lat_min, double lat_max,
                    double lon_min, double lon_max) const;

    size_t size() const { return records_.size(); }

private:
    std::vector<ServiceRequest> records_;
    StringPool pool_;

    StringRegistry<uint8_t> agency_reg_;
    StringRegistry<uint8_t> borough_reg_;
    StringRegistry<uint8_t> location_type_reg_;
    StringRegistry<uint8_t> status_reg_;
    StringRegistry<uint8_t> channel_type_reg_;
    StringRegistry<uint8_t> park_borough_reg_;

    StringRegistry<uint16_t> problem_reg_;
    StringRegistry<uint16_t> problem_detail_reg_;

    ServiceRequest parseRow(std::string_view* fields, int nfields);
};