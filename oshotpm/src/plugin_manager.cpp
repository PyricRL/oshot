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

#include "plugin_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "fmt/format.h"
#include "fmt/ranges.h"
#include "manifest.hpp"
#include "tiny-process-library/process.hpp"
#include "util.hpp"

using namespace TinyProcessLib;

static bool is_update = false;

Result<> PluginManager::AddPluginRepo(const std::string& repo)
{
    std::string str_stderr;

    static std::mt19937                    gen(std::random_device{}());
    static std::uniform_int_distribution<> dist(0, 999999);

    // create temponary directory
    const fs::path& working_dir = m_cache_path / ("plugin_" + std::to_string(dist(gen)));
    fs::create_directories(working_dir);

    // and lets clone the repository
    status("Cloning repository '{}' at '{}'", repo, working_dir.string());
    if (Process({ "git", "clone", "--recursive", repo, working_dir.string() },
                "",
                nullptr,
                [&](const char* p, size_t n) { str_stderr.append(p, n); })
            .get_exit_status() != 0)
    {
        fs::remove_all(working_dir);
        return Err("Failed to clone at directory '{}': {}", working_dir.string(), str_stderr);
    }
    success("Successfully cloned. Changing current directory to '{}'", working_dir.string());

    return BuildPlugins(working_dir);
}

bool PluginManager::IsPluginConflicting(const plugin_t& pending_plugin) const
{
    for (const auto& manifest : m_state_manager.GetAllRepos())
        for (const auto& plugin : manifest.plugins)
            if (pending_plugin.id == plugin.id)
                return true;
    return false;
}

Result<> PluginManager::UpdateRepos()
{
    for (const manifest_t& repo : m_state_manager.GetAllRepos())
    {
        std::string output;
        auto        func = [&](const char* buf, size_t len) { output.append(buf, len); };
        // the user didn't remove the cache directory, right?
        if (fs::exists(m_cache_path / repo.name))
        {
            spdlog::debug("Repo '{}' cache path exists", repo.name);
            CdGuard guard(m_cache_path / repo.name);
            if (Process({ "git", "pull", "--rebase" }, "", func, func).get_exit_status() != 0)
                return Err("Failed to 'git pull --rebase' repository {}: {}", repo.name, output);
            spdlog::debug("git output = {}", output);

            std::string remote;
            if (Process(
                    { "git", "rev-parse", "@{u}" },
                    "",
                    [&](const char* buf, size_t len) { remote.assign(buf, len); },
                    func)
                    .get_exit_status() != 0)
                return Err("Failed to retrieve upstream hash from repository {}: {}", repo.name, output);

            spdlog::debug("remote = {} && git_hash = {}", remote, repo.git_hash);
            // let's avoid any spaces or newlines
            if (remote.starts_with(repo.git_hash))
            {
                info("{} is already up-to-date.", repo.name);
                continue;
            }

            status("Updating {}", repo.name);
            is_update = true;
            TRY(BuildPlugins(m_cache_path / repo.name));
        }
        // they did, dammit
        else
        {
            spdlog::debug("Repo '{}' cache path got deleted/not found", repo.name);
            if (Process({ "git", "ls-remote", repo.url, "HEAD" }, "", func, func).get_exit_status() != 0)
                return Err("Failed to retrieve latest commit from url {}: {}", repo.url, output);

            spdlog::debug("git output = {}", output);
            // let's avoid any spaces or newlines
            if (output.starts_with(repo.git_hash))
            {
                info("{} is already up-to-date.", repo.name);
                continue;
            }
            status("Cloning and then updating {}", repo.name);
            is_update = true;
            TRY(AddPluginRepo(repo.url));
        }
    }

    return Ok();
}

