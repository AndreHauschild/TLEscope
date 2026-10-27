#include "file_dialog.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>    // std::tolower
#include <cstdio>
#include <cstdlib>   // std::getenv
#include <system_error>
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

namespace fs = std::filesystem;

namespace
{
    std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }
}

std::string getHomeDirectory()
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

    return ".";
}

FileDialog::FileDialog(std::string filterText)
{
    setFilter(filterText);
};

void FileDialog::open(const std::string& initialPath)
{
    std::error_code ec;

    // Prefer the directory of the last selected file
    if (!lastSelectedDirectory_.empty() && fs::is_directory(lastSelectedDirectory_, ec))
    {
        currentDirectory_ = lastSelectedDirectory_;
    }
    else if (initialPath.empty())
    {
        currentDirectory_ = fs::current_path();
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
            currentDirectory_ = path.parent_path();

            if (currentDirectory_.empty())
                currentDirectory_ = fs::current_path();
        }
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
        currentDirectory_ = path.lexically_normal();
        fileName_.clear();
    }
}

bool FileDialog::draw()
{
    bool result = false;

    if (!open_)
        return false;

    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal("Open File",nullptr,ImGuiWindowFlags_NoCollapse))
    {
        // ------------------------------------------------------------
        // Current directory
        // ------------------------------------------------------------

        std::string directory = currentDirectory_.string();

        ImGui::Text("Directory:");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##directory",directory.data(),directory.size() + 1,ImGuiInputTextFlags_ReadOnly);

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
            setDirectory(getHomeDirectory());

        ImGui::SameLine();
        ImGui::Checkbox("Show hidden files", &showHidden_);

        ImGui::Separator();

        // ------------------------------------------------------------
        // Directory / file list
        // ------------------------------------------------------------

        if (ImGui::BeginChild("FileList", ImVec2(0, -80), ImGuiChildFlags_Borders))
        {
            std::vector<fs::directory_entry> entries;
            std::error_code ec;

            for (const auto& entry : fs::directory_iterator(currentDirectory_, ec))
            {
                if (ec)
                    break;

                if (!showHidden_ && isHidden(entry.path()))
                    continue;

                entries.push_back(entry);
            }

            // Directories first, then files
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
                    std::string label = "[DIR] " + name;

                    if (ImGui::Selectable(label.c_str()))
                    {
                        setDirectory(path);
                    }
                }
                else
                {
                    if (!matchesFilter(path))
                        continue;

                    bool selected = fileName_ == name;
                    if (ImGui::Selectable(name.c_str(),selected))
                    {
                        fileName_ = name;
                    }

                    // Double-click opens the file
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

        ImGui::SetNextItemWidth(-160);

        char buffer[512];
        std::snprintf(buffer,sizeof(buffer),"%s",fileName_.c_str());

        if (ImGui::InputText("##filename",buffer,sizeof(buffer)))
        {
            fileName_ = buffer;
        }

        ImGui::SameLine();

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

void FileDialog::parseFilter()
{
    extensions_.clear();

    std::string token;
    std::vector<std::string> tokens;

    // Split on commas, semicolons and whitespace
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

bool FileDialog::matchesFilter(const fs::path& path) const
{
    if (extensions_.empty())
        return true;

    const std::string ext = toLower(path.extension().string());
    return std::find(extensions_.begin(), extensions_.end(), ext) != extensions_.end();
}

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
