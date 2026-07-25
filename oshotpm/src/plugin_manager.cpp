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
#include <random>

#include "dylib.hpp"
#include "fmt/format.h"
#include "fmt/ranges.h"
#include "tiny-process-library/process.hpp"

using namespace TinyProcessLib;

namespace
{
// Replaces the four repeated `fs::remove_all(working_dir)` calls scattered
// across the old BuildPlugins' early-return paths. Cleans up on any early
// return unless dismissed, i.e. once working_dir has been renamed away by
// FinalizeRepoDirectory, there's nothing left to clean up.
struct WorkingDirCleanupGuard
{
    fs::path path;
    bool     dismissed = false;

    ~WorkingDirCleanupGuard()
    {
        if (!dismissed)
            fs::remove_all(path);
    }

    void Dismiss() { dismissed = true; }
};
}  // namespace

// ----- GitClient -----
Result<> GitClient::Clone(const std::string& url, const fs::path& dest_dir) const
{
    std::string str_stderr;

    if (Process({ "git", "clone", "--recursive", url, dest_dir.string() }, "", nullptr, [&](const char* p, size_t n) {
            str_stderr.append(p, n);
        }).get_exit_status() != 0)
        return Err("Failed to clone at directory '{}': {}", dest_dir.string(), str_stderr);

    return Ok();
}

Result<> GitClient::PullRebase(const fs::path& repo_dir) const
{
    std::string output;
    auto        func = [&](const char* buf, size_t len) { output.assign(buf, len); };

    CdGuard guard(repo_dir);
    if (Process({ "git", "pull", "--rebase" }, "", func, func).get_exit_status() != 0)
        return Err("Failed to 'git pull --rebase' repository at '{}': {}", repo_dir.string(), output);

    spdlog::debug("git output = {}", output);
    return Ok();
}

Result<> GitClient::RevParseUpstream(const fs::path& repo_dir, std::string& out_hash) const
{
    std::string str_stderr;
    auto        err_func = [&](const char* buf, size_t len) { str_stderr.assign(buf, len); };

    CdGuard guard(repo_dir);
    if (Process(
            { "git", "rev-parse", "@{u}" },
            "",
            [&](const char* buf, size_t len) { out_hash.assign(buf, len); },
            err_func)
            .get_exit_status() != 0)
        return Err("Failed to retrieve upstream hash from repository at '{}': {}", repo_dir.string(), str_stderr);

    return Ok();
}

Result<> GitClient::LsRemoteHead(const std::string& url, std::string& out_hash) const
{
    auto func = [&](const char* buf, size_t len) { out_hash.assign(buf, len); };

    if (Process({ "git", "ls-remote", url, "HEAD" }, "", func, func).get_exit_status() != 0)
        return Err("Failed to retrieve latest commit from url {}", url);

    return Ok();
}

// ----- PluginBuilder -----
Result<PluginBuildResult> PluginBuilder::Build(const plugin_t&                             plugin,
                                               bool                                        is_update,
                                               const std::function<bool(const plugin_t&)>& is_conflicting) const
{
    if (!plugin.platforms.empty() && plugin.platforms.at(0) != "all")
    {
        const bool supported =
            std::find(plugin.platforms.begin(), plugin.platforms.end(), PLATFORM) != plugin.platforms.end();
        if (!supported)
        {
            m_callbacks.on_warning(
                fmt::format("Plugin '{}' doesn't support the platform '{}'. Skipping", plugin.name, PLATFORM));
            return Ok(PluginBuildResult::SkippedUnsupportedPlatform);
        }
    }

    if (is_conflicting(plugin) && !is_update)
        return Err(
            "Plugin '{}' has conflicting ID with other plugins.\n"
            "Check with 'oshotpm list' the plugins that have an equal ID and uninstall it",
            plugin.id);

    m_callbacks.on_status(fmt::format("Trying to build plugin '{}'", plugin.name));
    Process process({ "bash", "-c", fmt::format("set -e; {}", fmt::join(plugin.build_steps, " && ")) }, "");
    if (process.get_exit_status() != 0)
        return Err("Failed to build plugin '{}'", plugin.name);

    m_callbacks.on_success(fmt::format("Successfully built '{}' into '{}'", plugin.name, plugin.output_dir));
    return Ok(PluginBuildResult::Built);
}

