#ifndef _CONFIG_HPP_
#define _CONFIG_HPP_

#include <filesystem>
#include <memory>
#include <unordered_map>

#include "fmt/format.h"
#include "toml_api.hpp"
#include "util.hpp"

class Config : public TomlAPI
{
public:
    // Create .config directories and files and load the config file (args or default)
    Config(const std::filesystem::path& configFile, const std::filesystem::path& configDir);

    // Variables of config file in [default] table.
    // They can be overwritten from CLI arguments
    struct config_file_t
    {
        // Since we package the eng.traineddata file on Windows/MacOS,
        // because the user may not know how to download one or doesn't want to,
        // just for out-of-box experience sake, let's use the relative
        // ./models directory for the OCR models.
#ifdef __linux__
        std::string ocr_path = "/usr/share/tessdata/";
#else
        std::string ocr_path = "./models";
#endif
        std::string ocr_get_repo     = "tesseract-ocr/tessdata";
        std::string ocr_model        = "eng";
        std::string theme_style      = "auto";
        std::string theme_file_path  = "theme.toml";
        std::string image_out_fmt    = "oshot_{:%F_%H-%M}";
        int         delay            = 0;
        bool        allow_out_edit   = false;
        bool        real_full_screen = false;
        bool        show_text_tools  = true;
        bool        enable_vsync     = true;
        bool        render_anns      = true;
        bool        pref_conf_to_env = false;
        bool        ctrl_c_copy_img  = true;

        std::vector<std::string> fonts;

        bool operator==(const config_file_t&) const = default;
    } File;

    // Only from CLI arguments
    // Or ImGUI window
    struct runtime_settings_t
    {
        std::string source_file;
        int         preferred_psm    = 0;
        bool        enable_handles   = true;
        bool        only_launch_tray = false;
        bool        only_launch_gui  = false;
#if DEBUG || (defined(_WIN32) && WINDOWS_CMD)
        bool debug_print = true;
#else
        bool debug_print = false;
#endif
        bool operator==(const runtime_settings_t&) const = default;
    } Runtime;

    struct theme_overrides_t
    {
        std::unordered_map<std::string, std::string> colors;  // ImGuiCol name -> "#RRGGBB[AA]"

        float window_rounding = -1.f;
        float frame_rounding  = -1.f;
        float grab_rounding   = -1.f;
        float tab_rounding    = -1.f;
        float window_border   = -1.f;
        float frame_border    = -1.f;

        // custom oshot specific theme options
        bool smooth_animations = false;

        bool operator==(const theme_overrides_t&) const = default;
    } theme_overrides;

    /**
     * Load config file and parse every config variables
     * @param filename The config file path
     */
    void LoadConfigFile(const std::string& filename);

    /**
     * Parse the theme file (aka "theme.toml")
     *  @param filename The directory of the theme file
     */
    void LoadThemeFile(const std::string& filename);

    /**
     * Generate a config file
     * @param filename The config file path
     * @param force Overwrite without asking
     */
    void GenerateConfig(const std::string& filename, const bool force = false);

    /**
     * Generate a theme file
     * @param filename The theme file path
     * @param force Overwrite without asking
     */
    void GenerateTheme(const std::string& filename, const bool force = false);

    using TomlAPI::GetValue;
    using TomlAPI::SetValue;

    template <typename T>
    T GetThemeValue(const std::string_view value, const T& fallback, bool dont_expand_var = true) const
    {
        return m_theme.GetValue<T>(fmt::format("theme.{}", value), fallback, dont_expand_var);
    }

    template <typename T>
    T GetThemeStyleValue(const std::string_view value, const T& fallback, bool dont_expand_var = true) const
    {
        return m_theme.GetValue<T>(fmt::format("theme.style.{}", value), fallback, dont_expand_var);
    }

    uint32_t GetThemeColorValue(const std::string_view value,
                                const std::string&     fallback,
                                bool                   dont_expand_var = true) const
    {
        uint32_t out;
        hexstr_to_col(m_theme.GetValue<std::string>(fmt::format("theme.colors.{}", value), fallback, dont_expand_var),
                      out);
        return out;
    }

    const std::string& GetConfigPath() const { return m_config_path; }
    const std::string& GetThemePath() const { return m_theme_path; }
    const std::string& GetConfigDirPath() const { return m_config_dir_path; }

private:
    // Parsed theme from LoadThemeFile()
    TomlAPI m_theme;

    std::string m_config_path;
    std::string m_theme_path;
    std::string m_config_dir_path;
};

extern std::unique_ptr<Config> g_config;

void apply_imgui_theme();

// default config
inline constexpr std::string_view AUTOCONFIG = R"#([default]
# Default Path to where we'll use all the '.traineddata' models.
# The TESSDATA_PREFIX environment variable supersedes this.
ocr-path = "{}"

# Default OCR model.
ocr-model = "{}"

# GitHub repository from where we are going to
# download an OCR '.traineddata' model.
# The models must be on the root directory of the repository
ocr-repo-downlaod = "{}"

# Delay the app before acquiring a screenshot (in milliseconds)
# Doesn't affect if opening external image (i.e. -f flag)
delay = {}

# On some desktop environments (e.g. MATE), the compositor may cause
# the capture window to look grainy or pixelated. Enabling this uses exclusive
# fullscreen mode which bypasses the compositor and fixes it.
# Downside: the window may briefly take over the display on some setups.
real-full-screen = {}

