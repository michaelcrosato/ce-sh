#include "render/capture.h"

#include "core/json_writer.h"
#include "core/log.h"
#include "platform/files.h"

#include <algorithm>

namespace lc {

bool WriteCapture(const std::filesystem::path& dir, const std::string& baseName, const CaptureImages& images,
                  const CaptureMetadata& meta) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::filesystem::path pngPath = dir / (baseName + ".png");
    const std::filesystem::path pfmPath = dir / (baseName + ".pfm");
    const std::filesystem::path jsonPath = dir / (baseName + ".json");

    bool ok = files::WriteBinaryFile(pngPath, EncodePng(images.display));
    ok = files::WriteBinaryFile(pfmPath, EncodePfm(images.linear)) && ok;
    const bool hasRaw = !images.rawMean.pixels.empty();
    const std::filesystem::path rawPath = dir / (baseName + "_raw.pfm");
    if (hasRaw) {
        ok = files::WriteBinaryFile(rawPath, EncodePfm(images.rawMean)) && ok;  // Mean of the recomposed raw samples (denoised mode).
    }

    JsonWriter j;
    j.BeginObject();
    j.Field("scene", meta.scene);
    j.Field("view", meta.view);
    j.Field("mode", meta.mode);
    j.Field("strategy", meta.strategy);
    j.Field("diagnosticView", meta.mode == "diag");
    j.Field("emitterCount", meta.emitterCount);
    j.Field("linearIsRadiance", meta.mode != "diag");
    j.Field("frameIndex", meta.frameIndex);
    j.Field("sampleIndex", meta.sampleIndex);
    j.Field("seed", meta.seed);
    j.Field("pathDepth", meta.pathDepth);
    j.Field("exposure", meta.exposure);
    j.Key("renderSize");
    j.BeginArray();
    j.Value(meta.renderWidth);
    j.Value(meta.renderHeight);
    j.EndArray();
    j.Key("outputSize");
    j.BeginArray();
    j.Value(meta.outputWidth);
    j.Value(meta.outputHeight);
    j.EndArray();
    j.Field("reconstruction", meta.reconstruction);
    j.Field("framesSinceHistoryReset", images.framesSinceReset);
    j.Field("historyResets", meta.historyResets);
    j.Field("adapter", meta.adapter);
    j.Field("driver", meta.driver);
    j.Field("buildCommit", meta.buildCommit);
    j.Field("buildDirty", meta.buildDirty);
    j.Field("buildConfig", meta.buildConfig);
    j.Field("timestamp", meta.timestamp);
    j.Field("instanceCount", meta.instanceCount);
    j.Field("triangleInstances", meta.triangleInstances);
    j.Key("files");
    j.BeginObject();
    j.Field("display", pngPath.filename().string());
    j.Field("linear", pfmPath.filename().string());
    if (hasRaw) j.Field("rawMean", rawPath.filename().string());
    j.EndObject();
    j.Key("gpuTimingsMs");
    j.BeginObject();
    for (const gfx::TimerResult& t : meta.timings) {
        j.Field(t.name, t.milliseconds);
    }
    j.EndObject();
    j.EndObject();
    ok = files::WriteTextFile(jsonPath, j.Text() + "\n") && ok;

    if (ok) {
        log::Info("Capture written: {} (+ .pfm, .json) {}x{}", pngPath.string(), images.width, images.height);
    }
    return ok;
}

ImageRgba8 CropImage(const ImageRgba8& image, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) {
    ImageRgba8 out;
    if (image.width == 0 || image.height == 0 || x >= image.width || y >= image.height) {
        return out;
    }
    const std::uint32_t w = std::min(width == 0 ? image.width : width, image.width - x);
    const std::uint32_t h = std::min(height == 0 ? image.height : height, image.height - y);
    out.width = w;
    out.height = h;
    out.pixels.resize(static_cast<std::size_t>(w) * h * 4);
    for (std::uint32_t row = 0; row < h; ++row) {
        const std::uint8_t* src = image.pixels.data() + (static_cast<std::size_t>(y + row) * image.width + x) * 4;
        std::copy(src, src + static_cast<std::size_t>(w) * 4, out.pixels.data() + static_cast<std::size_t>(row) * w * 4);
    }
    return out;
}

bool WriteSequenceFrame(const std::filesystem::path& dir, const std::string& fileName, const ImageRgba8& image) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return files::WriteBinaryFile(dir / fileName, EncodePng(image));
}

}  // namespace lc
