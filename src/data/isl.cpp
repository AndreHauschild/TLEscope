#include "isl.h"
#include "core/types.h"
#include "util/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <algorithm>
#include <fstream>
#include <iostream>

/* Link schedule object */

LinkSchedule schedule;

/* Load link schedule file */

bool linkScheduleRead(const char *filename, LinkSchedule &schedule)
{

  std::ifstream file(filename);

  if (!file.is_open())
  {
    std::cerr << "Cannot open ISL schedule file: " << filename << std::endl;
    return false;
  }

  schedule.epochs.clear();

  double        epoch;
  unsigned int  numLinks;

  while (file >> epoch >> numLinks)
  {

    LinkEpoch entry;
    entry.epoch = epoch;
    entry.links.reserve(numLinks);

    for (std::size_t i = 0; i < numLinks; ++i)
    {

      unsigned int norad1;
      unsigned int norad2;

      if (!(file >> norad1 >> norad2))
      {
        std::cerr << "Error reading link pair at epoch " << epoch << std::endl;
        schedule.epochs.clear();
        return false;
      }

      entry.links.push_back({norad1,norad2});
    }

    /* Make sure the input file is ordered by epoch. */
    if (!schedule.epochs.empty() && epoch < schedule.epochs.back().epoch)
    {
      std::cerr << "Error: schedule is not sorted by epoch" << std::endl;
      schedule.epochs.clear();
      return false;
    }

    schedule.epochs.push_back(std::move(entry));
  }

  return !file.bad();
}

/* Search for links for the current epoch */

const LinkEpoch *linkScheduleFind(const LinkSchedule &schedule, double epoch)
{
  /*
   * std::lower_bound performs a binary search.
   *
   * It returns the first element whose epoch is >= the requested epoch.
   */
  auto it = std::lower_bound(schedule.epochs.begin(), schedule.epochs.end(), epoch,
                             [](const LinkEpoch &entry, double value){ return entry.epoch < value; });

  if (it != schedule.epochs.end())
  {
    return &(*it);
  }

  return nullptr;
}


/* Check for Earth masking
 *
 * h_mask : masking height in [km]
 *
 * */
bool earthMasking(const Vector3& sat1, const Vector3& sat2, const float h_mask)
{
  /* LOS vector */

  const float dx = sat2.x - sat1.x;
  const float dy = sat2.y - sat1.y;
  const float dz = sat2.z - sat1.z;

  const float los2 = dx * dx + dy * dy + dz * dz;

  /* Parameter t of the closest point on the infinite line
   * sat1 + t * (sat2 - sat1) to the Earth's center.
   */
  const float t = -(sat1.x * dx + sat1.y * dy + sat1.z * dz) / los2;

  /* Closest point must be between sat1 and sat2 */
  if (t <= 0.0f || t >= 1.0f)
    return false;

  /*  Closest point on LOS */
  const float x = sat1.x + t * dx;
  const float y = sat1.y + t * dy;
  const float z = sat1.z + t * dz;

  const float distance2 = x * x + y * y + z * z;

  const float distanceMin = (EARTH_RADIUS_KM + h_mask);

  /* Earth intersects the LOS */
  return distance2 < distanceMin * distanceMin;
}


/* Angle between two vectors */
float angleBetween(const Vector3& a, const Vector3& b)
{
  Vector3 c = Vector3CrossProduct(a,b);
  return std::atan2(Vector3Length(c), Vector3DotProduct(a,b));  // radians
}
