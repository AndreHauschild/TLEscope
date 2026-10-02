#include "file_dialog.h"

#include "imgui.h"

#include <algorithm>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

void FileDialog::open(const std::string& initialPath)
{
  if (initialPath.empty())
  {
    currentDirectory_ = fs::current_path();
  }
  else
  {
    fs::path path(initialPath);

    if (fs::is_directory(path))
      currentDirectory_ = path;
    else
      currentDirectory_ = path.parent_path();
  }

  fileName_.clear();
  open_ = true;

  ImGui::OpenPopup("Open File");
}

void FileDialog::setDirectory(const fs::path& path)
{
  std::error_code ec;

  if (fs::is_directory(path, ec))
  {
    currentDirectory_ = fs::weakly_canonical(path, ec);

    if (ec)
      currentDirectory_ = path;

    fileName_.clear();
  }
}

bool FileDialog::draw(std::string& selectedFile)
{
  bool result = false;

  if (!open_)
    return false;

  ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);

  if (ImGui::BeginPopupModal(
      "Open File",
      nullptr,
      ImGuiWindowFlags_NoCollapse))
  {
    // ------------------------------------------------------------
    // Current directory
    // ------------------------------------------------------------

    std::string directory = currentDirectory_.string();

    ImGui::Text("Directory:");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(-1);
    ImGui::InputText(
        "##directory",
        directory.data(),
        directory.size() + 1,
        ImGuiInputTextFlags_ReadOnly);

    // ------------------------------------------------------------
    // Navigation buttons
    // ------------------------------------------------------------

    if (ImGui::Button("Up"))
    {
      fs::path parent = currentDirectory_.parent_path();

      if (!parent.empty())
        setDirectory(parent);
    }

    ImGui::SameLine();

    if (ImGui::Button("Home"))
    {
      const char* home = std::getenv("HOME");

      if (home)
        setDirectory(home);
    }

    ImGui::Separator();

    // ------------------------------------------------------------
    // Directory / file list
    // ------------------------------------------------------------

    if (ImGui::BeginChild(
        "FileList",
        ImVec2(0, -80),
        ImGuiChildFlags_Borders))
    {
      std::vector<fs::directory_entry> entries;

      std::error_code ec;

      for (const auto& entry :
          fs::directory_iterator(currentDirectory_, ec))
      {
        if (ec)
          break;

        entries.push_back(entry);
      }

      // Directories first, then files.
      std::sort(
          entries.begin(),
          entries.end(),
          [](const fs::directory_entry& a,
              const fs::directory_entry& b)
              {
        if (a.is_directory() != b.is_directory())
          return a.is_directory();

        return a.path().filename().string() <
            b.path().filename().string();
              });

      for (const auto& entry : entries)
      {
        const fs::path path = entry.path();
        const std::string name = path.filename().string();

        if (entry.is_directory())
        {
          std::string label = "[DIR] " + name;

          if (ImGui::Selectable(label.c_str()))
          {
            setDirectory(path);
          }
        }
        else
        {
          // Only show files useful for TLEscope.
          std::string extension = path.extension().string();

          std::transform(
              extension.begin(),
              extension.end(),
              extension.begin(),
              [](unsigned char c)
              {
            return std::tolower(c);
              });

          if (extension != ".tle" &&
              extension != ".3le" &&
              extension != ".txt")
          {
            continue;
          }

          bool selected =
              fileName_ == name;

          if (ImGui::Selectable(
              name.c_str(),
              selected))
          {
            fileName_ = name;
          }

          // Double-click opens the file.
          if (selected &&
              ImGui::IsMouseDoubleClicked(
                  ImGuiMouseButton_Left))
          {
            selectedFile =
                (currentDirectory_ / name).string();

            open_ = false;
            ImGui::CloseCurrentPopup();

            result = true;
          }
        }
      }

      ImGui::EndChild();
    }

    // ------------------------------------------------------------
    // Filename + buttons
    // ------------------------------------------------------------

    ImGui::Text("File:");

    ImGui::SameLine();

    ImGui::SetNextItemWidth(-160);

    char buffer[512];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%s",
        fileName_.c_str());

    if (ImGui::InputText(
        "##filename",
        buffer,
        sizeof(buffer)))
    {
      fileName_ = buffer;
    }

    ImGui::SameLine();

    if (ImGui::Button("Open", ImVec2(70, 0)))
    {
      if (!fileName_.empty())
      {
        fs::path path =
            currentDirectory_ / fileName_;

        std::error_code ec;

        if (fs::is_regular_file(path, ec))
        {
          selectedFile = path.string();

          open_ = false;
          ImGui::CloseCurrentPopup();

          result = true;
        }
      }
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(70, 0)))
    {
      open_ = false;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  return result;
}
