#pragma once

#include <string>
#include <filesystem>

class FileDialog
{
  public:

    void open(const std::string& initialPath = "");

    // Call every frame while the dialog is active.
    // Returns true when the user selected a file.
    bool draw(std::string& selectedFile);

    bool isOpen() const { return open_; }

  private:

    void setDirectory(const std::filesystem::path& path);

    bool open_ = false;

    std::filesystem::path currentDirectory_;
    std::string fileName_;
};
