#include "game/replay.h"

#include "core/json_reader.h"
#include "core/json_writer.h"
#include "core/log.h"

#include <format>
#include <fstream>
#include <sstream>

namespace lc::game {

InputFrame Replay::InputAt(std::uint64_t tick) const {
    for (const InputSegment& s : segments) {
        if (tick >= s.fromTick && tick <= s.toTick) {
            return s.input;
        }
        if (s.fromTick > tick) {
            break;
        }
    }
    return InputFrame{};
}

void Replay::Record(std::uint64_t tick, const InputFrame& input) {
    if (!segments.empty()) {
        InputSegment& last = segments.back();
        if (tick == last.toTick + 1 && last.input == input) {
            last.toTick = tick;
            return;
        }
        if (tick <= last.toTick) {
            log::Warn("replay: recording tick {} out of order (last {}); ignored", tick, last.toTick);
            return;
        }
    }
    if (input.IsNeutral() && segments.empty()) {
        return;  // Leading neutral input needs no segment.
    }
    if (input.IsNeutral()) {
        // Gaps are neutral by definition; only record non-neutral input.
        return;
    }
    segments.push_back(InputSegment{tick, tick, input});
}

std::uint64_t Replay::LastTick() const {
    std::uint64_t last = 0;
    for (const InputSegment& s : segments) last = std::max(last, s.toTick);
    for (const ReplayCheck& c : checks) last = std::max(last, c.tick);
    return last;
}

namespace {

void WriteVec3(JsonWriter& j, const char* key, const math::Vec3& v) {
    j.Key(key);
    j.BeginArray();
    j.Value(static_cast<double>(v.x));
    j.Value(static_cast<double>(v.y));
    j.Value(static_cast<double>(v.z));
    j.EndArray();
}

bool ReadVec3(const json::Value* v, math::Vec3& out) {
    if (v == nullptr || !v->IsArray() || v->Size() != 3) return false;
    for (std::size_t i = 0; i < 3; ++i) {
        if (!v->At(i)->IsNumber()) return false;
    }
    out = {static_cast<float>(v->At(0)->AsNumber()), static_cast<float>(v->At(1)->AsNumber()), static_cast<float>(v->At(2)->AsNumber())};
    return true;
}

bool ReadVec2(const json::Value* v, math::Vec2& out) {
    if (v == nullptr || !v->IsArray() || v->Size() != 2) return false;
    if (!v->At(0)->IsNumber() || !v->At(1)->IsNumber()) return false;
    out = {static_cast<float>(v->At(0)->AsNumber()), static_cast<float>(v->At(1)->AsNumber())};
    return true;
}

}  // namespace

std::string Replay::ToJson() const {
    JsonWriter j;
    j.BeginObject();
    j.Field("version", version);
    j.Field("scene", scene);
    j.Field("tickRate", tickRate);
    j.Field("seed", seed);
    j.Key("segments");
    j.BeginArray();
    for (const InputSegment& s : segments) {
        j.BeginObject();
        j.Field("from", static_cast<std::uint64_t>(s.fromTick));
        j.Field("to", static_cast<std::uint64_t>(s.toTick));
        j.Field("moveX", static_cast<double>(s.input.moveX));
        j.Field("moveZ", static_cast<double>(s.input.moveZ));
        j.Field("lookDx", static_cast<double>(s.input.lookDx));
        j.Field("lookDy", static_cast<double>(s.input.lookDy));
        j.Field("sprint", s.input.sprint);
        j.Field("interact", s.input.interactPressed);
        j.Field("lamp", s.input.lampPressed);
        j.EndObject();
    }
    j.EndArray();
    j.Key("checks");
    j.BeginArray();
    for (const ReplayCheck& c : checks) {
        j.BeginObject();
        j.Field("tick", static_cast<std::uint64_t>(c.tick));
        j.Field("kind", c.kind);
        j.Field("description", c.description);
        if (c.point) WriteVec3(j, "point", *c.point);
        if (c.pixel) {
            j.Key("pixel");
            j.BeginArray();
            j.Value(static_cast<double>(c.pixel->x));
            j.Value(static_cast<double>(c.pixel->y));
            j.EndArray();
        }
        if (!c.entity.empty()) j.Field("entity", c.entity);
        if (c.otherPoint) WriteVec3(j, "otherPoint", *c.otherPoint);
        j.Field("minimum", static_cast<double>(c.minimum));
        j.Field("maximum", static_cast<double>(c.maximum));
        j.Field("tolerance", static_cast<double>(c.tolerance));
        j.Field("ratioFactor", static_cast<double>(c.ratioFactor));
        j.Field("halfSize", c.halfSize);
        if (c.kind == "motion") j.Field("reflected", c.reflected);
        if (c.kind == "trail_lag") {
            j.Field("maxLagFrames", c.maxLagFrames);
            j.Field("settleFraction", static_cast<double>(c.settleFraction));
            j.Field("fromFrame", static_cast<std::uint64_t>(c.fromFrame));
        }
        if (!c.state.empty()) j.Field("state", c.state);
        if (c.kind == "caught_count") j.Field("count", c.count);
        j.EndObject();
    }
    j.EndArray();
    j.EndObject();
    return j.Text() + "\n";
}

std::optional<Replay> Replay::FromJson(std::string_view text, std::string& error) {
    const json::ParseResult parsed = json::Parse(text);
    if (!parsed.value) {
        error = "replay JSON: " + parsed.error;
        return std::nullopt;
    }
    const json::Value& root = *parsed.value;
    if (!root.IsObject()) {
        error = "replay JSON: the document must be an object";
        return std::nullopt;
    }
    Replay r;
    r.version = static_cast<std::uint32_t>(root.NumberOr("version", 0));
    if (r.version != 1) {
        error = std::format("replay JSON: unsupported version {} (expected 1)", r.version);
        return std::nullopt;
    }
    r.scene = root.StringOr("scene", "");
    r.tickRate = static_cast<std::uint32_t>(root.NumberOr("tickRate", 60));
    r.seed = static_cast<std::uint32_t>(root.NumberOr("seed", 0));
    const std::string threat = root.StringOr("threat", "patrol");
    if (threat != "patrol" && threat != "hunt") {
        error = std::format("replay JSON: threat must be 'patrol' or 'hunt', not '{}'", threat);
        return std::nullopt;
    }
    r.hunt = threat == "hunt";
    if (r.tickRate == 0 || r.tickRate > 1000) {
        error = "replay JSON: tickRate must be within 1..1000";
        return std::nullopt;
    }
    if (const json::Value* segments = root.Get("segments"); segments != nullptr && segments->IsArray()) {
        std::uint64_t previousTo = 0;
        bool first = true;
        for (const json::Value& s : segments->Items()) {
            if (!s.IsObject()) {
                error = "replay JSON: segments must be objects";
                return std::nullopt;
            }
            InputSegment seg;
            seg.fromTick = static_cast<std::uint64_t>(s.NumberOr("from", 0));
            seg.toTick = static_cast<std::uint64_t>(s.NumberOr("to", static_cast<double>(seg.fromTick)));
            if (seg.toTick < seg.fromTick || (!first && seg.fromTick <= previousTo)) {
                error = std::format("replay JSON: segment {}..{} is not ordered after {}", seg.fromTick, seg.toTick, previousTo);
                return std::nullopt;
            }
            seg.input.moveX = static_cast<float>(s.NumberOr("moveX", 0.0));
            seg.input.moveZ = static_cast<float>(s.NumberOr("moveZ", 0.0));
            seg.input.lookDx = static_cast<float>(s.NumberOr("lookDx", 0.0));
            seg.input.lookDy = static_cast<float>(s.NumberOr("lookDy", 0.0));
            seg.input.sprint = s.BoolOr("sprint", false);
            seg.input.interactPressed = s.BoolOr("interact", false);
            seg.input.lampPressed = s.BoolOr("lamp", false);
            previousTo = seg.toTick;
            first = false;
            r.segments.push_back(seg);
        }
    }
    if (const json::Value* checks = root.Get("checks"); checks != nullptr && checks->IsArray()) {
        for (const json::Value& c : checks->Items()) {
            if (!c.IsObject()) {
                error = "replay JSON: checks must be objects";
                return std::nullopt;
            }
            ReplayCheck check;
            check.tick = static_cast<std::uint64_t>(c.NumberOr("tick", 0));
            check.kind = c.StringOr("kind", "");
            check.description = c.StringOr("description", check.kind);
            math::Vec3 point;
            if (ReadVec3(c.Get("point"), point)) check.point = point;
            math::Vec2 pixel;
            if (ReadVec2(c.Get("pixel"), pixel)) check.pixel = pixel;
            check.entity = c.StringOr("entity", "");
            math::Vec3 other;
            if (ReadVec3(c.Get("otherPoint"), other)) check.otherPoint = other;
            check.minimum = static_cast<float>(c.NumberOr("minimum", 1e-3));
            check.maximum = static_cast<float>(c.NumberOr("maximum", 1e-3));
            check.tolerance = static_cast<float>(c.NumberOr("tolerance", 1e-6));
            check.ratioFactor = static_cast<float>(c.NumberOr("ratioFactor", 1.2));
            check.halfSize = static_cast<std::uint32_t>(c.NumberOr("halfSize", 2));
            check.reflected = c.BoolOr("reflected", false);
            check.maxLagFrames = static_cast<std::uint32_t>(c.NumberOr("maxLagFrames", 6));
            check.settleFraction = static_cast<float>(c.NumberOr("settleFraction", 0.8));
            check.fromFrame = static_cast<std::uint64_t>(c.NumberOr("fromFrame", 0));
            check.state = c.StringOr("state", "");
            check.count = static_cast<std::uint32_t>(c.NumberOr("count", 0));
            bool known = false;
            for (const char* k : kReplayCheckKinds) known = known || check.kind == k;
            if (!known) {
                error = std::format("replay JSON: unknown check kind '{}'", check.kind);
                return std::nullopt;
            }
            if (check.kind == "trail_lag" && (check.settleFraction <= 0.0f || check.settleFraction > 1.0f || check.fromFrame > check.tick)) {
                error = std::format("replay JSON: trail_lag check '{}' needs settleFraction in (0, 1] and fromFrame <= tick", check.description);
                return std::nullopt;
            }
            if ((check.kind == "motion" || check.kind == "static_motion") && !check.point && !check.pixel) {
                error = std::format("replay JSON: {} check '{}' needs a point or pixel", check.kind, check.description);
                return std::nullopt;
            }
            if (check.kind == "motion" && check.entity.empty()) {
                error = std::format("replay JSON: motion check '{}' needs an entity", check.description);
                return std::nullopt;
            }
            if ((check.kind == "objective_state" || check.kind == "threat_state") && check.state.empty()) {
                error = std::format("replay JSON: {} check '{}' needs a state", check.kind, check.description);
                return std::nullopt;
            }
            if (check.kind == "player_near" && !check.point) {
                error = std::format("replay JSON: player_near check '{}' needs a point", check.description);
                return std::nullopt;
            }
            r.checks.push_back(check);
        }
    }
    return r;
}

std::optional<Replay> Replay::Load(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = std::format("cannot open replay file: {}", path.string());
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return FromJson(buffer.str(), error);
}

bool Replay::Save(const std::filesystem::path& path) const {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log::Error("cannot write replay file: {}", path.string());
        return false;
    }
    out << ToJson();
    return static_cast<bool>(out);
}

}  // namespace lc::game
