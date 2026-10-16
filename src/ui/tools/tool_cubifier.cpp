// tool_cubeifier.cpp
#include "tools.h"
#include "tools_scene.h"
#include "tools_settings.h"
#include "core/config.h"
#include "imgui.h"
#include "raymath.h"

void DrawPanelCubeifier(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    bool show = ToolSettingGetBool(cfg, "cubeifier.enabled", false);
    if (ImGui::Checkbox("Show cube", &show))
        ToolSettingSetBool(cfg, "cubeifier.enabled", show);
}

void DrawSceneCubeifier(SceneContext *sctx, AppConfig *cfg)
{
    if (!ToolSettingGetBool(cfg, "cubeifier.enabled", false))
        return;
    Vector3 pos  = Vector3Scale(Vector3( 0.0f, 0.0f, 1.0f ),sctx->draw_earth_radius);
    Vector3 size = Vector3Scale(Vector3( 0.5f, 0.5f, 0.5f ),sctx->draw_earth_radius);
    DrawCubeV(pos, size, RED);             // raylib 3D draw call
}
