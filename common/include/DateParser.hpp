#pragma once
#include <cstdint>

uint32_t parseDateTime(const char* str, int len);
uint32_t toEpoch(int year, int month, int day, int hour, int min, int sec);
