#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

/*
 * @file file_dialog.h
 * @brief File selection dialog
 *
 * A simple file selection dialog
 *
 */

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class FileDialog
{
    public:

        FileDialog(std::string filterText = "*");

        void open(const std::string& initialPath = "");

        // Call every frame while the dialog is active.
        // Returns true when the user selected a file.
        bool draw();

        bool isOpen() const { return open_; }

        std::string fileName() const { return fileNameSelected_; };

        // Set the filter from code, e.g. setFilter(".tle, .3le, .txt").
        // Empty string or "*" means "show all files".
        void setFilter(const std::string& filterText)
        {
            filterText_ = filterText;
            parseFilter();
        }

        const std::string& filter() const { return filterText_; }

        void setShowHidden(bool show) { showHidden_ = show; }
        bool showHidden() const { return showHidden_; }

    private:

        void setDirectory(const fs::path& path);
        std::string getHomeDirectory() const;

        void parseFilter();
        bool matchesFilter(const fs::path& path) const;

        bool isHidden(const fs::path& path);

        bool showHidden_ = false;   // default: hidden files are not shown
        bool open_ = false;

        fs::path currentDirectory_;
        fs::path lastSelectedDirectory_;

        std::string fileName_;
        std::string fileNameSelected_;

        std::string filterText_ = "";
        std::vector<std::string> extensions_;   // lowercase, with leading dot; empty = all

};

#endif // FILE_DIALOG_H
