/**
 * @file file_dialog.cpp
 * @brief Implementation of the Dear ImGui file selection dialog.
 *
 * See file_dialog.h for the public interface and usage.
 */

#include "file_dialog.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <system_error>

// Platform specific headers:
//  - POSIX (Linux/macOS): getpwuid()/getuid() for the home directory fallback
//  - Windows: GetFileAttributesW() for the hidden file attribute
#ifdef _WIN32
    // nothing extra needed
#else
    #include <pwd.h>
    #include <unistd.h>
#endif
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

/// Shorthand used throughout the dialog implementation.
/// @note This alias is visible in every file that includes this header.
namespace fs = std::filesystem;

namespace
{

/**
 * @brief Return a lowercase copy of a string.
 *
 * Used to compare file extensions case-insensitively.
 *
 * @param s Input string.
 * @return Copy of @p s with all characters converted by std::tolower().
 */
std::string toLower(std::string s)
{
    for(unsigned int i=0;i<s.length();i++)
    {
      s[i] = std::tolower(s[i]);
    }
    return s;
}

}

/**
 * @brief Construct the dialog and apply the initial filter.
 * @param filterText Initial extension filter, see setFilter().
 */
FileDialog::FileDialog(std::string filterText)
{
    setFilter(filterText);
}

/**
 * @brief Open the dialog and choose its start directory.
 *
 * Start directory priority: last selected directory, home directory (if
 * @p initialPath is empty), @p initialPath itself, or its parent directory.
 * Registers the ImGui popup; the popup is drawn by draw().
 *
 * @param initialPath Preferred start directory or file path. May be empty.
 */
void FileDialog::open(const std::string& initialPath)
{
    std::error_code ec;

    // Prefer the directory of the last selected file
    if (!lastSelectedDirectory_.empty() && fs::is_directory(lastSelectedDirectory_, ec))
    {
        currentDirectory_ = lastSelectedDirectory_;
    }
    // No hint given: start in the home directory
    else if (initialPath.empty())
    {
        currentDirectory_ = getHomeDirectory();
    }
    else
    {
        fs::path path(initialPath);

        ec.clear();

        if (fs::is_directory(path, ec))
        {
            currentDirectory_ = path;
        }
        else
        {
            // Path points to a file (or does not exist): use its directory
            currentDirectory_ = path.parent_path();

            // Bare file name without directory part: use working directory
            if (currentDirectory_.empty())
                currentDirectory_ = fs::current_path();
        }
    }

    // Start without a highlighted file and request the popup
    fileName_.clear();
    open_ = true;

    ImGui::OpenPopup("Open File");
}

/**
 * @brief Draw the dialog and handle user interaction.
 *
 * Layout (top to bottom): directory field, navigation row (Up, Home, hidden
 * files checkbox), scrollable file list, file name row with Open/Cancel
 * buttons, and the filter row.
 *
 * @return true in the frame in which a file was confirmed, false otherwise.
 */
