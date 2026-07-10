#ifndef DISABLE_PLUGINS

#  include "plugin.hpp"
#  include "util.hpp"

void load_plugins()
{
    const fs::path pluginDir = get_config_dir() / "plugins";
    fs::create_directories(pluginDir);

    for (const auto& plugin_dir : fs::directory_iterator(pluginDir, fs::directory_options::skip_permission_denied))
    {
        if (!plugin_dir.is_directory())
            continue;

        const std::string expected =
            "lib" + plugin_dir.path().filename().string() + dylib::decorations::os_default().suffix;

        for (const auto& entry :
             fs::directory_iterator(plugin_dir.path(), fs::directory_options::skip_permission_denied))
        {
            const fs::path& path = entry.path();

            if (!path.has_extension() || path.extension().string() != dylib::decorations::os_default().suffix)
                continue;

            if (path.filename().string() != expected)
            {
                spdlog::warn("Found plugin filename '{}' at {}, expected '{}'. Skipping",
                             path.filename().string(),
                             path.parent_path().string(),
                             expected);
                continue;
            }

            try
            {
                dylib::library  lib(path.string());
                auto            oshot_get_plugin = lib.get_function<oshot_plugin_t*(void)>("oshot_host_get_plugin");
                oshot_plugin_t* plugin           = oshot_get_plugin();

                if (!plugin || plugin->abi_version != oshot_get_abi_version())
                {
                    spdlog::error("Plugin '{}' has incompatible ABI version, skipping", path.stem().string());
                    continue;
                }

                if (!plugin->render || !plugin->id || !plugin->name || plugin->name[0] == '\0')
                {
                    spdlog::error("Plugin '{}' doesn't define name/ID or render function", path.stem().string());
                    continue;
                }

                if (plugin->id[0] == '.' ||
                    !std::ranges::all_of(std::string_view(plugin->id), [](const unsigned char c) {
                        return (isalnum(c) || c == '-' || c == '_' || c == '=' || c == '.');
                    }))
                {
                    spdlog::error("Plugin '{}' has an invalid ID", plugin->id);
                    continue;
                }

                if (!std::ranges::all_of(std::string_view(plugin->name), [](const unsigned char c) {
                        return (isalnum(c) || c == '-' || c == '_' || c == '=' || c == ' ');
                    }))
                {
                    spdlog::error("Plugin name '{}' in '{}' contains chars other than -_= or alpha numerical",
                                  plugin->name,
                                  plugin->id);
                    continue;
                }

                fs::path plugin_config_dir  = get_config_dir() / "plugins" / plugin->id;
                fs::path plugin_config_path = plugin_config_dir / "config.toml";

                fs::create_directories(plugin_config_dir);

                TomlAPI toml_api;
                if (!fs::exists(plugin_config_path))
                    std::ofstream(plugin_config_path).close();
                toml_api.LoadFile(plugin_config_path.string());

                auto [it, inserted] = g_plugins.try_emplace(plugin->id,
                                                            plugin->id,
                                                            fmt::format("plugins.{}.", plugin->id),
                                                            std::move(plugin_config_dir),
                                                            plugin,
                                                            nullptr,  // state filled in below
                                                            std::move(lib),
                                                            std::move(toml_api),
                                                            std::move(plugin_config_path));
                if (!inserted)
                {
                    spdlog::warn("Duplicate plugin '{}'", plugin->id);
                    continue;
                }

                if (plugin->init)
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
    }
}

#endif
