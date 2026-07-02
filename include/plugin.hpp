#pragma once

#include <unordered_map>

#include "../src/plugins/oshot_plugin.h"
#include "dylib.hpp"

struct plugin_runtime_t;

extern std::unordered_map<std::string, plugin_runtime_t> g_plugins;
extern plugin_runtime_t*                                 g_current_plugin;  // plain global, see above

struct ScopedActivePlugin
{
    plugin_runtime_t* prev;
    explicit ScopedActivePlugin(plugin_runtime_t* rt) : prev(g_current_plugin) { g_current_plugin = rt; }
    ~ScopedActivePlugin() { g_current_plugin = prev; }
};

struct plugin_runtime_t
{
    std::string           id;             // must be validated
    std::string           config_prefix;  // cached "plugins.<id>."
    std::filesystem::path data_dir;

    oshot_plugin_t* plugin = nullptr;  // non-owning and static, process-lifetime per ABI contract
    void*           state  = nullptr;  // owned by the plugin; released via plugin->destroy()
    dylib::library  lib;

    plugin_runtime_t(std::string           id,
                     std::string           config_prefix,
                     std::filesystem::path data_dir,
                     oshot_plugin_t*       plugin,
                     void*                 state,
                     dylib::library        lib)
        : id(std::move(id)),
          config_prefix(std::move(config_prefix)),
          data_dir(std::move(data_dir)),
          plugin(plugin),
          state(state),
          lib(std::move(lib))
    {}

    ~plugin_runtime_t()
    {
        if (plugin && plugin->destroy && state)
        {
            ScopedActivePlugin _(this);  // destroy() may still call oshot_log/config_get_*
            plugin->destroy(state);
        }
    }

    plugin_runtime_t(const plugin_runtime_t&)            = delete;
    plugin_runtime_t& operator=(const plugin_runtime_t&) = delete;
    plugin_runtime_t(plugin_runtime_t&&)                 = default;
    plugin_runtime_t& operator=(plugin_runtime_t&&)      = default;
};

void load_plugins();
