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

#include <cstdlib>
#include <filesystem>
#include <unordered_map>

#include "fmt/compile.h"
#include "fmt/os.h"
#include "fmt/ranges.h"
#include "manifest.hpp"
#include "nvdialog/nvdialog_error.h"
#include "plugin_manager.hpp"
#include "state_manager.hpp"
#include "texts.hpp"
#include "util.hpp"

#if (!__has_include("version.h"))
#  error "version.h not found, please generate it with ../scripts/generateVersion.sh"
#else
#  include "version.h"
#endif

#include "getopt_port/getopt.h"

enum OPs
{
    NONE,
    INSTALL,
    UPDATE,
    LIST,
    ENABLE,
    DISABLE,
    UNINSTALL,
    GEN_MANIFEST,
    HELP
} op = NONE;

const std::unordered_map<std::string_view, OPs> map{
    { "install", INSTALL }, { "update", UPDATE },   { "list", LIST },           { "help", HELP },
    { "enable", ENABLE },   { "disable", DISABLE }, { "uninstall", UNINSTALL }, { "gen-manifest", GEN_MANIFEST },
};

OPs str_to_enum(const std::string_view name)
{
    if (auto it = map.find(name); it != map.end())
        return it->second;
    return NONE;
}

void version()
{
    fmt::print(
        "oshotpm {} built from branch '{}' at {} commit '{}' ({}).\n"
        "Date: {}\n"
        "Tag: {}\n",
        VERSION,
        GIT_BRANCH,
        GIT_DIRTY,
        GIT_COMMIT_HASH,
        GIT_COMMIT_MESSAGE,
        GIT_COMMIT_DATE,
        GIT_TAG);

    // if only everyone would not return error when querying the program version :(
    std::exit(EXIT_SUCCESS);
}

void help(int invalid_opt = false)
{
    fmt::print(FMT_COMPILE("{}"), oshotpm_help);

    std::exit(invalid_opt);
}

void help_install(int invalid_opt = false)
{
    fmt::print(FMT_COMPILE("{}"), oshotpm_help_install);

    std::exit(invalid_opt);
}

void help_list(int invalid_opt = false)
{
    fmt::print(FMT_COMPILE("{}"), oshotpm_help_list);

    std::exit(invalid_opt);
}

bool parse_install_args(int argc, char* argv[])
{
    // clang-format off
    const struct option long_opts[] = {
        {"force", no_argument, nullptr, 'f'},
        {"help",  no_argument, nullptr, 'h'},
        {"yes",   no_argument, nullptr, 'y'},
        {0, 0, 0, 0}
    };
    // clang-format on

    int opt;
    while ((opt = getopt_long(argc, argv, "+yfh", long_opts, nullptr)) != -1)
    {
        switch (opt)
        {
            case 'h': help_install(EXIT_SUCCESS); break;
            case '?': help_install(EXIT_FAILURE); break;

            case 'f': options.install_force = true; break;
            case 'y': options.install_shut_up = true; break;
        }
    }

    for (int i = optind; i < argc; ++i)
        options.arguments.emplace_back(argv[i]);

    if (options.arguments.empty())
        die("install: no repositories/paths given");

    return true;
}

bool parse_list_args(int argc, char* argv[])
{
    // clang-format off
    const struct option long_opts[] = {
        {"verbose", no_argument, nullptr, 'v'},
        {"help",    no_argument, nullptr, 'h'},
        {0, 0, 0, 0}
    };
    // clang-format on

    int opt;
    while ((opt = getopt_long(argc, argv, "+vh", long_opts, nullptr)) != -1)
    {
        switch (opt)
        {
            case 'v': options.list_verbose = true; break;
            case 'h': help_list(EXIT_SUCCESS); break;
            case '?': help_list(EXIT_FAILURE); break;
        }
    }

    return true;
}

bool parse_general_command_args(int argc, char* argv[])
{
    // clang-format off
    const struct option long_opts[] = {
        {"help",  no_argument, nullptr, 'h'},
        {0, 0, 0, 0}
    };
    // clang-format on

    int opt;
    while ((opt = getopt_long(argc, argv, "+h", long_opts, nullptr)) != -1)
    {
        switch (opt)
        {
            case 'h': help(EXIT_SUCCESS); break;
            case '?': help_install(EXIT_FAILURE); break;
        }
    }

    for (int i = optind; i < argc; ++i)
        options.arguments.emplace_back(argv[i]);

    return true;
}

