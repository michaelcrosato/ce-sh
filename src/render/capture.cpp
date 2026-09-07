#include "render/capture.h"

#include "core/json_writer.h"
#include "core/log.h"
#include "platform/files.h"

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

    JsonWriter j;
    j.BeginObject();
    j.Field("scene", meta.scene);
    j.Field("view", meta.view);
    j.Field("diagnosticView", true);
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

}  // namespace lc
