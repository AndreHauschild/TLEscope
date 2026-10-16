/*
 * tool_isl.cpp - ISL panel
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <cstdint>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

#include "tools.h"
#include "tools_common.h"
#include "tools_registry.h"
#include "tools_settings.h"
#include "core/astro.h"
#include "core/config.h"
#include "core/theme.h"
#include "util/log.h"

#include "imgui.h"
#include "IconsFontAwesome6.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

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

/* Link schedule object */

LinkSchedule schedule;

/* File dialog object */

FileDialog file_dialog_isl;

/* Data functions */

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
        LOG_ERROR("cannot read link pair at epoch %9.3f",epoch);
        schedule.epochs.clear();
        return false;
      }

      entry.links.push_back({norad1,norad2});
    }

    /* Make sure the input file is ordered by epoch. */
    if (!schedule.epochs.empty() && epoch < schedule.epochs.back().epoch)
    {
      LOG_ERROR("schedule is not sorted by epoch");
      schedule.epochs.clear();
      return false;
    }

    schedule.epochs.push_back(std::move(entry));
  }

  return !file.bad();
}

/* Search for links for the current epoch using binary search */
const LinkEpoch *linkScheduleFind(const LinkSchedule &schedule, double epoch)
{
  /*
   * std::lower_bound performs a binary search.
   *
   * It returns the first element whose epoch is < the requested epoch.
   */
  auto it = std::lower_bound(schedule.epochs.begin(), schedule.epochs.end(), epoch,
      [](const LinkEpoch &entry, double epoch){ return entry.epoch < epoch; });

  /* NOTE: the first test is false for empty vector, so the others are not evaluated
   * in this case preventing a segmentation fault!
   */
  if (it != schedule.epochs.end() &&
      epoch>=(schedule.epochs.front()).epoch && epoch<=(schedule.epochs.back()).epoch)
  {
    return &(*it);
  }

  return nullptr;
}


/* Check for Earth masking
 *
 * Vector3 sat1   : Position vector satellite 1  [km]
 * Vector3 sat2   : Position vector satellite 2  [km]
 * double  h_mask : masking height               [km]
 *
 */
bool earthMasking(const Vector3& sat1, const Vector3& sat2, const float h_mask=1000)
{
	
  /* LOS vector */
  const Vector3 d = Vector3Subtract(sat2,sat1);
  const float los2 = Vector3DotProduct(d,d);

  /* Parameter t of the closest point on the infinite line
   * sat1 + t * (sat2 - sat1) to the Earth's center.
   */
  const float t = -Vector3DotProduct(sat1,d) / los2;

  /* Closest point must be between sat1 and sat2 */
  if (t <= 0.0f || t >= 1.0f)
    return false;

  /*  Closest point on LOS */
  const Vector3 p = Vector3Add(sat1,Vector3Scale(d,t));

  const float distance2 = Vector3DotProduct(p,p);

  const float distanceMin = (EARTH_RADIUS_KM + h_mask);

  /* Earth intersects the LOS */
  return distance2 < distanceMin * distanceMin;
  
}

bool earthMaskingOld(const Vector3& sat1, const Vector3& sat2, const float h_mask=1000)
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

/* Draw the tool panel */

