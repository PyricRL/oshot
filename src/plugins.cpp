#include "plugin.hpp"
#include "util.hpp"

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
            if (entry.path().filename() != expected)
                continue;

            try
            {
                dylib::library  lib(entry.path().string());
                auto            oshot_get_plugin = lib.get_function<oshot_plugin_t*(void)>("oshot_host_get_plugin");
                oshot_plugin_t* plugin           = oshot_get_plugin();

                if (!plugin || plugin->abi_version != oshot_get_abi_version())
                {
                    error("Plugin '{}' has incompatible ABI version, skipping", entry.path().stem().string());
                    continue;
                }

                if (!plugin->render || !plugin->id || !plugin->name || plugin->name[0] == '\0')
                {
                    error("Plugin '{}' doesn't define name/ID or render function", entry.path().stem().string());
                    continue;
                }

                if (plugin->id[0] == '.' ||
                    !std::ranges::all_of(std::string_view(plugin->id), [](const unsigned char c) {
                        return (isalnum(c) || c == '-' || c == '_' || c == '=' || c == '.');
                    }))
                {
                    error("Plugin '{}' has an invalid ID", plugin->id);
                    continue;
                }

                if (!std::ranges::all_of(std::string_view(plugin->name), [](const unsigned char c) {
                        return (isalnum(c) || c == '-' || c == '_' || c == '=' || c == ' ');
                    }))
                {
                    error("Plugin '{}' contains chars other than -_= or alpha numerical", plugin->id);
                    continue;
                }

                auto [it, inserted] = g_plugins.try_emplace(plugin->id,
                                                            plugin->id,
                                                            fmt::format("plugins.{}.", plugin->id),
                                                            get_config_dir() / "plugins" / plugin->id,
                                                            plugin,
                                                            nullptr,  // state filled in below
                                                            std::move(lib));
                if (!inserted)
                {
                    error("Duplicate plugin '{}'", plugin->id);
                    continue;
                }

                if (plugin->init)
                {
                    ScopedActivePlugin _(&it->second);
                    it->second.state = plugin->init();
                }

                spdlog::info("loading plugin at {}!", entry.path().string());
            }
            catch (const dylib::load_error& e)
            {
                error("Failed to load '{}' library: {}", entry.path().stem().string(), e.what());
            }
            catch (const dylib::symbol_error& e)
            {
                error("Failed to get 'oshot_get_plugin()' symbol: {}", e.what());
            }
        }
    }
}
