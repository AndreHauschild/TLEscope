#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

/**
 * @file file_dialog.h
 * @brief File selection dialog for Dear ImGui.
 *
 * A simple modal "Open File" dialog. It provides:
 *  - directory navigation (click a directory, "Up" and "Home" buttons)
 *  - a user-editable file extension filter (e.g. ".tle, .3le, .txt")
 *  - an optional display of hidden files
 *  - memory of the last directory a file was selected from
 *
 * Typical usage:
 * @code
 * FileDialog dialog(".tle, .txt");          // construct once, keep alive
 *
 * if (ImGui::Button("Browse..."))
 *     dialog.open();                         // request the dialog
 *
 * if (dialog.isOpen() && dialog.draw())      // call every frame while open
 *     loadFile(dialog.fileName());           // true once a file was chosen
 * @endcode
 *
 * @note draw() must be called from within an active ImGui frame, and from the
 *       same ImGui ID scope as the one in which open() was called, because the
 *       dialog is implemented as an ImGui popup.
 */

#include <filesystem>
#include <string>
#include <vector>

/**
 * @class FileDialog
 * @brief Modal file selection dialog built on Dear ImGui.
 *
 * The dialog keeps its state (current directory, filter, hidden-file setting,
 * last used directory) in the object, so create one instance per use case and
 * keep it alive for the lifetime of the program.
 */
class FileDialog
{
    public:

        /**
         * @brief Construct a dialog with an initial extension filter.
         *
         * @param filterText Extensions to show, separated by commas, semicolons
         *                   or whitespace. Dots and wildcards are optional, so
         *                   ".tle", "*.tle" and "tle" are equivalent. Matching
         *                   is case-insensitive. An empty string or "*" shows
         *                   all files. Defaults to "*".
         *
         * @see setFilter()
         */
        FileDialog(std::string filterText = "*");

        /**
         * @brief Open the dialog.
         *
         * Selects the start directory using the following priority:
         *  1. The directory of the last selected file, if it still exists.
         *  2. The user's home directory, if @p initialPath is empty.
         *  3. @p initialPath, if it is a directory.
         *  4. The parent directory of @p initialPath, if it is a file path
         *     (or the current working directory if there is no parent).
         *
         * Also clears the currently highlighted file name.
         *
         * @param initialPath Preferred start directory or file path. May be empty.
         */
        void open(const std::string& initialPath = "");

        /**
         * @brief Draw the dialog and process user input.
         *
         * Call this every frame while the dialog is active (see isOpen()).
         * Does nothing and returns false if the dialog is not open.
         *
         * @return true in the single frame in which the user confirmed a
         *         selection (double-click or "Open" button). The selected path
         *         is then available through fileName(). false otherwise,
         *         including when the user cancelled.
         */
        bool draw();

        /**
         * @brief Check whether the dialog is currently open.
         * @return true between a call to open() and the moment the user
         *         selects a file or cancels.
         */
        bool isOpen() const { return open_; }

        /**
         * @brief Get the file selected by the user.
         *
         * @return Full path of the last file that was selected, or an empty
         *         string if no file has been selected yet. The value stays
         *         valid until the next selection.
         */
        std::string fileName() const { return fileNameSelected_; };

        /**
         * @brief Set the file extension filter from code.
         *
         * The user can still change the filter in the dialog afterwards.
         *
         * @param filterText Extensions separated by commas, semicolons or
         *                   whitespace, e.g. ".tle, .3le, .txt". Empty string
         *                   or "*" means "show all files".
         */
        void setFilter(const std::string& filterText)
        {
            filterText_ = filterText;
            parseFilter();
        }

        /**
         * @brief Get the current filter text, including user edits.
         *
         * Useful for saving the filter in program settings.
         *
         * @return The filter exactly as entered (not normalized).
         */
        const std::string& filter() const { return filterText_; }

        /**
         * @brief Set whether hidden files and directories are shown.
         * @param show true to show hidden entries, false to hide them.
         */
        void setShowHidden(bool show) { showHidden_ = show; }

        /**
         * @brief Check whether hidden files and directories are shown.
         *
         * Reflects the "Show hidden files" checkbox, so it includes user
         * changes made in the dialog.
         */
        bool showHidden() const { return showHidden_; }

    private:

        /**
         * @brief Change the directory shown in the dialog.
         *
         * Does nothing if @p path is not an existing directory. On success the
         * path is normalized (e.g. "a/../b" becomes "b") and the highlighted
         * file name is cleared.
         *
         * @param path Directory to navigate to.
         */
        void setDirectory(const std::filesystem::path& path);

        /**
         * @brief Determine the user's home directory.
         *
         * Linux/macOS: the HOME environment variable, falling back to the
         * user database (getpwuid). Windows: USERPROFILE, falling back to
         * HOMEDRIVE + HOMEPATH.
         *
         * @return Home directory, or "." if it cannot be determined.
         */
        std::string getHomeDirectory() const;

        /**
         * @brief Rebuild #extensions_ from #filterText_.
         *
         * Must be called whenever #filterText_ changes.
         */
        void parseFilter();

        /**
         * @brief Check whether a file passes the extension filter.
         *
         * Comparison is case-insensitive. Only the last extension is
         * considered (for "a.tar.gz" the extension is ".gz").
         *
         * @param path File path to test.
         * @return true if the filter is empty ("show all") or the extension
         *         of @p path is listed in #extensions_.
         */
        bool matchesFilter(const std::filesystem::path& path) const;

        /**
         * @brief Check whether a file or directory is hidden.
         *
         * An entry is hidden if its name starts with a dot (all platforms),
         * or, on Windows, if it has the hidden file attribute.
         *
         * @param path Path of the entry to test.
         */
        bool isHidden(const std::filesystem::path& path);

        /// Whether hidden entries are listed. Bound to the "Show hidden files" checkbox.
        bool showHidden_ = false;   // default: hidden files are not shown

        /// true while the dialog is open and draw() has work to do.
        bool open_ = false;

        /// Directory currently displayed in the file list.
        std::filesystem::path currentDirectory_;

        /// Directory of the last confirmed selection. Used as start directory
        /// the next time the dialog is opened. Empty until a file was selected.
        std::filesystem::path lastSelectedDirectory_;

        /// File name highlighted in the list or typed into the "File:" box
        /// (name only, relative to #currentDirectory_).
        std::string fileName_;

        /// Full path of the last confirmed selection, returned by fileName().
        std::string fileNameSelected_;

        /// Filter exactly as entered by the program or user, e.g. ".tle, .txt".
        std::string filterText_ = "";

        /// Parsed form of #filterText_: lowercase extensions with leading dot
        /// (e.g. ".tle"). Empty means "show all files".
        std::vector<std::string> extensions_;

};

#endif // FILE_DIALOG_H