bool FileDialog::draw()
{
    bool result = false;

    if (!open_)
        return false;

    // Initial window size; afterwards the user's size is kept
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal("Open File",nullptr,ImGuiWindowFlags_NoCollapse))
    {
        // ------------------------------------------------------------
        // Current directory
        // ------------------------------------------------------------

        // Read-only display of the current path. InputText needs a mutable
        // buffer, so a local copy is used; edits are not possible anyway.
        std::string directory = currentDirectory_.string();

        ImGui::Text("Directory:");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##directory",directory.data(),directory.size() + 1,ImGuiInputTextFlags_ReadOnly);

        // ------------------------------------------------------------
        // Navigation buttons
        // ------------------------------------------------------------

        // Go to the parent directory (nothing happens at the root)
        if (ImGui::Button("Up"))
        {
            fs::path parent = currentDirectory_.parent_path();

            if (!parent.empty())
                setDirectory(parent);
        }

        ImGui::SameLine();

        // Jump to the user's home directory
        if (ImGui::Button("Home"))
            setDirectory(getHomeDirectory());

        ImGui::SameLine();

        // Toggles display of hidden files and directories
        ImGui::Checkbox("Show hidden files", &showHidden_);

        ImGui::Separator();

        // ------------------------------------------------------------
        // Directory / file list
        // ------------------------------------------------------------

        // Scrollable child window. The negative height reserves space below
        // the list for the file name row and the filter row.
        if (ImGui::BeginChild("FileList", ImVec2(0, -80), ImGuiChildFlags_Borders))
        {
            std::vector<fs::directory_entry> entries;
            std::error_code ec;

            // Collect all entries of the current directory. On an iteration
            // error (e.g. permission denied) the list is cut short.
            for (const auto& entry : fs::directory_iterator(currentDirectory_, ec))
            {
                if (ec)
                    break;

                // Skip hidden entries unless the user asked for them
                if (!showHidden_ && isHidden(entry.path()))
                    continue;

                entries.push_back(entry);
            }

            // Directories first, then files; each group sorted by name
            std::sort(entries.begin(),entries.end(),
                    [](const fs::directory_entry& a,const fs::directory_entry& b)
                    {
                        if (a.is_directory() != b.is_directory())
                            return a.is_directory();
                        return a.path().filename().string() < b.path().filename().string();
                    });

            for (const auto& entry : entries)
            {
                const fs::path path = entry.path();
                const std::string name = path.filename().string();

                if (entry.is_directory())
                {
                    // Directories are always listed (the extension filter
                    // only applies to files). A click enters the directory.
                    std::string label = "[DIR] " + name;

                    if (ImGui::Selectable(label.c_str()))
                    {
                        setDirectory(path);
                    }
                }
                else
                {
                    // Hide files that do not match the extension filter
                    if (!matchesFilter(path))
                        continue;

                    // A click highlights the file and copies its name into
                    // the "File:" box
                    bool selected = fileName_ == name;
                    if (ImGui::Selectable(name.c_str(),selected))
                    {
                        fileName_ = name;
                    }

                    // Double-click on the highlighted file opens it
                    if (selected && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        fileNameSelected_ = (currentDirectory_ / name).string();
                        lastSelectedDirectory_ = currentDirectory_;

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

        // Leave room on the right for the Open and Cancel buttons
        ImGui::SetNextItemWidth(-160);

        // Editable file name. ImGui edits a char buffer, so copy the
        // string in and, if the user changed it, copy it back.
        char buffer[512];
        std::snprintf(buffer,sizeof(buffer),"%s",fileName_.c_str());

        if (ImGui::InputText("##filename",buffer,sizeof(buffer)))
        {
            fileName_ = buffer;
        }

        ImGui::SameLine();

        // Confirm the typed or highlighted file name. The file must exist
        // in the current directory; the extension filter is not enforced
        // here, so typed names that the filter hides are still accepted.
        if (ImGui::Button("Open", ImVec2(70, 0)))
        {
            if (!fileName_.empty())
            {
                fs::path path = currentDirectory_ / fileName_;

                std::error_code ec;

                if (fs::is_regular_file(path, ec))
                {
                    fileNameSelected_ = path.string();
                    lastSelectedDirectory_ = currentDirectory_;

                    open_ = false;
                    ImGui::CloseCurrentPopup();

                    result = true;
                }
            }
        }

        ImGui::SameLine();

        // Close the dialog without selecting anything
        if (ImGui::Button("Cancel", ImVec2(70, 0)))
        {
            open_ = false;
            ImGui::CloseCurrentPopup();
        }

        // ------------------------------------------------------------
        // File filter
        // ------------------------------------------------------------

        ImGui::Text("Filter:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);

        // Same buffer round trip as for the file name. Edits take effect
        // immediately: the file list is rebuilt every frame.
        char filterBuffer[256];
        std::snprintf(filterBuffer, sizeof(filterBuffer), "%s", filterText_.c_str());

        if (ImGui::InputTextWithHint("##filter",
                                     "e.g. .tle, .3le, .txt  (empty or * = all files)",
                                     filterBuffer, sizeof(filterBuffer)))
        {
            filterText_ = filterBuffer;
            parseFilter();
        }

        ImGui::EndPopup();
    }

    return result;
}

/**
 * @brief Determine the user's home directory.
 *
 * Linux/macOS: $HOME, falling back to the user database (getpwuid).
 * Windows: %USERPROFILE%, falling back to %HOMEDRIVE%%HOMEPATH%.
 *
 * @return Home directory, or "." if it cannot be determined.
 */
std::string FileDialog::getHomeDirectory() const
{
#ifdef _WIN32
    if (const char* profile = std::getenv("USERPROFILE"))
        return profile;

    const char* drive = std::getenv("HOMEDRIVE");
    const char* path  = std::getenv("HOMEPATH");
    if (drive && path)
        return std::string(drive) + path;
#else
    // Linux and macOS
    if (const char* home = std::getenv("HOME"))
    {
        // Ignore an empty variable
        if (*home != '\0')
            return home;
    }

    // Fallback: ask the user database
    if (const passwd* pw = getpwuid(getuid()))
    {
        if (pw->pw_dir && *pw->pw_dir != '\0')
            return pw->pw_dir;
    }
#endif

    // Last resort: current directory
    return ".";
}

/**
 * @brief Navigate to a directory.
 *
 * Ignored if @p path is not an existing directory, so the dialog never ends
 * up in an invalid location. The path is normalized and the highlighted file
 * name is cleared on success.
 *
 * @param path Directory to navigate to.
 */
void FileDialog::setDirectory(const fs::path& path)
{
    std::error_code ec;

    if (fs::is_directory(path, ec))
    {
        currentDirectory_ = path.lexically_normal();
        fileName_.clear();
    }
}

/**
 * @brief Parse #filterText_ into the list of lowercase extensions.
 *
 * Tokens are separated by commas, semicolons or whitespace. Each token may be
 * written as "*.tle", ".tle" or "tle"; all are stored as ".tle". A token that
 * consists only of wildcards ("*", "*.*") means "show all files" and empties
 * the extension list.
 */
void FileDialog::parseFilter()
{
    extensions_.clear();

    std::string token;
    std::vector<std::string> tokens;

    // Split on commas, semicolons and whitespace. A trailing "," is appended
    // so that the last token is flushed as well.
    for (char c : filterText_ + ",")
    {
        if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c)))
        {
            if (!token.empty())
                tokens.push_back(token);
            token.clear();
        }
        else
        {
            token += c;
        }
    }

    // Normalize every token to the form ".ext" (lowercase)
    for (std::string t : tokens)
    {
        // Accept "*.tle", ".tle" and "tle"
        while (!t.empty() && t.front() == '*')
            t.erase(t.begin());

        // "*" or "*.*" means: show everything
        if (t.empty() || t == "." || t == ".*")
        {
            extensions_.clear();
            return;
        }

        if (t.front() != '.')
            t.insert(t.begin(), '.');

        extensions_.push_back(toLower(t));
    }
}

/**
 * @brief Test a file against the extension filter.
 *
 * @param path File path to test.
 * @return true if no filter is active or the (lowercased) extension of
 *         @p path is in #extensions_.
 */
bool FileDialog::matchesFilter(const fs::path& path) const
{
    // Empty list: no filtering
    if (extensions_.empty())
        return true;

    const std::string ext = toLower(path.extension().string());
    return std::find(extensions_.begin(), extensions_.end(), ext) != extensions_.end();
}

/**
 * @brief Test whether a file or directory is hidden.
 *
 * @param path Path of the entry to test.
 * @return true if the name starts with a dot (all platforms) or, on Windows,
 *         if the entry has the hidden file attribute.
 */
bool FileDialog::isHidden(const fs::path& path)
{
    // All platforms: names starting with a dot are hidden
    const std::string name = path.filename().string();
    if (name.size() > 1 && name[0] == '.')
        return true;

#ifdef _WIN32
    // Windows: additionally honor the hidden file attribute
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN);
#else
    return false;
#endif
}