Result<> PluginManager::BuildPlugins(const fs::path& working_dir)
{
    std::vector<std::string> non_supported_plugins;

    // cd to the working directory and parse its manifest
    CdGuard  guard(working_dir);
    Manifest manifest(working_dir / MANIFEST_NAME);
    TRY_MSG(manifest.ParseManifest(), "Failed to parse manifest: {}");

    const manifest_t& repo = manifest.GetRepo();

    // though lets check if we have already installed the plugin in the cache
    const fs::path& repo_cache_path = (m_cache_path / repo.name);
    if (fs::exists(repo_cache_path) && !is_update)
    {
        if (!options.install_force)
        {
            warn("Repository '{}' already exists in '{}'", repo.name, repo_cache_path.string());
            fs::remove_all(working_dir);
            return Ok();
        }
    }

    if (!options.install_shut_up)
    {
        spdlog::warn("{}",
                     "You should never blindly trust anything in life that you never saw/know about.\n"
                     "       Right now you are installing something that can be a \033[1;31mPOTENTIAL trojan or any "
                     "malware.\033[0m\n"
                     "       \033[1;36mPlease make sure that you trust every plugin you put to compile and install.\n"
                     "       \033[1;33mYOU ARE THE SOLE RESPONSABLE FOR ANY DAMAGES DONE ON YOUR MACHINE.");
        if (!ask_user_yn(false, "Do you want to continue installing these plugins?"))
            return Err("Operation cancelled from the user");
    }

    // So we don't have any plugins in the manifest uh
    if (repo.plugins.empty())
    {
        fs::remove_all(working_dir);
        return Err("Looks like there are no plugins to build in repository '{}'", repo.name);
    }

    if (!repo.dependencies.empty())
    {
        info("The plugin repository {} requires the following dependencies, check if you have them installed:\n    {}",
             repo.name,
             fmt::join(repo.dependencies, ", "));
        if (!options.install_shut_up && !ask_user_yn(true, "Are these dependencies installed?"))
            return Err("Balling out, re-install the repository again after installing all dependencies.");
    }

    // build each plugin from the manifest
    // and add the infos to the state.toml
    for (const plugin_t& plugin : repo.plugins)
    {
        bool found_platform = false;

        if (!plugin.platforms.empty() && plugin.platforms.at(0) != "all")
        {
            for (const std::string& plugin_platform : plugin.platforms)
                if (plugin_platform == PLATFORM)
                    found_platform = true;

            if (!found_platform)
            {
                warn("Plugin '{}' doesn't support the platform '{}'. Skipping", plugin.name, PLATFORM);
                non_supported_plugins.push_back(plugin.name);
                continue;
            }
        }

        if (IsPluginConflicting(plugin) && !is_update)
        {
            warn(
                "Plugin '{}' has conflicting ID with other plugins.\n"
                "Check with 'oshotpm list' the plugins that have an equal ID",
                plugin.id);
            if (!options.install_shut_up && !ask_user_yn(false, "Wanna continue?"))
            {
                fs::remove_all(working_dir);
                return Err("Balling out");
            }
        }

        status("Trying to build plugin '{}'", plugin.name);
        // make the shell stop at the first failure
        Process process({ "bash", "-c", fmt::format("set -e; {}", fmt::join(plugin.build_steps, " && ")) }, "");
        if (process.get_exit_status() != 0)
        {
            fs::remove_all(working_dir);
            return Err("Failed to build plugin '{}'", plugin.name);
        }

        success("Successfully built '{}' into '{}'", plugin.name, plugin.output_dir);
    }
    TRY(m_state_manager.AddNewRepo(manifest));

    // we built all plugins. let's rename the working directory to its actual manifest name,
    success("Repository plugins are successfully built!", repo_cache_path.string());
    status("Renaming directory working directory to '{}'", repo_cache_path.string());
    fs::remove_all(repo_cache_path);
    fs::create_directories(repo_cache_path);
    fs::rename(working_dir, repo_cache_path);

    // and then we move each plugin built library from its output-dir
    // and we'll declare all plugins we have moved.
    const fs::path& manifest_config_path = (m_config_path / repo.name);
    fs::create_directories(manifest_config_path);
    status("Moving each built plugin to '{}'", manifest_config_path.string());
    for (const plugin_t& plugin : repo.plugins)
    {
        // already told before
        if (std::find(non_supported_plugins.begin(), non_supported_plugins.end(), plugin.name) !=
            non_supported_plugins.end())
            continue;

        // ugh, devs fault. Report this error to them
        if (!fs::exists(plugin.output_dir))
        {
            error("Plugin '{}' output-dir '{}' doesn't exist", plugin.name, plugin.output_dir);
            continue;
        }

        toml::array built_libraries;
        for (const auto& library : fs::directory_iterator{ plugin.output_dir })
        {
            // ~/.config/oshot/plugins/<id>/<plugin-filename>
            const fs::path& library_config_path = manifest_config_path / library.path().filename();
            if (fs::exists(library_config_path) && (!options.install_force || !is_update))
            {
                if (options.install_shut_up ||
                    ask_user_yn(false, "Plugin '{}' already exists. Replace it?", library_config_path.string()))
                    fs::remove_all(library_config_path);
                else
                    continue;
            }

            if (library.is_regular_file() || library.is_symlink())
            {
                std::error_code er;
                fs::rename(fs::canonical(library), library_config_path, er);
                if (er)
                {
                    error("Failed to move '{}' to '{}': {}",
                          fs::canonical(library).string(),
                          library_config_path.string(),
                          er.message());
                    continue;
                }
                built_libraries.push_back(library_config_path.string());
            }
            else
            {
                error("Built library '{}' is not a regular file", library.path().string());
            }
        }
        m_state_manager.insert_or_assign_at_plugin(repo.name, plugin.name, "libraries", std::move(built_libraries));
    }
    success("Enjoy the new plugins from {}", repo.name);
    return Ok();
}

Result<> PluginManager::RemoveRepo(const std::string& repo_name)
{
    std::error_code ec;
    fs::remove_all(m_cache_path / repo_name, ec);
    if (ec)
        return Err("Failed to remove plugin repository cache path '{}'", (m_cache_path / repo_name).string());

    fs::remove_all(m_config_path / repo_name, ec);
    if (ec)
        return Err("Failed to remove plugin repository config path '{}'", (m_config_path / repo_name).string());

    TRY(m_state_manager.RemoveRepo(repo_name));
    success("Removed plugin repository '{}'", repo_name);
    return Ok();
}
