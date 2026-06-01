#include "templatemanager.h"

TemplateManager::TemplateManager()
{
    initDefaults();
    load();
}

void TemplateManager::initDefaults()
{
    // Crop detection: analyze ~3 minutes, skip first 2 min
    m_templates[KEY_CROPDETECT] =
        "{ffmpeg} -ss 300 -i {input} -t 600 -vf scale=in_range=limited:out_range=full,cropdetect=64:2:0 -an -f null - 2>&1";

    // Probe for Dolby Vision metadata
    m_templates[KEY_PROBE_DOVI] =
        "{ffprobe} -v quiet -select_streams v:0 -show_entries stream_side_data=dv_profile "
        "-of default=nw=1:nk=1 {input}";

    // Extract RPU from HEVC stream, convert Profile 7 -> 8.1 (mode 2),
    // and reset active area offsets (-c) so cropped output matches RPU
    m_templates[KEY_EXTRACT_RPU] =
        "{ffmpeg} -i {input} -c:v copy -bsf:v hevc_mp4toannexb -f hevc - | "
        "{dovi_tool} -m 2 -c extract-rpu - -o {rpu_file}";

    // Software x265 10-bit encode with crop + HDR metadata passthrough
    // Video-only output for DV pipeline (audio/subs added in final mux)
    m_templates[KEY_ENCODE_SW] =
        "{ffmpeg} -i {input} -map 0:v:0 "
        "-vf \"crop={crop_w}:{crop_h}:{crop_x}:{crop_y}\" "
        "-c:v libx265 -preset slow -crf {crf} -pix_fmt yuv420p10le "
        "-x265-params \"hdr10=1:repeat-headers=1:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc\" "
        "-an -sn {output}";

    // QuickSync HEVC 10-bit Zero-Copy encode (vpp_qsv for HW crop)
    // Video-only output for DV pipeline
    m_templates[KEY_ENCODE_QSV] =
        "{ffmpeg} -hwaccel qsv -hwaccel_output_format qsv -extra_hw_frames 64 -i {input} "
        "-vf \"vpp_qsv=cw={crop_w}:ch={crop_h}:cx={crop_x}:cy={crop_y},format=p010\" "
        "-map 0:v:0 "
        "-c:v hevc_qsv -profile:v main10 -preset slow "
        "-global_quality {crf} -look_ahead 1 -look_ahead_depth 60 "
        "-an -sn {output}";

    // NVENC HEVC 10-bit encode (hwdownload p010 + SW crop + re-upload)
    // Video-only output for DV pipeline
    m_templates[KEY_ENCODE_NVENC] =
        "{ffmpeg} -hwaccel cuda -hwaccel_output_format cuda -i {input} "
        "-vf \"hwdownload,format=p010,crop={crop_w}:{crop_h}:{crop_x}:{crop_y},hwupload_cuda\" "
        "-map 0:v:0 "
        "-c:v hevc_nvenc -preset p7 -tune hq -rc vbr -cq {crf} -b:v 0 "
        "-profile:v main10 "
        "-an -sn {output}";

    // AMD AMF HEVC 10-bit encode with crop
    // Video-only output for DV pipeline
    m_templates[KEY_ENCODE_AMF] =
        "{ffmpeg} -hwaccel d3d11va -i {input} "
        "-vf \"crop={crop_w}:{crop_h}:{crop_x}:{crop_y},format=p010\" "
        "-map 0:v:0 "
        "-c:v hevc_amf -profile:v main10 -quality quality "
        "-qp_i {crf} -qp_p {crf} "
        "-an -sn {output}";

    // Software x265 10-bit encode for FullHD (SDR, no HDR params)
    m_templates[KEY_ENCODE_SW_FHD] =
        "{ffmpeg} -i {input} -map 0:v:0 -map 0:a -map 0:s -map 0:t? -map_chapters 0 -map_metadata 0 "
        "-vf \"crop={crop_w}:{crop_h}:{crop_x}:{crop_y}\" "
        "-c:v libx265 -preset slow -crf {crf} -pix_fmt yuv420p10le "
        "-c:a copy -c:s copy -max_muxing_queue_size 9999 "
        "-tag:v hvc1 {output}";

    // QuickSync encode for FullHD (SDR)
    m_templates[KEY_ENCODE_QSV_FHD] =
        "{ffmpeg} -hwaccel qsv -hwaccel_output_format qsv -extra_hw_frames 64 -i {input} "
        "-vf \"vpp_qsv=cw={crop_w}:ch={crop_h}:cx={crop_x}:cy={crop_y},format=p010\" "
        "-map 0:v:0 -map 0:a -map 0:s -map 0:t? -map_chapters 0 -map_metadata 0 "
        "-c:v hevc_qsv -profile:v main10 -preset slow "
        "-global_quality {crf} -look_ahead 1 -look_ahead_depth 60 "
        "-c:a copy -c:s copy -max_muxing_queue_size 9999 "
        "-tag:v hvc1 {output}";

    // NVENC encode for FullHD (SDR)
    m_templates[KEY_ENCODE_NVENC_FHD] =
        "{ffmpeg} -hwaccel cuda -hwaccel_output_format cuda -i {input} "
        "-vf \"hwdownload,format=p010,crop={crop_w}:{crop_h}:{crop_x}:{crop_y},hwupload_cuda\" "
        "-map 0:v:0 -map 0:a -map 0:s -map 0:t? -map_chapters 0 -map_metadata 0 "
        "-c:v hevc_nvenc -preset p7 -tune hq -rc vbr -cq {crf} -b:v 0 "
        "-profile:v main10 "
        "-c:a copy -c:s copy -max_muxing_queue_size 9999 "
        "-tag:v hvc1 {output}";

    // AMF encode for FullHD (SDR)
    m_templates[KEY_ENCODE_AMF_FHD] =
        "{ffmpeg} -hwaccel d3d11va -i {input} "
        "-vf \"crop={crop_w}:{crop_h}:{crop_x}:{crop_y},format=p010\" "
        "-map 0:v:0 -map 0:a -map 0:s -map 0:t? -map_chapters 0 -map_metadata 0 "
        "-c:v hevc_amf -profile:v main10 -quality quality "
        "-qp_i {crf} -qp_p {crf} "
        "-c:a copy -c:s copy -max_muxing_queue_size 9999 "
        "-tag:v hvc1 {output}";

    // QSV encode with SW decode (for VC-1 sources that lack HW decode support)
    m_templates[KEY_ENCODE_QSV_SWDEC] =
        "{ffmpeg} -fflags +genpts -i {input} "
        "-vf \"crop={crop_w}:{crop_h}:{crop_x}:{crop_y},format=p010\" "
        "-map 0:v:0 -map 0:a -map 0:s -map 0:t? -map_chapters 0 -map_metadata 0 "
        "-c:v hevc_qsv -profile:v main10 -preset slow "
        "-global_quality {crf} -look_ahead 1 -look_ahead_depth 60 "
        "-c:a copy -c:s copy -max_muxing_queue_size 9999 "
        "-tag:v hvc1 {output}";

    // NVEnc encode with SW decode (for VC-1 sources)
    m_templates[KEY_ENCODE_NVENC_SWDEC] =
        "{ffmpeg} -fflags +genpts -i {input} "
        "-vf \"crop={crop_w}:{crop_h}:{crop_x}:{crop_y},format=p010\" "
        "-map 0:v:0 -map 0:a -map 0:s -map 0:t? -map_chapters 0 -map_metadata 0 "
        "-c:v hevc_nvenc -preset p7 -tune hq -rc vbr -cq {crf} -b:v 0 "
        "-profile:v main10 "
        "-c:a copy -c:s copy -max_muxing_queue_size 9999 "
        "-tag:v hvc1 {output}";

    // AMF encode with SW decode (for VC-1 sources)
    m_templates[KEY_ENCODE_AMF_SWDEC] =
        "{ffmpeg} -fflags +genpts -i {input} "
        "-vf \"crop={crop_w}:{crop_h}:{crop_x}:{crop_y},format=p010\" "
        "-map 0:v:0 -map 0:a -map 0:s -map 0:t? -map_chapters 0 -map_metadata 0 "
        "-c:v hevc_amf -profile:v main10 -quality quality "
        "-qp_i {crf} -qp_p {crf} "
        "-c:a copy -c:s copy -max_muxing_queue_size 9999 "
        "-tag:v hvc1 {output}";

    // Inject converted RPU into encoded HEVC stream
    m_templates[KEY_INJECT_DOVI] =
        "{dovi_tool} inject-rpu -i {encoded_hevc} --rpu-in {rpu_file} -o {injected_hevc}";

    // Final mux: combine DV-injected video with original audio/subs
    // mkvmerge handles raw HEVC timestamps automatically
    m_templates[KEY_MUXFINAL] =
        "{mkvmerge} -o {output} {injected_hevc} -D {input}";
}

QString TemplateManager::getTemplate(const QString &key) const
{
    return m_templates.value(key);
}

void TemplateManager::setTemplate(const QString &key, const QString &tmpl)
{
    m_templates[key] = tmpl;
}

void TemplateManager::save()
{
    QSettings settings;
    settings.beginGroup("Templates");
    settings.setValue("build_id", BUILD_ID);
    for (auto it = m_templates.cbegin(); it != m_templates.cend(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
}

void TemplateManager::load()
{
    QSettings settings;
    int savedBuild = settings.value("Templates/build_id", 0).toInt();
    if (savedBuild >= BUILD_ID) {
        // Saved templates are up-to-date, load them
        settings.beginGroup("Templates");
        for (const QString &key : settings.childKeys()) {
            if (key != "build_id")
                m_templates[key] = settings.value(key).toString();
        }
        settings.endGroup();
    }
    // else: defaults from initDefaults() remain, will be saved on next save()
}

QString TemplateManager::resolve(const QString &tmpl, const QMap<QString, QString> &vars)
{
    QString result = tmpl;
    for (auto it = vars.cbegin(); it != vars.cend(); ++it) {
        result.replace("{" + it.key() + "}", it.value());
    }
    return result;
}
