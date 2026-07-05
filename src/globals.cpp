#include "cache.hpp"
#include "clipboard.hpp"
#include "config.hpp"
#ifndef DISABLE_PLUGINS
#  include "plugin.hpp"
#endif
#include "screenshot_tool.hpp"
#include "util.hpp"

// Extern variables declariaions
std::deque<std::string> g_dropped_paths;
std::unique_ptr<Config> g_config;
std::unique_ptr<Cache>  g_cache;
ScreenshotTool          g_ss_tool;
bool                    g_is_systray = false;
int                     g_scr_w{}, g_scr_h{};
Clipboard               g_clipboard(SessionType::Unknown);

#ifndef DISABLE_PLUGINS
std::unordered_map<std::string, plugin_runtime_t> g_plugins;
plugin_runtime_t*                                 g_current_plugin;
#endif
