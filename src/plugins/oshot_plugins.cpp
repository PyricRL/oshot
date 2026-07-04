#include <cstring>

#include "cache.hpp"
#include "config.hpp"
#include "fmt/format.h"
#include "oshot_plugin.h"
#include "plugin.hpp"
#include "screenshot_tool.hpp"
#include "util.hpp"

static size_t toml_get_array(const toml::array* arr, oshot_value_t** out, size_t max)
{
    if (!arr)
        return 0;

    size_t i = 0;
    for (const toml::node& el : *arr)
    {
        if (i == max)
            break;

        oshot_value_t& val = *out[i];
        switch (el.type())
        {
            case toml::node_type::string:
                val.kind = OSValueKind::OSHOT_VAL_STRING;
                val.s    = oshot_str_new(el.as_string()->get().c_str(), el.as_string()->get().length());
                break;

            case toml::node_type::boolean:
                val.kind = OSValueKind::OSHOT_VAL_BOOL;
                val.b    = el.as_boolean()->get();
                break;

            case toml::node_type::integer:
                val.kind = OSValueKind::OSHOT_VAL_INT64;
                val.i    = el.as_integer()->get();
                break;

            case toml::node_type::floating_point:
                val.kind = OSValueKind::OSHOT_VAL_DOUBLE;
                val.d    = el.as_floating_point()->get();
                break;

            default: continue;  // unsupported TOML type: skip, don't consume a slot
        }
        i++;
    }

    return i;
}

static std::string prefixed_key(const char* key)
{
    return g_current_plugin->config_prefix + key;
}

/* ------------------------------------------------------------------
 * ABI / identity
 * ------------------------------------------------------------------ */
uint32_t oshot_get_abi_version()
{
    return OSHOT_API_VERSION;
}

/* ------------------------------------------------------------------
 * oshot_str_t lifecycle
 * ------------------------------------------------------------------ */
oshot_str_t oshot_str_new(const char* str, size_t len)
{
    oshot_str_t r;
    r.p = reinterpret_cast<char*>(malloc(len));
    if (r.p)
        memcpy(const_cast<char*>(r.p), str, len);
    r.len = r.p ? len : 0;
    return r;
}

void oshot_str_free(oshot_str_t* str)
{
    free((void*)str->p);
    str->p   = nullptr;
    str->len = 0;
}

void oshot_value_array_free(oshot_value_t* arr, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (arr[i].kind == OSValueKind::OSHOT_VAL_STRING)
            oshot_str_free(&arr[i].s);
}

/* ------------------------------------------------------------------
 * Logging
 * ------------------------------------------------------------------ */
void oshot_log(const OSLogLevel lvl, oshot_str_t str)
{
    const std::string& out = fmt::format("{}: {}", g_current_plugin->plugin->id, std::string_view(str.p, str.len));
    switch (lvl)
    {
        case OSLogLevel::OSHOT_LOG_DEBUG: spdlog::debug("{}", out); break;
        case OSLogLevel::OSHOT_LOG_INFO:  spdlog::info("{}", out); break;
        case OSLogLevel::OSHOT_LOG_WARN:  spdlog::warn("{}", out); break;
        case OSLogLevel::OSHOT_LOG_ERROR: spdlog::error("{}", out); break;
    }
}

void oshot_debug(oshot_str_t str)
{
    oshot_log(OSLogLevel::OSHOT_LOG_DEBUG, std::move(str));
}

void oshot_logs(OSLogLevel lvl, const char* str)
{
    oshot_str_t s = oshot_str_new(str, strlen(str));
    oshot_log(lvl, s);
    oshot_str_free(&s);
}

void oshot_debugs(const char* str)
{
    oshot_str_t s = oshot_str_new(str, strlen(str));
    oshot_debug(s);
    oshot_str_free(&s);
}

/* ------------------------------------------------------------------
 * Config
 * ------------------------------------------------------------------ */

// ---------------------
// Getter
// ---------------------
template <typename T>
static T get_config_value(const char* key, T fallback)
{
    return g_config->GetValue<T>(prefixed_key(key), fallback);
}

oshot_str_t oshot_config_get_string(const char* key, oshot_str_t fallback)
{
    const std::string& str = get_config_value(key, std::string(fallback.p, fallback.len));
    return oshot_str_new(str.c_str(), str.length());
}

bool oshot_config_get_bool(const char* key, bool fallback)
{
    return get_config_value<bool>(key, fallback);
}

int64_t oshot_config_get_int64(const char* key, int64_t fallback)
{
    return get_config_value<int64_t>(key, fallback);
}

double oshot_config_get_double(const char* key, double fallback)
{
    return get_config_value<double>(key, fallback);
}

size_t oshot_config_get_array(const char* key, oshot_value_t** out, size_t max)
{
    return toml_get_array(g_config->GetValueArray(prefixed_key(key)), out, max);
}

// ---------------------
// Setter
// ---------------------
template <typename T>
static void set_config_value(const char* key, T val)
{
    g_config->SetValue<T>(prefixed_key(key), val);
}

void oshot_config_set_string(const char* key, const oshot_str_t* val)
{
    set_config_value(key, std::string(val->p, val->len));
}

void oshot_config_set_bool(const char* key, bool val)
{
    set_config_value<bool>(key, val);
}

void oshot_config_set_int64(const char* key, int64_t val)
{
    set_config_value<int64_t>(key, val);
}

void oshot_config_set_double(const char* key, double val)
{
    set_config_value<double>(key, val);
}