void DrawPanelIsl(UIContext *ctx, AppConfig *cfg)
{
  (void)ctx;

  /* fixed icon width so all checkboxes align vertically */
  const float icon_w = 24.0f;

  auto DrawLayerCheckbox = [&](const char *label, bool *value, const char *icon, const char *tooltip) {
      ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(*value ? g_theme.ui.accent : g_theme.ui.text_dim));
      ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
      /* center the icon within a fixed-width cell so all rows align */
      ImVec2 icon_sz = ImGui::CalcTextSize(icon);
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + (icon_w - icon_sz.x) * 0.5f, pos.y), col, icon);
      ImGui::PopStyleColor();
      ImGui::Dummy(ImVec2(icon_w, ImGui::GetFrameHeight()));
      ImGui::SameLine();
      ImGui::Checkbox(label, value);
      if (tooltip && ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", tooltip);
  };

  bool showIsl = ToolSettingGetBool(cfg, "isl.enabled", false);
  bool prev = showIsl;
  DrawLayerCheckbox("ISL Range", &showIsl, ICON_FA_ARROW_RIGHT_ARROW_LEFT,
                    "Show a ISL range line from all satellites (or one selected) to active satellites (3D only)");
  if (showIsl!=prev)
    ToolSettingSetBool(cfg, "isl.enabled", showIsl);
  ImGui::SameLine();

  if (ImGui::Button("Open file"))
  {
    char *home = std::getenv("HOME");
    std::string initialPath = (home? std::string(home)+ "/GNSS_DAT/TLE" : ".");
    file_dialog_isl.open(initialPath);
  }
  if (file_dialog_isl.draw())
  {
    LOG_INFO("Opening ISL schedule file: %s",file_dialog_isl.fileName().c_str());
    linkScheduleRead(file_dialog_isl.fileName().c_str(), schedule);
  }

}

/* Draw scene overlay */

void DrawSceneIsl(SceneContext *sctx, AppConfig *cfg)
{

  LOG_INFO("Current epoch %15.6f",sctx->current_epoch);

  if (ToolSettingGetBool(cfg, "isl.enabled", false))
  {
    double unix_time = get_unix_from_epoch(sctx->current_epoch);
    const LinkEpoch* isls = linkScheduleFind(schedule, unix_time);
    if (isls) {
      for (int i = 0; i<isls->links.size(); i++)
      {

        Vector3 pos3d1 = Vector3Zeros;
        Vector3 pos3d2 = Vector3Zeros;

        for (int j=0; j<sat_count; j++)
        {
          /* Do not show links to disabled satellites */
          if (!satellites[j].is_active)
            continue;

          /* Show only links from selected satellite */
          if(sctx->selected_sat && \
              (isls->links[i].norad1 != sctx->selected_sat->norad_id_num && \
               isls->links[i].norad2 != sctx->selected_sat->norad_id_num))
            continue;

          if (isls->links[i].norad1 == satellites[j].norad_id_num)
            pos3d1 = Vector3Scale(satellites[j].current_pos, 1.0f / DRAW_SCALE);
          if (isls->links[i].norad2 == satellites[j].norad_id_num)
            pos3d2 = Vector3Scale(satellites[j].current_pos, 1.0f / DRAW_SCALE);
        }

        if(Vector3Length(pos3d1)<1 || Vector3Length(pos3d2)<1)
          continue;

        /* Draw ISL connections with depth test and depth mask enabled */
        rlDrawRenderBatchActive();
        DrawLine3D(pos3d1, pos3d2, ApplyAlpha(RED, 0.6f));
        rlDrawRenderBatchActive();
      }
    }
  }

#define DEBUG 0
#if DEBUG > 0
  if (sctx->active_sat && sctx->active_sat->is_active)
  {
    for (int i = 0; i < sat_count; i++)
    {
      if (!satellites[i].is_active ||
          satellites[i].norad_id_num==sctx->active_sat->norad_id_num)
        continue;

      /* Earth masking */
      if (earthMasking(sctx->active_sat->current_pos,satellites[i].current_pos))
        continue;

      Vector3 h_pos3d = Vector3Scale(satellites[i].current_pos, 1.0f / DRAW_SCALE);
      Vector3 s_pos3d = Vector3Scale(sctx->active_sat->current_pos, 1.0f / DRAW_SCALE);

      /* draw on top of the clouds / scattering / ground coverage
       * layers: flush the pending batch, then disable depth test so
       * those earlier-drawn shells do not occlude the line between
       * home and the satellite (the batch must be flushed while the
       * state is changed, otherwise the line is drawn later with
       * depth testing still enabled) */
      rlDrawRenderBatchActive();
      rlDisableDepthTest();
      rlDisableDepthMask();
      DrawLine3D(h_pos3d, s_pos3d, ApplyAlpha(RED, 0.6f));
      rlDrawRenderBatchActive();
      rlEnableDepthTest();
      rlEnableDepthMask();
    }
  }
#endif

}

