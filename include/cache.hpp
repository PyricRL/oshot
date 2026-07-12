#ifndef _CACHE_HPP_
#define _CACHE_HPP_

#include <string>
#include <array>

#include "toml_api.hpp"
#include "util.hpp"

// util.hpp
std::string expand_var(std::string ret);

enum class CacheEntry
{
    AnnColor,
    ImgSavePath,
    COUNT
};

class Cache : public TomlAPI
{
public:
    Cache(const std::string& cache_dir);
    ~Cache();

    Result<> LoadCacheFile();

    const std::string& GetCacheDirPath() const { return m_cache_dir_path; }

    using TomlAPI::GetValue;
    using TomlAPI::SetValue;

    // CacheEntry convenience overloads are the ONLY thing Cache needs to add now
    template <typename T>
    T GetValue(CacheEntry e, const T& fallback, bool dont_expand_var = false)
    {
        return GetValue<T>(mk_cache_entries.at(idx(e)), fallback, dont_expand_var);
    }

    template <typename T>
    void SetValue(CacheEntry e, const T& value)
    {
        SetValue<T>(mk_cache_entries.at(idx(e)), value);
    }

protected:
    std::string BuildKey(const std::string_view key) const override { return fmt::format("cache.{}", key); }

private:
    static constexpr const char* mk_file_path = "cache.toml";

    std::string m_cache_dir_path;

    static constexpr std::array<std::string_view, idx(CacheEntry::COUNT)> mk_cache_entries = {
        "default-color-picker-color",
        "last-saved-dir"
    };
};

extern std::unique_ptr<Cache> g_cache;

#endif  // !_CACHE_HPP_