static bool parseargs(int argc, char* argv[])
{
    // clang-format off
    int opt = 0;
    static const struct option opts[] = {
        {"version", no_argument, 0, 'V'},
        {"help",    no_argument, 0, 'h'},
        {"dialogs", no_argument, 0, 'D'},
        {0,0,0,0}
    };

    // clang-format on
    optind = 1;
    while ((opt = getopt_long(argc, argv, "+VhD", opts, nullptr)) != -1)
    {
        switch (opt)
        {
            case 0:   break;
            case '?': help(EXIT_FAILURE); break;

            case 'V': version(); break;
            case 'h': help(); break;
            case 'D': options.cli_only_logging = false; break;
            default:  return false;
        }
    }

    if (optind >= argc)
        help(EXIT_FAILURE);  // no subcommand

    std::string_view cmd      = argv[optind];
    int              sub_argc = argc - optind;
    char**           sub_argv = argv + optind;

    op = str_to_enum(cmd);
    switch (op)
    {
        case INSTALL: optind = 0; return parse_install_args(sub_argc, sub_argv);
        case LIST:    optind = 0; return parse_list_args(sub_argc, sub_argv);
        case HELP:    break;
        default:      optind = 0; return parse_general_command_args(sub_argc, sub_argv);
    }

    if (op == HELP)
    {
        if (sub_argc >= 2)
        {
            const std::string_view target(sub_argv[1]);
            if (target == "install")
                help_install();
            else if (target == "list")
                help_list();
            else
                die("Couldn't find help text for subcommand '{}'", cmd);
        }
        else
        {
            help(EXIT_FAILURE);
        }
    }

    return true;
}

void switch_plugin(const StateManager& state, bool switch_)
{
    const char*        switch_str = switch_ ? "Enabl" : "Disabl";  // e/ed/ing
    const toml::table& tbl        = state.GetState();

    for (const std::string& arg : options.arguments)
    {
        const size_t pos = arg.find('/');
        if (pos == arg.npos)
            die("Plugin to {}e '{}' doesn't have a slash '/' to separate repository and plugin", switch_str, arg);

        const std::string& repo   = arg.substr(0, pos);
        const std::string& plugin = arg.substr(pos + 1);

        const auto* repo_tbl = tbl["repositories"][repo].as_table();
        if (!repo_tbl)
            die("No such repository '{}'", repo);
        if (const auto* plugins_arr_tbl = repo_tbl->get_as<toml::array>("plugins"))
        {
            for (const auto& plugin_node : *plugins_arr_tbl)
            {
                const toml::table* plugin_tbl = plugin_node.as_table();
                if (!plugin_tbl || TomlAPI(*plugin_tbl).GetValue<std::string>("name", "(unknown)") != plugin)
                    continue;

                for (fs::path base_path : TomlAPI(*plugin_tbl).GetValueArrayStr("libraries", {}))
                {
                    if (base_path.extension() == ".disabled")
                        base_path.replace_extension();  // normalize to enabled form

                    const fs::path& enabled_path  = base_path;
                    const fs::path& disabled_path = base_path.string() + ".disabled";

                    fs::path current_path;
                    if (fs::exists(enabled_path))
                        current_path = enabled_path;
                    else if (fs::exists(disabled_path))
                        current_path = disabled_path;
                    else
                    {
                        warn("Plugin library '{}' not found. Skipping", base_path.string());
                        continue;
                    }

                    const fs::path& target_path = switch_ ? enabled_path : disabled_path;
                    if (current_path == target_path)
                    {
                        warn("{} is already {}ed", arg, switch_str);
                        continue;
                    }

                    fs::rename(current_path, target_path);
                    info("{}ed {}!", switch_str, arg);
                }
            }
        }
    }
}

