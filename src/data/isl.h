#ifndef ISL_H
#define ISL__H

/**
 * @file isl.h
 * @brief Inter-satellite link related functionalities
 *
 */

#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <cstdint>
#include <vector>

#include "raylib.h"
#include "raymath.h"

/* Data structures */

struct LinkPair
{
    unsigned int norad1;
    unsigned int norad2;
};

struct LinkEpoch
{
    double epoch;
    std::vector<LinkPair> links;
};

struct LinkSchedule
{
    std::vector<LinkEpoch> epochs;
};

/* Data structures */

/* Read schedule from file */
bool linkScheduleRead(const char *filename, LinkSchedule &schedule);


/* Find exact epoch using binary search */
const LinkEpoch *linkScheduleFind(const LinkSchedule &schedule, double epoch);


/* Check for Earth masking
 *
 * h_mask : masking height in [km]
 *
 */
bool earthMasking(const Vector3& sat1, const Vector3& sat2, const float h_mask = 1000);


/* Angle between two vectors
 */
float angleBetween(const Vector3& a, const Vector3& b);

#endif
