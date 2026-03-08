#pragma once
#include "../../phase1/include/ServiceRequest.hpp"
#include "../../phase1/include/CSVParser.hpp"
#include "../../common/include/StringPool.hpp"
#include "../../common/include/StringRegistry.hpp"
#include "../../common/include/DateParser.hpp"

#include <vector>
#include <string>
#include <cstdint>

class SoADataStore {
    public:
        SoADataStore();
        ~SoADataStore() = default;

        void load(const std::string& path, int num_threads);

        std::vector<const ServiceRequest*> searchByZip(uint32_t zip_min, uint32_t zip_max,
                                                    int num_threads) const;
        std::vector<const ServiceRequest*> searchByDate(uint32_t from, uint32_t to,
                                                     int num_threads) const;
        std::vector<const ServiceRequest*> searchByBoundingBox(double lat_min, double lat_max,
                                                            double lon_min, double lon_max,
                                                            int num_threads) const;
        size_t size() const { return records_.size(); }
    
    private:
        std::vector<uint32_t> zip_;
        std::vector<uint32_t> created_;
        std::vector<float>    lat_;
        std::vector<float>    lon_;

        std::vector<ServiceRequest> records_;

        StringPool pool_;
        StringPool res_pool_;

        StringRegistry<uint8_t>  agency_reg_, borough_reg_, location_type_reg_,
                                status_reg_, channel_type_reg_, park_borough_reg_,
                                address_type_reg_;
        StringRegistry<uint16_t> problem_reg_, problem_detail_reg_;

};