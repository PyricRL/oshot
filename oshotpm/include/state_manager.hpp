/*
 * Copyright 2026 Toni500
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 * disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
 * following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _STATE_MANAGER_HPP_
#define _STATE_MANAGER_HPP_

#include "manifest.hpp"
#include "toml_api.hpp"
#include "util.hpp"

class StateManager
{
public:
    StateManager();
    StateManager(StateManager&&)                 = default;
    StateManager(const StateManager&)            = delete;
    StateManager& operator=(const StateManager&) = delete;
    ~StateManager()                              = default;

    Result<>                AddNewRepo(const manifest_t& repo);
    Result<>                RemoveRepo(const std::string& repo);
    std::vector<manifest_t> GetAllRepos() const;

    template <typename T>
    Result<> UpdatePlugin(const std::string_view repo_name,
                          const std::string_view plugin_name,
                          const std::string_view key,
                          T&&                    value);

    const toml::table& GetState() const { return m_toml.GetTbl(); }

    bool SaveState();

private:
    const fs::path m_path{ get_config_dir() / "plugins" / "state.toml" };
    TomlAPI        m_toml;

    mutable std::vector<manifest_t> m_manifests_cache;
    mutable bool                    m_is_changed{ true };
};

template <typename T>
Result<> StateManager::UpdatePlugin(const std::string_view repo_name,
                                    const std::string_view plugin_name,
                                    const std::string_view key,
                                    T&&                    value)
{
    auto* repo_plugins_arr = m_toml.GetTbl()["repositories"][repo_name]["plugins"].as_array();
    if (!repo_plugins_arr)
        return Err("Couldn't find an array of plugins from repository '{}'", repo_name);

    bool found = false;
    for (auto&& plugins_node : *repo_plugins_arr)
    {
        auto* plugin_tbl = plugins_node.as_table();
        if (!plugin_tbl)
            continue;
        const std::string& name = TomlAPI(*plugin_tbl).GetValue<std::string>("name", UNKNOWN);
        if (name != plugin_name)
            continue;

        (*plugin_tbl).insert_or_assign(key, std::forward<T>(value));
        found = true;
        break;
    }

    if (!found)
        return Err("Couldn't find plugin '{}' in repository '{}' to update its state", plugin_name, repo_name);
    m_is_changed = true;
    return Ok();
}

#endif
