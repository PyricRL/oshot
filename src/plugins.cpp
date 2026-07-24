#ifndef DISABLE_PLUGINS

#  include "../oshotpm/include/manifest.hpp"
#  include "plugin.hpp"
#  include "util.hpp"

static void load_plugin_path(const fs::path& path)
{
    const fs::path disabled_path = path.string() + ".disabled";

    const bool is_enabled = fs::exists(path);

    if (fs::exists(disabled_path))
    {
        spdlog::info("Plugin '{}' is disabled, skipping", path.filename().string());
        return;
    }

    if (!is_enabled)
    {
        spdlog::warn("Plugin file '{}' listed in state.toml not found (checked '{}' and '{}'). Skipping",
                     path.filename().string(),
                     path.string(),
                     disabled_path.string());
        return;
    }

    try
    {
        dylib::library  lib(path.string());
        oshot_plugin_t* plugin = lib.get_function<oshot_plugin_t*(void)>("oshot_host_get_plugin")();

        if (!plugin || plugin->abi_version != oshot_get_abi_version())
        {
            spdlog::error("Plugin '{}' has incompatible ABI version, skipping", path.stem().string());
            return;
        }

        if (!plugin->render || !plugin->id || !plugin->name || plugin->name[0] == '\0')
        {
            spdlog::error("Plugin '{}' doesn't define name/ID or render function", path.stem().string());
            return;
        }

        if (plugin->id[0] == '.' || !std::ranges::all_of(std::string_view(plugin->id), [](const unsigned char c) {
                return (isalnum(c) || c == '-' || c == '_' || c == '=' || c == '.');
            }))
        {
            spdlog::error("Plugin '{}' has an invalid ID", plugin->id);
            return;
        }

        if (!std::ranges::all_of(std::string_view(plugin->name), [](const unsigned char c) {
                return (isalnum(c) || c == '-' || c == '_' || c == '=' || c == ' ');
            }))
        {
            spdlog::error(
                "Plugin name '{}' in '{}' contains chars other than -_= or alpha numerical", plugin->name, plugin->id);
            return;
        }

        const fs::path& plugin_config_dir  = path.parent_path();
        fs::path        plugin_config_path = plugin_config_dir / "config.toml";

        fs::create_directories(plugin_config_dir);

        TomlAPI toml_api;
        if (!fs::exists(plugin_config_path))
            std::ofstream(plugin_config_path, std::ios::trunc | std::ios::ate).close();
        toml_api.LoadFile(plugin_config_path.string());

        auto [it, inserted] = g_plugins.try_emplace(plugin->id,
                                                    plugin->id,
                                                    fmt::format("plugins.{}.", plugin->id),
                                                    std::move(plugin_config_dir),
                                                    std::move(path),
                                                    plugin,
                                                    nullptr,  // state filled in below
                                                    is_enabled,
                                                    std::move(lib),
                                                    std::move(toml_api),
                                                    std::move(plugin_config_path));
        if (!inserted)
        {
            spdlog::warn("Duplicate plugin '{}'", plugin->id);
            return;
        }

        // TODO: add a *real* plugin management and metadata and do not load anything
        //       if plugin is set to disabled.
        if (is_enabled && plugin->init)
        {
            ScopedActivePlugin _(&it->second);
            it->second.state = plugin->init();
        }

        spdlog::info("loading plugin at {}!", path.string());
    }
    catch (const dylib::load_error& e)
    {
        spdlog::error("Failed to load '{}' library: {}", path.stem().string(), e.what());
    }
    catch (const dylib::symbol_error& e)
    {
        spdlog::error("Failed to get 'oshot_get_plugin()' symbol: {}", e.what());
    }
}

void load_plugins(const std::vector<manifest_t>& repos)
{
    const fs::path pluginsDir = get_config_dir() / "plugins";
    fs::create_directories(pluginsDir);

    for (const manifest_t& repo : repos)
        for (const plugin_t& plugin : repo.plugins)
            for (const std::string& path : plugin.libraries)
                load_plugin_path(path);
}

#endif