// ----- PluginInstaller -----
Result<fs::path> PluginInstaller::InstallLibrary(const plugin_t& plugin,
                                                 const fs::path& manifest_config_path,
                                                 bool            force,
                                                 bool            is_update) const
{
    for (const auto& library : fs::directory_iterator{ plugin.output_dir })
    {
        const fs::path library_config_path = manifest_config_path / library.path().filename();

        if (fs::exists(library_config_path) && (!force || !is_update))
        {
            if (!m_callbacks.confirm(
                    fmt::format("Plugin '{}' already exists. Replace it?", library_config_path.string()), true))
                continue;
            fs::remove_all(library_config_path);
        }

        // Must be a file (library.so) and have the library extension of the OS
        if (library.is_regular_file() && library.path().has_extension() &&
            library.path().extension().string() == dylib::decorations::os_default().suffix)
        {
            std::error_code er;
            fs::rename(fs::canonical(library), library_config_path, er);
            if (er)
            {
                m_callbacks.on_error(fmt::format("Failed to move '{}' to '{}': {}",
                                                 fs::canonical(library).string(),
                                                 library_config_path.string(),
                                                 er.message()));
                continue;
            }
            return Ok(library_config_path);
        }
        else
        {
            m_callbacks.on_error(fmt::format("Built library '{}' is not a regular file", library.path().string()));
        }
    }

    return Err("Didn't find any library in output-dir");
}

// ----- PluginManager -----
bool PluginManager::IsPluginConflicting(const plugin_t& pending_plugin) const
{
    for (const manifest_t& manifest : m_state_manager.GetAllRepos())
        for (const plugin_t& plugin : manifest.plugins)
            if (pending_plugin.id == plugin.id)
                return true;
    return false;
}

Result<> PluginManager::AddPluginRepo(const std::string& repo, bool is_update)
{
    static std::mt19937                    gen(std::random_device{}());
    static std::uniform_int_distribution<> dist(0, 999999);

    WorkingDirCleanupGuard working_dir{ m_cache_path / ("plugin_" + fmt::to_string(dist(gen))) };
    fs::create_directories(working_dir.path);

    m_callbacks.on_status(fmt::format("Cloning repository '{}' at '{}'", repo, working_dir.path.string()));
    TRY(m_git.Clone(repo, working_dir.path));
    m_callbacks.on_success(
        fmt::format("Successfully cloned. Changing current directory to '{}'", working_dir.path.string()));
    working_dir.Dismiss();

    return BuildPlugins(working_dir.path, is_update);
}

Result<> PluginManager::UpdateRepos()
{
    for (const manifest_t& repo : m_state_manager.GetAllRepos())
    {
        if (fs::exists(m_cache_path / repo.name))
        {
            spdlog::debug("Repo '{}' cache path exists", repo.name);
            const fs::path repo_path = m_cache_path / repo.name;
            TRY(m_git.PullRebase(repo_path));

            std::string remote_hash;
            TRY(m_git.RevParseUpstream(repo_path, remote_hash));

            spdlog::debug("remote = {} && git_hash = {}", remote_hash, repo.git_hash);
            if (remote_hash.starts_with(repo.git_hash))
            {
                m_callbacks.on_info(fmt::format("{} is already up-to-date.", repo.name));
                continue;
            }

            m_callbacks.on_status(fmt::format("Updating {}", repo.name));
            TRY(BuildPlugins(repo_path, /*is_update=*/true));
        }
        else
        {
            // Cache dir got deleted but the repo is still tracked in state.toml.
            spdlog::debug("Repo '{}' cache path got deleted/not found", repo.name);
            std::string latest_hash;
            TRY(m_git.LsRemoteHead(repo.url, latest_hash));

            if (latest_hash.starts_with(repo.git_hash))
            {
                m_callbacks.on_info(fmt::format("{} is already up-to-date.", repo.name));
                continue;
            }

            m_callbacks.on_status(fmt::format("Cloning and then updating {}", repo.name));
            // is_update = true matters here: without it, plugins in this
            // manifest get flagged as "conflicting" against the state entries
            // this same repo already owns. The original only got this right
            // because the file-scope `is_update` global was already true from
            // earlier in this function.
            TRY(AddPluginRepo(repo.url, /*is_update=*/true));
        }
    }
    return Ok();
}

Result<> PluginManager::RemoveRepo(const std::string& repo_name)
{
    const std::vector<manifest_t>& repos = m_state_manager.GetAllRepos();

    std::error_code ec;
    fs::remove_all(m_cache_path / repo_name, ec);
    if (ec)
        return Err("Failed to remove plugin repository cache path '{}'", (m_cache_path / repo_name).string());

    fs::remove_all(m_config_path / repo_name, ec);
    if (ec)
        return Err("Failed to remove plugin repository config path '{}'", (m_config_path / repo_name).string());

    auto it = std::find_if(repos.begin(), repos.end(), [&](const manifest_t& m) { return m.name == repo_name; });
    if (it != repos.end())
    {
        for (const plugin_t& pl : it->plugins)
        {
            std::error_code ec;
            fs::remove_all(m_config_path / pl.id, ec);
            if (ec)
                return Err("Failed to remove plugin config path '{}'", (m_config_path / pl.id).string());
        }
    }

    TRY(m_state_manager.RemoveRepo(repo_name));
    m_callbacks.on_success(fmt::format("Removed plugin repository '{}'", repo_name));
    return Ok();
}

