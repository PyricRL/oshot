#ifndef DISABLE_PLUGINS

#  include "plugin.hpp"
#  include "util.hpp"

void load_plugins()
{
    const fs::path pluginsDir = get_config_dir() / "plugins";
    fs::create_directories(pluginsDir);

    for (const auto& plugin_dir : fs::directory_iterator(pluginsDir, fs::directory_options::skip_permission_denied))
    {
        if (!plugin_dir.is_directory())
            continue;

        const std::string expected =
            "lib" + plugin_dir.path().filename().string() + dylib::decorations::os_default().suffix;

        for (const auto& entry :
             fs::directory_iterator(plugin_dir.path(), fs::directory_options::skip_permission_denied))
        {
            const fs::path& path = entry.path();

            // TODO: add a *real* plugin management and metadata and do not load anything
            //       if plugin is set to disabled.
            const std::string expected_disabled = expected + ".disabled";
            const std::string filename          = path.filename().string();
            const bool        is_enabled_file   = filename == expected;
            const bool        is_disabled_file  = filename == expected_disabled;

            if (!is_enabled_file && !is_disabled_file)
            {
                spdlog::warn("Found plugin filename '{}' at {}, expected '{}' or '{}'. Skipping",
                             filename,
                             path.string(),
                             expected,
                             expected_disabled);
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

                // TODO: add a *real* plugin management and metadata and do not load anything
                //       if plugin is set to disabled.
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
                                                            std::move(path),
                                                            plugin,
                                                            nullptr,  // state filled in below
                                                            is_enabled_file,
                                                            std::move(lib),
                                                            std::move(toml_api),
                                                            std::move(plugin_config_path));
                if (!inserted)
                {
                    spdlog::warn("Duplicate plugin '{}'", plugin->id);
                    continue;
                }

                // TODO: add a *real* plugin management and metadata and do not load anything
                //       if plugin is set to disabled.
                if (is_enabled_file && plugin->init)
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
