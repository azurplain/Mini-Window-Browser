#pragma once

#include "AppModel.h"

#include <filesystem>

namespace xiaochuang {

class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path overridePath = {});

    const std::filesystem::path& Path() const noexcept { return path_; }
    bool Load(AppState& state) const;
    bool Save(const AppState& state, const RECT& normalRect, bool maximized) const;

private:
    std::filesystem::path path_;
};

std::wstring NormalizeInputUrl(const std::wstring& input);
std::wstring NormalizeComparableUrl(std::wstring url);
std::wstring HotkeyDisplayText(const HotkeyBinding& binding);

} // namespace xiaochuang