Result<> PluginManager::ConfirmTrustDisclaimer() const
{
    if (options.install_shut_up)
        return Ok();

    m_callbacks.on_warning(
        "You should never blindly trust anything in life that you never saw/know about.\n"
        "Right now you are installing something that can be a POTENTIAL trojan or any malware.\n"
        "Please make sure that you trust every plugin you put to compile and install.\n"
        "YOU ARE THE SOLE RESPONSABLE FOR ANY DAMAGES DONE ON YOUR MACHINE.");

    if (!m_callbacks.confirm("Do you want to continue installing these plugins?", true))
        return Err("Operation cancelled from the user");

    return Ok();
}

Result<> PluginManager::ConfirmDependencies(const manifest_t& repo) const
{
    if (repo.dependencies.empty())
        return Ok();

    m_callbacks.on_info(fmt::format(
        "The plugin repository {} requires the following dependencies, check if you have them installed:\n    {}",
        repo.name,
        fmt::join(repo.dependencies, ", ")));

    if (!m_callbacks.confirm("Are these dependencies installed?", true))
        return Err("Balling out, re-install the repository again after installing all dependencies.");

    return Ok();
}

Result<> PluginManager::BuildAllPlugins(const manifest_t& repo, bool is_update, std::vector<std::string>& out_skipped)
{
    for (const plugin_t& plugin : repo.plugins)
    {
        auto result = m_builder.Build(plugin, is_update, [this](const plugin_t& p) { return IsPluginConflicting(p); });
        if (!result)
            return result.error();

        if (result.get() == PluginBuildResult::SkippedUnsupportedPlatform)
            out_skipped.push_back(plugin.name);
    }
    return Ok();
}

Result<> PluginManager::FinalizeRepoDirectory(const fs::path& working_dir, const fs::path& repo_cache_path) const
{
    m_callbacks.on_status(fmt::format("Renaming working directory to '{}'", repo_cache_path.string()));
    fs::remove_all(repo_cache_path);
    fs::create_directories(repo_cache_path);

    std::error_code ec;
    fs::rename(working_dir, repo_cache_path, ec);
    if (ec)
        return Err("Failed to rename '{}' to '{}': {}", working_dir.string(), repo_cache_path.string(), ec.message());

    return Ok();
}

Result<> PluginManager::InstallAllPlugins(const manifest_t&               repo,
                                          const std::vector<std::string>& skipped,
                                          bool                            is_update)
{
    for (const plugin_t& plugin : repo.plugins)
    {
        const fs::path manifest_config_path = m_config_path / plugin.id;
        fs::create_directories(manifest_config_path);
        m_callbacks.on_status(fmt::format("Moving each built plugin to '{}'", manifest_config_path.string()));

        if (std::find(skipped.begin(), skipped.end(), plugin.name) != skipped.end())
            continue;

        if (!fs::exists(plugin.output_dir))
        {
            m_callbacks.on_error(
                fmt::format("Plugin '{}' output-dir '{}' doesn't exist", plugin.name, plugin.output_dir));
            continue;
        }

        Result<fs::path> library =
            m_installer.InstallLibrary(plugin, manifest_config_path, options.install_force, is_update);
        TRY(library);

        TRY(m_state_manager.UpdatePlugin(repo.name, plugin.name, "library", library.get().string()));
        if (!m_state_manager.SaveState())
            return Err("Failed to write plugin state of repository '{}'", repo.name);
    }
    return Ok();
}

Result<> PluginManager::BuildPlugins(const fs::path& working_dir, bool is_update)
{
    WorkingDirCleanupGuard cleanup{ working_dir };

    CdGuard  guard(working_dir);
    Manifest manifest(working_dir / MANIFEST_NAME);
    TRY_MSG(manifest.ParseManifest(), "Failed to parse manifest: {}");
    const manifest_t& repo = manifest.GetRepo();

    // Stays inline (not extracted) since it's the one step that needs to end
    // the pipeline successfully without it being an error — every helper
    // above follows "Err = real failure, Ok = keep going", which is what
    // makes them safe to chain with TRY().
    const fs::path repo_cache_path = m_cache_path / repo.name;
    if (fs::exists(repo_cache_path) && !is_update && !options.install_force)
    {
        m_callbacks.on_warning(
            fmt::format("Repository '{}' already exists in '{}'", repo.name, repo_cache_path.string()));
        return Ok();
    }

    TRY(ConfirmTrustDisclaimer());

    if (repo.plugins.empty())
        return Err("Looks like there are no plugins to build in repository '{}'", repo.name);

    TRY(ConfirmDependencies(repo));

    std::vector<std::string> skipped_plugins;
    TRY(BuildAllPlugins(repo, is_update, skipped_plugins));

    TRY(m_state_manager.AddNewRepo(manifest));
    m_callbacks.on_success("Repository plugins are successfully built!");

    TRY(FinalizeRepoDirectory(working_dir, repo_cache_path));
    cleanup.Dismiss();

    TRY(InstallAllPlugins(repo, skipped_plugins, is_update));

    m_callbacks.on_success(fmt::format("Enjoy the new plugins from {}", repo.name));
    return Ok();
}