void list_all_plugins(const StateManager& state)
{
    const auto& is_plugin_disabled = [&](const std::string& manifest_name, const std::string& plugin_name) {
        const auto* repo_tbl = state.GetState()["repositories"][manifest_name].as_table();
        if (!repo_tbl)
            die("No such repository '{}'", manifest_name);
        if (const auto* plugins_arr_tbl = repo_tbl->get_as<toml::array>("plugins"))
        {
            for (const auto& plugin_node : *plugins_arr_tbl)
            {
                const toml::table* plugin_tbl = plugin_node.as_table();
                if (!plugin_tbl || TomlAPI(*plugin_tbl).GetValue<std::string>("name", "(unknown)") != plugin_name)
                    continue;

                for (fs::path base_path : TomlAPI(*plugin_tbl).GetValueArrayStr("libraries", {}))
                    if (fs::exists(base_path += ".disabled"))
                        return true;
            }
        }
        return false;
    };

    if (options.list_verbose)
    {
        for (const manifest_t& manifest : state.GetAllRepos())
        {
            fmt::println("\033[1;32mRepository:\033[0m {}", manifest.name);
            fmt::println("\033[1;33mURL:\033[0m {}", manifest.url);
            fmt::println("\033[1;34mPlugins:");
            for (const plugin_t& plugin : manifest.plugins)
            {
                fmt::println("\033[1;34m - {} ({})\033[0m", plugin.name, plugin.id);
                fmt::println("\t\033[1;35mDescription:\033[0m {}", plugin.description);
                fmt::println("\t\033[1;36mAuthor(s):\033[0m {}", fmt::join(plugin.authors, ", "));
                fmt::println("\t\033[1;38;2;255;100;220mDisabled:\033[0m {}",
                             is_plugin_disabled(manifest.name, plugin.name));
                fmt::println("\t\033[1;38;2;220;220;220mLicense(s):\033[0m {}", fmt::join(plugin.licenses, ", "));
            }
            fmt::print("\033[0m");
        }
    }
    else
    {
        for (const manifest_t& manifest : state.GetAllRepos())
        {
            fmt::println("\033[1;32mRepository:\033[0m {} (\033[1;33m{}\033[0m)", manifest.name, manifest.url);
            fmt::println("\033[1;34mPlugins:");
            for (const plugin_t& plugin : manifest.plugins)
            {
                fmt::print("   \033[1;34m{} - \033[1;35m{}", plugin.name, plugin.description);
                if (is_plugin_disabled(manifest.name, plugin.name))
                    fmt::print(" \033[1;31m(DISABLED)");
                fmt::print("\n");
            }
            fmt::print("\033[0m");
        }
    }
}

int main(int argc, char* argv[])
{
    if (!parseargs(argc, argv))
        return -1;

    fs::create_directories({ get_home_cache_dir() / "oshotpm" });
    fs::create_directories({ get_config_dir() / "plugins" });
    StateManager state;

    if (!options.cli_only_logging)
    {
        nvd_set_error(NVD_NOT_INITIALIZED);
    }
    else if (nvd_init() != 0)
    {
        fmt::print(
            stderr, "Failed to initialize nvdialog: {}\n", nvd_string_to_cstr(nvd_stringify_error(nvd_get_error())));
        return -67;
    }

    switch (op)
    {
        case INSTALL:
        {
            if (options.arguments.size() < 1)
                die("Please provide a plugin repository to install");
            PluginManager plugin_manager(std::move(state));
            for (const std::string& arg : options.arguments)
            {
                if (fs::exists(arg))
                    MUST_OK(plugin_manager.BuildPlugins(arg), error("Failed to build repository: {}", _r.error_v()));
                else
                    MUST_OK(plugin_manager.AddPluginRepo(arg), error("Failed to add repository: {}", _r.error_v()));
            }
            break;
        }
        case LIST:
        {
            list_all_plugins(state);
            break;
        }
        case GEN_MANIFEST:
        {
            if (fs::exists(MANIFEST_NAME) && !ask_user_yn(false, "{} already exists. Overwrite it?", MANIFEST_NAME))
                return EXIT_FAILURE;
            auto f = fmt::output_file(MANIFEST_NAME, fmt::file::CREATE | fmt::file::WRONLY | fmt::file::TRUNC);
            f.print("{}", AUTO_MANIFEST);
            f.close();
            break;
        }
        case ENABLE:
        {
            switch_plugin(state, true);
            break;
        }
        case DISABLE:
        {
            switch_plugin(state, false);
            break;
        }
        case UPDATE:
        {
            PluginManager plugin_manager(std::move(state));
            MUST_OK(plugin_manager.UpdateRepos(), error("Failed to update repositories: {}", _r.error_v()));
            break;
        }
        case UNINSTALL:
        {
            if (options.arguments.size() < 1)
                die("Please provide a plugin repository to uninstall");

            PluginManager plugin_manager(std::move(state));
            for (const std::string& arg : options.arguments)
                MUST_OK(plugin_manager.RemoveRepo(arg), error("Failed to remove repository: {}", _r.error_v()));
            break;
        }
        default: warn("uh?");
    }

    return EXIT_SUCCESS;
}
