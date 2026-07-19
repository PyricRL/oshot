/*
 * Copyright 2025 Toni500git
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

#include "manifest.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#include "tiny-process-library/process.hpp"
#include "util.hpp"

namespace fs = std::filesystem;

static bool is_valid_name(const std::string_view n)
{
    return std::ranges::all_of(n,
                               [](const unsigned char c) { return (isalnum(c) || c == '-' || c == '_' || c == '='); });
}

Manifest::Manifest(const fs::path& path) : m_path(path)
{
    m_toml.LoadFile(path.string());
}

Result<> Manifest::ParseManifest()
{
    if (IsParsed())
        return Ok();

    static std::string str_stderr;

    m_repo.name = m_toml.GetValue<std::string>("repository.name", "_\3");
    m_repo.url  = m_toml.GetValue<std::string>("repository.url", "");
    if (m_repo.name == "_\3")
        return Err("Couldn't find manifest repository name");
    if (!is_valid_name(m_repo.name))
        return Err(
            "Manifest repository name '{}' is invalid. Only alphanumeric and '-', '_', '=' are allowed in the name",
            m_repo.name);

    TinyProcessLib::Process proc(
        fmt::format("git -C {} rev-parse HEAD", m_path.parent_path().string()),
        "",
        [&](const char* buf, size_t len) { m_repo.git_hash.assign(buf, len); },
        [&](const char* buf, size_t len) { str_stderr.append(buf, len); });
    if (proc.get_exit_status() != 0)
        return Err("Failed to get manifest git repository hash: {}", str_stderr);
    m_repo.git_hash.erase(std::remove(m_repo.git_hash.begin(), m_repo.git_hash.end(), '\n'), m_repo.git_hash.end());

    const auto& deps_all  = m_toml.GetValueArrayStr("dependencies.all", {});
    const auto& deps_plat = m_toml.GetValueArrayStr("dependencies." PLATFORM, {});
    m_repo.dependencies.insert(m_repo.dependencies.end(), deps_all.begin(), deps_all.end());
    m_repo.dependencies.insert(m_repo.dependencies.end(), deps_plat.begin(), deps_plat.end());

    for (const auto& [name, _] : m_toml.GetTbl())
    {
        if (name.str() == "repository" || name.str() == "dependencies")
            continue;

        if (!is_valid_name(name.str()))
        {
            warn("Plugin '{}' has an invalid name. Only alphanumeric and '-', '_', '=' are allowed in the name",
                 name.str());
            continue;
        }

        m_repo.plugins.push_back(GetPlugin(name));
    }

    return Ok();
}

plugin_t Manifest::GetPlugin(const std::string_view name) const
{
    return { .name        = name.data(),
             .id          = m_toml.GetValue<std::string>(name, "id", "(unknown)"),
             .description = m_toml.GetValue<std::string>(name, "description", "(unknown)"),
             .output_dir  = m_toml.GetValue<std::string>(name, "output-dir", "(unknown)"),
             .licenses    = m_toml.GetValueArrayStr(name, "licenses", {}),
             .authors     = m_toml.GetValueArrayStr(name, "authors", {}),
             .build_steps = m_toml.GetValueArrayStr(name, "build-steps", {}),
             .platforms   = m_toml.GetValueArrayStr(name, "platforms", {}) };
}