# Controls vertical sync (VSync). When enabled, the capture window renders in sync
# with your monitor's refresh rate, thus being smoother visually but uses slightly more CPU/GPU.
# Disable if the overlay feels sluggish or unresponsive.
vsync = {}

# Allow the extracted output to be editable.
allow-text-edit = {}

# Display the text tools (OCR, Bar/QR code scan) at startup.
show-text-tools = {}

# Prefer using config variables over environment variable.
config-over-env = {}

# Consider annotations when scanning (true)
# or only when saving the selection (false).
annotations-in-text-tools = {}

# Copy image shortcut to use.
# true: CTRL+C
# false: CTRL+SHIFT+C
ctrl-c-copy-img = {}

# Fonts to use for the application. Can be an absolute path, or just a name.
# You can combine multiple fonts for multiple language support.
# for example, using "Roboto-Regular.ttf" and "RobotoCJK-Regular.ttc" for Chinese, Japanese, and Korean support alongside English support.
# If empty, or non-existent (or commented out), oshot will use the default font for ImGUI.
fonts = [{}]

# Format of the output image filename when saving.
# The .png extension is appended automatically.
# Uses {{fmt}} chrono specifiers. NOTE: 
#    the colon inside {{}} is required: {{:%F}} correct, {{%F}} will error.
#
# Default: "oshot_{{:%F_%H-%M}}"
image-out-fmt = "{}"

# Base UI theme: "auto" (follow OS dark/light), "dark", "light", or "classic".
# Fine-grained overrides live in theme.toml.
theme = "{}"

# Path to a theme file. Absolute or relative to this config's directory.
# Delete or comment out to use only the base theme above.
theme-file = "{}"
)#";

inline constexpr std::string_view AUTOTHEME = (R"(
# Drop this next to config.toml or point theme-file at its path.
# All sections and keys are optional — omit anything you don't want to override.

[theme]
smooth-animations = false

# ---------------------------------------------------------------
# Rounding (pixels, 0 = sharp corners, max ~12)
# ---------------------------------------------------------------
[theme.style]
window-rounding = 8.0
frame-rounding  = 4.0
grab-rounding   = 4.0
tab-rounding    = 4.0

# Border width in pixels. 0 = none, 1 = thin line.
window-border = 1.0
frame-border  = 0.0

# ---------------------------------------------------------------
# Color overrides
# Format: "#RRGGBBAA"
# Only the entries you list here are overridden;
# everything else falls back to the base theme.
#
# Full list of valid names:
#   https://github.com/ocornut/imgui/blob/master/imgui.cpp
#   (search for "GetStyleColorName")
# ---------------------------------------------------------------
[theme.colors]
# --- Text ---
Text         = "#cdd6f4FF"
TextDisabled = "#6c7086FF"

# --- Backgrounds ---
WindowBg       = "#1e1e2eFF"
ChildBg        = "#181825FF"
PopupBg        = "#1e1e2eFF"
FrameBg        = "#313244FF"
FrameBgHovered = "#45475aFF"
FrameBgActive  = "#585b70FF"
MenuBarBg      = "#181825FF"

# --- Title bar ---
TitleBg       = "#181825FF"
TitleBgActive = "#313244FF"

# --- Borders ---
Border       = "#585b70FF"
BorderShadow = "#00000000"

# --- Scrollbar ---
ScrollbarBg          = "#181825FF"
ScrollbarGrab        = "#585b70FF"
ScrollbarGrabHovered = "#6c7086FF"
ScrollbarGrabActive  = "#7f849cFF"

# --- Buttons ---
Button        = "#313244FF"
ButtonHovered = "#45475aFF"
ButtonActive  = "#585b70FF"

# --- Headers (selectables, tree nodes, collapsing headers) ---
Header        = "#313244FF"
HeaderHovered = "#45475aFF"
HeaderActive  = "#585b70FF"

# --- Sliders / checkmarks ---
CheckMark        = "#cba6f7FF"
SliderGrab       = "#cba6f7FF"
SliderGrabActive = "#b4befeff"

# --- Tabs ---
Tab         = "#313244FF"
TabHovered  = "#cba6f7FF"
TabSelected = "#45475aFF"

# --- Misc ---
Separator         = "#585b70FF"
ResizeGrip        = "#cba6f7FF"
ResizeGripHovered = "#cba6f7FF"
ResizeGripActive  = "#cba6f7FF"
PlotHistogram     = "#2ba2f0FF"  # original: "#e6b300FF"
)");

inline constexpr std::string_view oshot_help = (R"(Usage: oshot [OPTIONS]...
Lightweight Screenshot tool to extract text on the fly.

GENERAL OPTIONS:
    -h, --help                  Print this help menu.
    -V, --version               Print version and other infos about the build.
    -f, --source <PATH>         Path to the image to use as background (use '-' for reading from stdin).
    -C, --config <PATH>         Path to the config file to use (default: ~/.config/oshot/config.toml).
    -O, --override <OPTION>     Override a config option (e.g "delay=200", "default.ocr-model='jpn'").
    -d, --delay <MILLIS>        Delay the app before acquiring the screenshot by milliseconds.
                                Won't affect if using the -f flag

    -g, --gui                   Only launch the GUI.
    -t, --tray                  Only launch system tray.
    --debug                     Print debug statments.
    --gen-config [<PATH>]       Generate default config file. If PATH is omitted, saves to default location.
                                Prompts before overwriting.
)");

#endif  // !_CONFIG_HPP_