void oshot_config_set_value(const char* key, const oshot_value_t* val)
{
    if (val->kind < OSHOT_VAL_STRING || val->kind > OSHOT_VAL_DOUBLE)
    {
        oshot_logs(OSLogLevel::OSHOT_LOG_ERROR, "Failed to set config value: unknown kind");
        return;
    }

    switch (val->kind)
    {
        case OSValueKind::OSHOT_VAL_STRING:
            if (!val->s.p)
                oshot_logs(OSLogLevel::OSHOT_LOG_ERROR, "Failed to set config value: string has null pointer");
            else
                g_config->SetValue<std::string>(prefixed_key(key), std::string(val->s.p, val->s.len));
            break;

        case OSValueKind::OSHOT_VAL_INT64: g_config->SetValue<int64_t>(prefixed_key(key), val->i); break;

        case OSValueKind::OSHOT_VAL_DOUBLE: g_config->SetValue<double>(prefixed_key(key), val->d); break;

        case OSValueKind::OSHOT_VAL_BOOL: g_config->SetValue<bool>(prefixed_key(key), val->b); break;
    }
}

/* ------------------------------------------------------------------
 * Cache
 * ------------------------------------------------------------------ */

// ---------------------
// Getter
// ---------------------
template <typename T>
static T get_cache_value(const char* key, T fallback)
{
    return g_cache->GetValue<T>(prefixed_key(key), fallback);
}

oshot_str_t oshot_cache_get_string(const char* key, oshot_str_t fallback)
{
    const std::string& str = get_cache_value(key, std::string(fallback.p, fallback.len));
    return oshot_str_new(str.c_str(), str.length());
}

bool oshot_cache_get_bool(const char* key, bool fallback)
{
    return get_cache_value<bool>(key, fallback);
}

int64_t oshot_cache_get_int64(const char* key, int64_t fallback)
{
    return get_cache_value<int64_t>(key, fallback);
}

double oshot_cache_get_double(const char* key, double fallback)
{
    return get_cache_value<double>(key, fallback);
}

size_t oshot_cache_get_array(const char* key, oshot_value_t** out, size_t max)
{
    return toml_get_array(g_cache->GetValueArray(prefixed_key(key)), out, max);
}

// ---------------------
// Setter
// ---------------------
template <typename T>
static void set_cache_value(const char* key, T val)
{
    g_cache->SetValue<T>(prefixed_key(key), val);
}

void oshot_cache_set_string(const char* key, const oshot_str_t* val)
{
    set_cache_value(key, std::string(val->p, val->len));
}

void oshot_cache_set_bool(const char* key, bool val)
{
    set_cache_value<bool>(key, val);
}

void oshot_cache_set_int64(const char* key, int64_t val)
{
    set_cache_value<int64_t>(key, val);
}

void oshot_cache_set_double(const char* key, double val)
{
    set_cache_value<double>(key, val);
}

void oshot_cache_set_value(const char* key, const oshot_value_t* val)
{
    if (val->kind < OSHOT_VAL_STRING || val->kind > OSHOT_VAL_DOUBLE)
    {
        oshot_logs(OSLogLevel::OSHOT_LOG_ERROR, "Failed to set cache value: unknown kind");
        return;
    }

    switch (val->kind)
    {
        case OSValueKind::OSHOT_VAL_STRING:
            if (!val->s.p)
                oshot_logs(OSLogLevel::OSHOT_LOG_ERROR, "Failed to set cache value: string has null pointer");
            else
                g_cache->SetValue<std::string>(prefixed_key(key), std::string(val->s.p, val->s.len));
            break;

        case OSValueKind::OSHOT_VAL_INT64: g_cache->SetValue<int64_t>(prefixed_key(key), val->i); break;

        case OSValueKind::OSHOT_VAL_DOUBLE: g_cache->SetValue<double>(prefixed_key(key), val->d); break;

        case OSValueKind::OSHOT_VAL_BOOL: g_cache->SetValue<bool>(prefixed_key(key), val->b); break;
    }
}

/* ------------------------------------------------------------------
 * ImGui-bound text buffers
 * ------------------------------------------------------------------ */
bool oshot_get_text(const char* imgui_id, oshot_str_t* ret)
{
    auto& t = g_ss_tool.GetImGuiIDTexts();
    auto  p = t.find(imgui_id);
    if (p == t.end())
        return false;

    ret->p = static_cast<const char*>(std::malloc(p->second->length()));
    if (!ret->p)
        return false;

    std::memcpy(const_cast<char*>(ret->p), p->second->c_str(), p->second->length());
    ret->len = p->second->length();
    return true;
}

void oshot_set_text(const char* imgui_id, const oshot_str_t value)
{
    auto& t  = g_ss_tool.GetImGuiIDTexts();
    auto  it = t.find(imgui_id);
    if (it == t.end())
        return;  // unknown id

    it->second->assign(value.p, value.len);
}

/* ------------------------------------------------------------------
 * Capture acquisition
 * ------------------------------------------------------------------ */
oshot_capture_t oshot_get_capture(void)
{
    const capture_result_t& cap = g_ss_tool.GetFinalImage();

    oshot_capture_t ret{};  // zero-inits

    if (cap.data.empty() || cap.w <= 0 || cap.h <= 0)
        return ret;  // plugin must check ret.rgba == nullptr before use

    ret.rgba = static_cast<uint8_t*>(std::malloc(cap.data.size()));
    if (!ret.rgba)
        return ret;

    std::memcpy(ret.rgba, cap.data.data(), cap.data.size());
    ret.w = cap.w;
    ret.h = cap.h;
    return ret;
}

void oshot_capture_free(oshot_capture_t* cap)
{
    std::free(cap->rgba);
    cap->rgba = nullptr;
    cap->w = cap->h = 0;
}
