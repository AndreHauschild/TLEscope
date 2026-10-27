#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

/**
 * @file file_dialog.h
 * @brief File selection dialog
 *
 * A simple file selection dialog
 *
 */

#include <string>
#include <filesystem>

static std::string getHomeDirectory();

class FileDialog
{
  public:

    void open(const std::string& initialPath = "");

    // Call every frame while the dialog is active.
    // Returns true when the user selected a file.
    bool draw();

    bool isOpen() const { return open_; }

    std::string fileName() const { return fileNameSelected_; };

  private:

      void setDirectory(const std::filesystem::path& path);

      bool open_ = false;

      std::filesystem::path currentDirectory_;
      std::filesystem::path lastSelectedDirectory_;

      std::string fileName_;
      std::string fileNameSelected_;

};

#endif // FILE_DIALOG_H
