#pragma once

/**
 * @file templatemanager.h
 * @brief Manages configurable command-line templates for external tools.
 *
 * All ffmpeg, ffprobe, and dovi_tool commands are stored as templates with
 * placeholders (e.g. `{input}`, `{crop_w}`). This allows users to modify
 * encoding parameters without recompiling the application.
 *
 * Templates are persisted via QSettings and restored on startup.
 */

#include <QString>
#include <QMap>
#include <QSettings>

/**
 * @class TemplateManager
 * @brief Stores and resolves command-line templates for encoding workflows.
 *
 * Each template is identified by a string key and contains a command line
 * with `{placeholder}` tokens. The resolve() method substitutes these
 * placeholders with actual values at runtime.
 *
 * Separate templates exist for:
 * - 4K UHD content (with HDR/Dolby Vision metadata handling)
 * - Full HD content (SDR, no metadata extraction needed)
 * - Each hardware encoder variant (Software, QuickSync, NVEnc, AMF)
 *
 * Templates are saved to and loaded from QSettings, so user modifications
 * persist across application restarts.
 */
class TemplateManager
{
public:
    /**
     * @brief Build ID (YYYYMMDD integer). When this changes, saved templates
     *        are considered outdated and replaced with new defaults.
     */
    static constexpr int BUILD_ID = 20260604;
    /**
     * @brief Supported hardware encoder types.
     */
    enum Encoder {
        Software,   ///< CPU-based libx265 encoder
        QuickSync,  ///< Intel QuickSync Video (hevc_qsv)
        NVEnc,      ///< NVIDIA hardware encoder (hevc_nvenc)
        AMF         ///< AMD Advanced Media Framework (hevc_amf)
    };

    /**
     * @brief Construct and load templates (defaults + user overrides).
     */
    TemplateManager();

    /**
     * @brief Retrieve a template by key.
     * @param key The template identifier (use KEY_* constants).
     * @return The template string, or empty if not found.
     */
    QString getTemplate(const QString &key) const;

    /**
     * @brief Set or override a template.
     * @param key The template identifier.
     * @param tmpl The new template string with {placeholders}.
     */
    void setTemplate(const QString &key, const QString &tmpl);

    /**
     * @brief Persist all templates to QSettings.
     */
    void save();

    /**
     * @brief Export all templates to a JSON file.
     * @param filePath Path to write the JSON file.
     * @return True on success.
     */
    bool exportToJson(const QString &filePath) const;

    /**
     * @brief Import templates from a JSON file.
     * @param filePath Path to the JSON file.
     * @return True on success.
     */
    bool importFromJson(const QString &filePath);

    /**
     * @brief Load user-modified templates from QSettings (overrides defaults).
     */
    void load();

    /**
     * @brief Substitute placeholders in a template with actual values.
     * @param tmpl The template string containing `{key}` placeholders.
     * @param vars Map of placeholder names to their replacement values.
     * @return The resolved command string ready for execution.
     *
     * Example:
     * @code
     * QMap<QString, QString> vars;
     * vars["input"] = "/path/to/movie.mkv";
     * vars["crop_w"] = "3840";
     * QString cmd = TemplateManager::resolve(tmpl, vars);
     * @endcode
     */
    static QString resolve(const QString &tmpl, const QMap<QString, QString> &vars);

    // --- Template Keys ---

    static constexpr const char* KEY_CROPDETECT = "cropdetect";       ///< ffmpeg cropdetect for SDR/FHD
    static constexpr const char* KEY_CROPDETECT_HDR = "cropdetect_hdr"; ///< ffmpeg cropdetect for HDR/4K
    static constexpr const char* KEY_PROBE_DOVI = "probe_dovi";       ///< ffprobe Dolby Vision detection
    static constexpr const char* KEY_EXTRACT_RPU = "extract_rpu";     ///< Extract RPU + convert Profile 7→8.1 + reset crop
    static constexpr const char* KEY_ENCODE_SW = "encode_sw";         ///< 4K UHD software encode (libx265)
    static constexpr const char* KEY_ENCODE_QSV = "encode_qsv";       ///< 4K UHD QuickSync encode (zero-copy)
    static constexpr const char* KEY_ENCODE_NVENC = "encode_nvenc";   ///< 4K UHD NVEnc encode (zero-copy)
    static constexpr const char* KEY_ENCODE_AMF = "encode_amf";       ///< 4K UHD AMD AMF encode
    static constexpr const char* KEY_ENCODE_SW_FHD = "encode_sw_fhd";     ///< FullHD software encode (SDR)
    static constexpr const char* KEY_ENCODE_QSV_FHD = "encode_qsv_fhd";   ///< FullHD QuickSync encode (SDR)
    static constexpr const char* KEY_ENCODE_NVENC_FHD = "encode_nvenc_fhd"; ///< FullHD NVEnc encode (SDR)
    static constexpr const char* KEY_ENCODE_AMF_FHD = "encode_amf_fhd";   ///< FullHD AMD AMF encode (SDR)
    static constexpr const char* KEY_ENCODE_QSV_SWDEC = "encode_qsv_swdec";   ///< QSV encode with SW decode (VC-1)
    static constexpr const char* KEY_ENCODE_NVENC_SWDEC = "encode_nvenc_swdec"; ///< NVEnc encode with SW decode (VC-1)
    static constexpr const char* KEY_ENCODE_AMF_SWDEC = "encode_amf_swdec";   ///< AMF encode with SW decode (VC-1)
    static constexpr const char* KEY_INJECT_DOVI = "inject_dovi";     ///< Inject RPU into encoded HEVC stream
    static constexpr const char* KEY_MUXFINAL = "mux_final";          ///< Final mux: DV video + original audio/subs

private:
    QMap<QString, QString> m_templates; ///< All registered templates keyed by name.

    /** @brief Initialize built-in default templates for all workflow steps. */
    void initDefaults();
};
