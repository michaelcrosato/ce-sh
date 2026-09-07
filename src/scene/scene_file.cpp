#include "scene/scene_file.h"

#include "core/json_reader.h"
#include "scene/primitives.h"

#include <cmath>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace lc {

using math::Mat4;
using math::Vec3;

namespace {

std::filesystem::path g_assetRoot;

// ---------------------------------------------------------------------------------------------
// Parsed document (validated before anything is built).

struct MaterialSpec {
    std::string id;
    Material material;
    std::string circuit;  // Emitters only.
};

struct CircuitSpec {
    std::string id;
    bool on = true;
};

struct AimSpec {
    std::string camera;
    float cameraHeight = 0.0f;
    std::string target;
    float targetHeight = 0.0f;
};

struct ObjectSpec {
    std::string id;
    std::string kind;  // slab, wall_opening, door_leaf, emitter_rect, box, quad.
    std::string material;
    Vec3 min, max;                    // slab, wall_opening.
    bool axisX = false;               // wall_opening.
    WallOpening opening;              // wall_opening.
    DoorLeafSpec leaf;                // door_leaf.
    float width = 0.0f, height = 0.0f;  // emitter_rect.
    Vec3 position, facing;            // emitter_rect.
    Vec3 half;                        // box (xyz), quad (xy in x, z).
    std::optional<Vec3> centre;       // box: static placement.
    std::optional<float> yaw;         // box: explicit orientation.
    std::optional<AimSpec> aim;       // box: derived orientation (mirror).
    bool collider = true;
    bool colliderGiven = false;       // The file stated 'collider' explicitly.
    bool placedByEntity = false;      // Set while resolving entities.
};

struct PathSpec {
    std::string id;
    std::vector<Vec3> points;
    float speed = 1.0f;
};

struct MarkerSpec {
    std::string id;
    Vec3 position;
    float yaw = 0.0f;
    bool hasYaw = false;
};

struct Document {
    std::string name;
    std::vector<MaterialSpec> materials;
    std::vector<CircuitSpec> circuits;
    std::vector<ObjectSpec> objects;
    std::vector<SocketSpec> sockets;
    std::vector<PathSpec> paths;
    std::vector<MarkerSpec> markers;
    // Entities.
    std::string playerStart;
    std::string playerTorso, playerHandLeft, playerHandRight;  // Optional body parts.
    std::string doorObject;
    std::string lampHousing, lampFace, lampMaterial, lampStartSocket, lampPlaceSocket;
    float lampFaceOffset = 0.101f;
    std::string threatBody, threatHead, threatPath, threatParkAt;
    std::string mirrorObject, mirrorCheckCamera, mirrorAimAt, hallFloor;
    // Objectives: the exit marker (M5). Other kinds are rejected until they exist.
    std::string exitMarker;
    float exitRadius = 0.8f;
};

class Reader {
public:
    Reader(std::vector<std::string>& errors, const SceneFileLimits& limits) : errors_(errors), limits_(limits) {}

    void Error(std::string message) { errors_.push_back(std::move(message)); }

    static bool IsIdentifier(const std::string& s) {
        if (s.empty() || s.size() > 64) return false;
        for (const char c : s) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            if (!ok) return false;
        }
        return true;
    }

    // Reads a unique identifier from `id`; registers it in `seen`.
    bool ReadId(const json::Value& v, const char* list, std::size_t index, std::set<std::string>& seen, std::string& out) {
        const json::Value* id = v.Get("id");
        if (id == nullptr || !id->IsString()) {
            Error(std::format("{}[{}]: missing string 'id'", list, index));
            return false;
        }
        out = id->AsString();
        if (!IsIdentifier(out)) {
            Error(std::format("{}[{}]: id '{}' is not a lowercase identifier [a-z0-9_]", list, index, out));
            return false;
        }
        if (!seen.insert(out).second) {
            Error(std::format("{}: duplicate id '{}'", list, out));
            return false;
        }
        return true;
    }

    bool ReadNumber(const json::Value& parent, const char* where, const char* key, float& out, bool required, float lo, float hi) {
        const json::Value* v = parent.Get(key);
        if (v == nullptr) {
            if (required) Error(std::format("{}: missing number '{}'", where, key));
            return false;
        }
        if (!v->IsNumber() || !std::isfinite(v->AsNumber())) {
            Error(std::format("{}: '{}' must be a finite number", where, key));
            return false;
        }
        const double d = v->AsNumber();
        if (d < lo || d > hi) {
            Error(std::format("{}: '{}' = {} is outside [{}, {}]", where, key, d, lo, hi));
            return false;
        }
        out = static_cast<float>(d);
        return true;
    }

    bool ReadVec(const json::Value& parent, const char* where, const char* key, std::size_t n, float* out, bool required, float lo, float hi) {
        const json::Value* v = parent.Get(key);
        if (v == nullptr) {
            if (required) Error(std::format("{}: missing {}-vector '{}'", where, n, key));
            return false;
        }
        if (!v->IsArray() || v->Size() != n) {
            Error(std::format("{}: '{}' must be an array of {} numbers", where, key, n));
            return false;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const json::Value* e = v->At(i);
            if (e == nullptr || !e->IsNumber() || !std::isfinite(e->AsNumber())) {
                Error(std::format("{}: '{}'[{}] must be a finite number", where, key, i));
                return false;
            }
            const double d = e->AsNumber();
            if (d < lo || d > hi) {
                Error(std::format("{}: '{}'[{}] = {} is outside [{}, {}]", where, key, i, d, lo, hi));
                return false;
            }
            out[i] = static_cast<float>(d);
        }
        return true;
    }

    bool ReadVec3(const json::Value& parent, const char* where, const char* key, Vec3& out, bool required, float lo, float hi) {
        float f[3] = {};
        if (!ReadVec(parent, where, key, 3, f, required, lo, hi)) return false;
        out = {f[0], f[1], f[2]};
        return true;
    }

    bool ReadString(const json::Value& parent, const char* where, const char* key, std::string& out, bool required) {
        const json::Value* v = parent.Get(key);
        if (v == nullptr) {
            if (required) Error(std::format("{}: missing string '{}'", where, key));
            return false;
        }
        if (!v->IsString()) {
            Error(std::format("{}: '{}' must be a string", where, key));
            return false;
        }
        out = v->AsString();
        return true;
    }

    const SceneFileLimits& Limits() const { return limits_; }

private:
    std::vector<std::string>& errors_;
    const SceneFileLimits& limits_;
};

bool CheckExtent(Reader& r, const std::string& where, Vec3 min, Vec3 max) {
    bool ok = true;
    const float d[3] = {max.x - min.x, max.y - min.y, max.z - min.z};
    const char* axes[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) {
        if (d[i] < r.Limits().minDimension || d[i] > r.Limits().maxDimension) {
            r.Error(std::format("{}: extent along {} is {} m; must be within [{}, {}]", where, axes[i], d[i], r.Limits().minDimension, r.Limits().maxDimension));
            ok = false;
        }
    }
    return ok;
}

bool IsAxisDirection(Vec3 v) {
    const int nonZero = (v.x != 0.0f) + (v.y != 0.0f) + (v.z != 0.0f);
    return nonZero == 1 && (std::fabs(v.x) == 1.0f || std::fabs(v.y) == 1.0f || std::fabs(v.z) == 1.0f);
}

void ParseMaterials(Reader& r, const json::Value& root, Document& doc) {
    const json::Value* list = root.Get("materials");
    if (list == nullptr || !list->IsArray()) {
        r.Error("materials: missing array");
        return;
    }
    if (list->Size() > r.Limits().maxMaterials) {
        r.Error(std::format("materials: {} entries exceed the limit of {}", list->Size(), r.Limits().maxMaterials));
        return;
    }
    std::set<std::string> seen;
    for (std::size_t i = 0; i < list->Size(); ++i) {
        const json::Value& v = *list->At(i);
        if (!v.IsObject()) {
            r.Error(std::format("materials[{}]: must be an object", i));
            continue;
        }
        MaterialSpec m;
        if (!r.ReadId(v, "materials", i, seen, m.id)) continue;
        const std::string where = "material '" + m.id + "'";
        m.material.name = m.id;
        std::string type;
        r.ReadString(v, where.c_str(), "type", type, true);
        if (type == "diffuse") m.material.type = MaterialType::Diffuse;
        else if (type == "mirror") m.material.type = MaterialType::Mirror;
        else if (type == "emitter") m.material.type = MaterialType::Emitter;
        else if (type == "rough_conductor") m.material.type = MaterialType::RoughConductor;
        else {
            r.Error(std::format("{}: unknown type '{}' (diffuse, mirror, emitter, rough_conductor)", where, type));
            continue;
        }
        r.ReadVec3(v, where.c_str(), "reflectance", m.material.reflectance, true, 0.0f, 1.0f);
        const bool emitter = m.material.type == MaterialType::Emitter;
        if (emitter) {
            r.ReadVec3(v, where.c_str(), "radiance", m.material.radiance, true, 0.0f, r.Limits().maxRadiance);
            if (!r.ReadString(v, where.c_str(), "circuit", m.circuit, true) || !Reader::IsIdentifier(m.circuit)) {
                r.Error(std::format("{}: emitters need a 'circuit' identifier", where));
            }
        } else {
            if (v.Get("radiance") != nullptr) r.Error(std::format("{}: 'radiance' is only valid for emitters", where));
            if (v.Get("circuit") != nullptr) r.Error(std::format("{}: 'circuit' is only valid for emitters", where));
        }
        if (m.material.type == MaterialType::RoughConductor) {
            r.ReadNumber(v, where.c_str(), "roughness", m.material.roughness, true, kMinRoughness, kMaxRoughness);
        } else if (v.Get("roughness") != nullptr) {
            r.Error(std::format("{}: 'roughness' is only valid for rough conductors", where));
        }
        for (const std::string& problem : ValidateMaterial(m.material)) {
            r.Error(std::format("{}: {}", where, problem));
        }
        doc.materials.push_back(std::move(m));
    }
}

void ParseCircuits(Reader& r, const json::Value& root, Document& doc) {
    const json::Value* list = root.Get("circuits");
    if (list == nullptr || !list->IsArray()) {
        r.Error("circuits: missing array");
        return;
    }
    if (list->Size() > r.Limits().maxCircuits) {
        r.Error(std::format("circuits: {} entries exceed the limit of {}", list->Size(), r.Limits().maxCircuits));
        return;
    }
    std::set<std::string> seen;
    for (std::size_t i = 0; i < list->Size(); ++i) {
        const json::Value& v = *list->At(i);
        if (!v.IsObject()) {
            r.Error(std::format("circuits[{}]: must be an object", i));
            continue;
        }
        CircuitSpec c;
        if (!r.ReadId(v, "circuits", i, seen, c.id)) continue;
        const json::Value* on = v.Get("on");
        if (on == nullptr || !on->IsBool()) {
            r.Error(std::format("circuit '{}': missing boolean 'on'", c.id));
            continue;
        }
        c.on = on->AsBool();
        doc.circuits.push_back(c);
    }
    for (const MaterialSpec& m : doc.materials) {
        if (m.material.type != MaterialType::Emitter) continue;
        bool found = false;
        for (const CircuitSpec& c : doc.circuits) found = found || c.id == m.circuit;
        if (!found) r.Error(std::format("material '{}': circuit '{}' does not exist", m.id, m.circuit));
    }
}

void ParseObjects(Reader& r, const json::Value& root, Document& doc) {
    const json::Value* list = root.Get("objects");
    if (list == nullptr || !list->IsArray()) {
        r.Error("objects: missing array");
        return;
    }
    if (list->Size() > r.Limits().maxObjects) {
        r.Error(std::format("objects: {} entries exceed the limit of {}", list->Size(), r.Limits().maxObjects));
        return;
    }
    const float maxC = r.Limits().maxCoordinate;
    const float minD = r.Limits().minDimension;
    const float maxD = r.Limits().maxDimension;
    std::set<std::string> seen;
    for (std::size_t i = 0; i < list->Size(); ++i) {
        const json::Value& v = *list->At(i);
        if (!v.IsObject()) {
            r.Error(std::format("objects[{}]: must be an object", i));
            continue;
        }
        ObjectSpec o;
        if (!r.ReadId(v, "objects", i, seen, o.id)) continue;
        const std::string where = "object '" + o.id + "'";
        const char* w = where.c_str();
        r.ReadString(v, w, "kind", o.kind, true);
        if (r.ReadString(v, w, "material", o.material, true)) {
            bool found = false;
            for (const MaterialSpec& m : doc.materials) found = found || m.id == o.material;
            if (!found) r.Error(std::format("{}: material '{}' does not exist", where, o.material));
        }
        if (const json::Value* c = v.Get("collider"); c != nullptr) {
            if (!c->IsBool()) r.Error(std::format("{}: 'collider' must be a boolean", where));
            else o.collider = c->AsBool();
            o.colliderGiven = c->IsBool();
        }
        if (o.kind == "slab") {
            if (r.ReadVec3(v, w, "min", o.min, true, -maxC, maxC) && r.ReadVec3(v, w, "max", o.max, true, -maxC, maxC)) CheckExtent(r, where, o.min, o.max);
        } else if (o.kind == "wall_opening") {
            bool ok = r.ReadVec3(v, w, "min", o.min, true, -maxC, maxC) && r.ReadVec3(v, w, "max", o.max, true, -maxC, maxC);
            ok = ok && CheckExtent(r, where, o.min, o.max);
            std::string axis;
            r.ReadString(v, w, "axis", axis, true);
            if (axis == "x") o.axisX = true;
            else if (axis == "z") o.axisX = false;
            else r.Error(std::format("{}: 'axis' must be \"x\" or \"z\"", where));
            const json::Value* op = v.Get("opening");
            if (op == nullptr || !op->IsObject()) {
                r.Error(std::format("{}: missing 'opening' object", where));
            } else {
                const std::string ow = where + " opening";
                r.ReadNumber(*op, ow.c_str(), "centre", o.opening.centre, true, -maxC, maxC);
                r.ReadNumber(*op, ow.c_str(), "width", o.opening.width, true, minD, maxD);
                r.ReadNumber(*op, ow.c_str(), "height", o.opening.height, true, minD, maxD);
                if (ok) {
                    const float lo = o.axisX ? o.min.x : o.min.z;
                    const float hi = o.axisX ? o.max.x : o.max.z;
                    if (o.opening.centre - o.opening.width * 0.5f <= lo || o.opening.centre + o.opening.width * 0.5f >= hi) {
                        r.Error(std::format("{}: the opening leaves no wall on at least one side", where));
                    }
                    if (o.opening.height >= o.max.y - o.min.y) r.Error(std::format("{}: the opening is as tall as the wall", where));
                }
            }
        } else if (o.kind == "door_leaf") {
            r.ReadVec3(v, w, "hinge", o.leaf.hingeBase, true, -maxC, maxC);
            r.ReadVec3(v, w, "widthDir", o.leaf.widthDir, true, -1.0f, 1.0f);
            r.ReadVec3(v, w, "thicknessDir", o.leaf.thicknessDir, true, -1.0f, 1.0f);
            if (!IsAxisDirection(o.leaf.widthDir) || o.leaf.widthDir.y != 0.0f || !IsAxisDirection(o.leaf.thicknessDir) || o.leaf.thicknessDir.y != 0.0f ||
                math::Dot(o.leaf.widthDir, o.leaf.thicknessDir) != 0.0f) {
                r.Error(std::format("{}: 'widthDir' and 'thicknessDir' must be perpendicular horizontal unit axes", where));
            }
            r.ReadNumber(v, w, "width", o.leaf.width, true, minD, maxD);
            r.ReadNumber(v, w, "height", o.leaf.height, true, minD, maxD);
            r.ReadNumber(v, w, "thickness", o.leaf.thickness, false, minD, 1.0f);
            r.ReadNumber(v, w, "inset", o.leaf.inset, false, 0.0f, 0.1f);
            r.ReadNumber(v, w, "overlap", o.leaf.overlap, false, 0.0f, 0.5f);
        } else if (o.kind == "emitter_rect") {
            r.ReadNumber(v, w, "width", o.width, true, minD, maxD);
            r.ReadNumber(v, w, "height", o.height, true, minD, maxD);
            r.ReadVec3(v, w, "position", o.position, true, -maxC, maxC);
            if (r.ReadVec3(v, w, "facing", o.facing, true, -1.0f, 1.0f) && !IsAxisDirection(o.facing)) {
                r.Error(std::format("{}: 'facing' must be one of the six axis directions", where));
            }
            bool emitterMaterial = false;
            for (const MaterialSpec& m : doc.materials) emitterMaterial = emitterMaterial || (m.id == o.material && m.material.type == MaterialType::Emitter);
            if (!emitterMaterial) r.Error(std::format("{}: an emitter rectangle needs an emitter material", where));
        } else if (o.kind == "box") {
            r.ReadVec3(v, w, "half", o.half, true, minD * 0.5f, maxD * 0.5f);
            Vec3 centre;
            if (r.ReadVec3(v, w, "centre", centre, false, -maxC, maxC)) o.centre = centre;
            float yaw = 0.0f;
            if (r.ReadNumber(v, w, "yaw", yaw, false, -7.0f, 7.0f)) o.yaw = yaw;
            if (const json::Value* aim = v.Get("aim"); aim != nullptr) {
                if (!aim->IsObject()) {
                    r.Error(std::format("{}: 'aim' must be an object", where));
                } else {
                    AimSpec a;
                    const std::string aw = where + " aim";
                    r.ReadString(*aim, aw.c_str(), "camera", a.camera, true);
                    r.ReadString(*aim, aw.c_str(), "target", a.target, true);
                    r.ReadNumber(*aim, aw.c_str(), "cameraHeight", a.cameraHeight, false, 0.0f, 10.0f);
                    r.ReadNumber(*aim, aw.c_str(), "targetHeight", a.targetHeight, false, 0.0f, 10.0f);
                    o.aim = a;
                }
            }
            if (o.yaw && o.aim) r.Error(std::format("{}: 'yaw' and 'aim' are exclusive", where));
        } else if (o.kind == "quad") {
            float half[2] = {};
            if (r.ReadVec(v, w, "half", 2, half, true, minD * 0.5f, maxD * 0.5f)) o.half = {half[0], 0.0f, half[1]};
            o.collider = v.Get("collider") != nullptr ? o.collider : false;  // Quads do not collide unless asked.
            if (v.Get("centre") != nullptr) r.Error(std::format("{}: quads are placed by entities and take no 'centre'", where));
        } else {
            r.Error(std::format("{}: unknown kind '{}' (slab, wall_opening, door_leaf, emitter_rect, box, quad)", where, o.kind));
            continue;
        }
        if (o.kind == "emitter_rect") o.collider = v.Get("collider") != nullptr ? o.collider : false;
        doc.objects.push_back(std::move(o));
    }
}

void ParseSockets(Reader& r, const json::Value& root, Document& doc) {
    const json::Value* list = root.Get("sockets");
    if (list == nullptr || !list->IsArray()) {
        r.Error("sockets: missing array");
        return;
    }
    if (list->Size() > r.Limits().maxSockets) {
        r.Error(std::format("sockets: {} entries exceed the limit of {}", list->Size(), r.Limits().maxSockets));
        return;
    }
    std::set<std::string> seen;
    for (std::size_t i = 0; i < list->Size(); ++i) {
        const json::Value& v = *list->At(i);
        if (!v.IsObject()) {
            r.Error(std::format("sockets[{}]: must be an object", i));
            continue;
        }
        SocketSpec s;
        if (!r.ReadId(v, "sockets", i, seen, s.name)) continue;
        const std::string where = "socket '" + s.name + "'";
        r.ReadVec3(v, where.c_str(), "position", s.position, true, -r.Limits().maxCoordinate, r.Limits().maxCoordinate);
        r.ReadNumber(v, where.c_str(), "yaw", s.yaw, false, -7.0f, 7.0f);
        doc.sockets.push_back(s);
    }
}

void ParsePaths(Reader& r, const json::Value& root, Document& doc) {
    const json::Value* list = root.Get("paths");
    if (list == nullptr || !list->IsArray()) {
        r.Error("paths: missing array");
        return;
    }
    if (list->Size() > r.Limits().maxPaths) {
        r.Error(std::format("paths: {} entries exceed the limit of {}", list->Size(), r.Limits().maxPaths));
        return;
    }
    std::set<std::string> seen;
    for (std::size_t i = 0; i < list->Size(); ++i) {
        const json::Value& v = *list->At(i);
        if (!v.IsObject()) {
            r.Error(std::format("paths[{}]: must be an object", i));
            continue;
        }
        PathSpec p;
        if (!r.ReadId(v, "paths", i, seen, p.id)) continue;
        const std::string where = "path '" + p.id + "'";
        const json::Value* pts = v.Get("points");
        if (pts == nullptr || !pts->IsArray() || pts->Size() < 2 || pts->Size() > r.Limits().maxPathPoints) {
            r.Error(std::format("{}: 'points' must be an array of 2..{} points", where, r.Limits().maxPathPoints));
        } else {
            for (std::size_t k = 0; k < pts->Size(); ++k) {
                const json::Value* e = pts->At(k);
                float f[3] = {};
                bool ok = e != nullptr && e->IsArray() && e->Size() == 3;
                for (std::size_t c = 0; ok && c < 3; ++c) {
                    const json::Value* n = e->At(c);
                    ok = n != nullptr && n->IsNumber() && std::isfinite(n->AsNumber()) && std::fabs(n->AsNumber()) <= r.Limits().maxCoordinate;
                    if (ok) f[c] = static_cast<float>(n->AsNumber());
                }
                if (!ok) {
                    r.Error(std::format("{}: points[{}] must be a finite 3-vector within the coordinate bound", where, k));
                    break;
                }
                p.points.push_back({f[0], f[1], f[2]});
            }
            for (std::size_t k = 1; k < p.points.size(); ++k) {
                if (math::LengthSquared(p.points[k] - p.points[k - 1]) <= 0.0f) r.Error(std::format("{}: points[{}] repeats the previous point", where, k));
            }
        }
        r.ReadNumber(v, where.c_str(), "speed", p.speed, true, 1e-3f, 10.0f);
        std::string mode;
        if (r.ReadString(v, where.c_str(), "mode", mode, true) && mode != "ping_pong") {
            r.Error(std::format("{}: mode '{}' is not implemented (ping_pong)", where, mode));
        }
        doc.paths.push_back(std::move(p));
    }
}

void ParseMarkers(Reader& r, const json::Value& root, Document& doc) {
    const json::Value* list = root.Get("markers");
    if (list == nullptr || !list->IsArray()) {
        r.Error("markers: missing array");
        return;
    }
    if (list->Size() > r.Limits().maxMarkers) {
        r.Error(std::format("markers: {} entries exceed the limit of {}", list->Size(), r.Limits().maxMarkers));
        return;
    }
    std::set<std::string> seen;
    for (std::size_t i = 0; i < list->Size(); ++i) {
        const json::Value& v = *list->At(i);
        if (!v.IsObject()) {
            r.Error(std::format("markers[{}]: must be an object", i));
            continue;
        }
        MarkerSpec m;
        if (!r.ReadId(v, "markers", i, seen, m.id)) continue;
        const std::string where = "marker '" + m.id + "'";
        r.ReadVec3(v, where.c_str(), "position", m.position, true, -r.Limits().maxCoordinate, r.Limits().maxCoordinate);
        m.hasYaw = r.ReadNumber(v, where.c_str(), "yaw", m.yaw, false, -7.0f, 7.0f);
        doc.markers.push_back(m);
    }
}

template <class T>
const T* FindById(const std::vector<T>& list, const std::string& id, const std::string& (*idOf)(const T&)) {
    for (const T& item : list) {
        if (idOf(item) == id) return &item;
    }
    return nullptr;
}

const std::string& ObjectId(const ObjectSpec& o) { return o.id; }
const std::string& MaterialId(const MaterialSpec& m) { return m.id; }
const std::string& SocketId(const SocketSpec& s) { return s.name; }
const std::string& PathId(const PathSpec& p) { return p.id; }
const std::string& MarkerId(const MarkerSpec& m) { return m.id; }

void ParseEntities(Reader& r, const json::Value& root, Document& doc) {
    const json::Value* e = root.Get("entities");
    if (e == nullptr || !e->IsObject()) {
        r.Error("entities: missing object");
        return;
    }
    auto section = [&](const char* name) -> const json::Value* {
        const json::Value* s = e->Get(name);
        if (s == nullptr || !s->IsObject()) {
            r.Error(std::format("entities: missing object '{}'", name));
            return nullptr;
        }
        return s;
    };
    auto requireObject = [&](const std::string& id, const char* where, const char* kind, bool placedByEntity) -> bool {
        ObjectSpec* o = nullptr;
        for (ObjectSpec& candidate : doc.objects) {
            if (candidate.id == id) o = &candidate;
        }
        if (o == nullptr) {
            r.Error(std::format("{}: object '{}' does not exist", where, id));
            return false;
        }
        if (o->kind != kind) {
            r.Error(std::format("{}: object '{}' must be a {}", where, id, kind));
            return false;
        }
        if (placedByEntity) {
            if (o->centre) r.Error(std::format("{}: object '{}' is placed by the entity and must not have a 'centre'", where, id));
            o->placedByEntity = true;
            if (!o->colliderGiven) o->collider = false;  // Moving entities are not static solids (the door leaf is handled by the door).
        }
        return true;
    };
    auto requireMarker = [&](const std::string& id, const char* where) {
        if (FindById(doc.markers, id, MarkerId) == nullptr) r.Error(std::format("{}: marker '{}' does not exist", where, id));
    };

    if (const json::Value* player = section("player")) {
        if (r.ReadString(*player, "entities.player", "start", doc.playerStart, true)) requireMarker(doc.playerStart, "entities.player.start");
        const bool torso = r.ReadString(*player, "entities.player", "torso", doc.playerTorso, false);
        const bool left = r.ReadString(*player, "entities.player", "handLeft", doc.playerHandLeft, false);
        const bool right = r.ReadString(*player, "entities.player", "handRight", doc.playerHandRight, false);
        if (torso || left || right) {
            if (!(torso && left && right)) {
                r.Error("entities.player: 'torso', 'handLeft', and 'handRight' come together");
            } else {
                requireObject(doc.playerTorso, "entities.player.torso", "box", true);
                requireObject(doc.playerHandLeft, "entities.player.handLeft", "box", true);
                requireObject(doc.playerHandRight, "entities.player.handRight", "box", true);
            }
        }
    }
    if (const json::Value* door = section("door")) {
        if (r.ReadString(*door, "entities.door", "object", doc.doorObject, true)) requireObject(doc.doorObject, "entities.door.object", "door_leaf", false);
    }
    if (const json::Value* lamp = section("lamp")) {
        if (r.ReadString(*lamp, "entities.lamp", "housing", doc.lampHousing, true)) requireObject(doc.lampHousing, "entities.lamp.housing", "box", true);
        if (r.ReadString(*lamp, "entities.lamp", "face", doc.lampFace, true)) requireObject(doc.lampFace, "entities.lamp.face", "quad", true);
        r.ReadNumber(*lamp, "entities.lamp", "faceOffset", doc.lampFaceOffset, true, 1e-3f, 1.0f);
        if (r.ReadString(*lamp, "entities.lamp", "material", doc.lampMaterial, true)) {
            const MaterialSpec* m = FindById(doc.materials, doc.lampMaterial, MaterialId);
            if (m == nullptr || m->material.type != MaterialType::Emitter) r.Error("entities.lamp.material: must name an emitter material");
        }
        if (r.ReadString(*lamp, "entities.lamp", "startSocket", doc.lampStartSocket, true) && FindById(doc.sockets, doc.lampStartSocket, SocketId) == nullptr) {
            r.Error(std::format("entities.lamp.startSocket: socket '{}' does not exist", doc.lampStartSocket));
        }
        if (r.ReadString(*lamp, "entities.lamp", "placeSocket", doc.lampPlaceSocket, true) && FindById(doc.sockets, doc.lampPlaceSocket, SocketId) == nullptr) {
            r.Error(std::format("entities.lamp.placeSocket: socket '{}' does not exist", doc.lampPlaceSocket));
        }
        if (!doc.lampStartSocket.empty() && doc.lampStartSocket == doc.lampPlaceSocket) r.Error("entities.lamp: startSocket and placeSocket must differ");
    }
    if (const json::Value* threat = section("threat")) {
        if (r.ReadString(*threat, "entities.threat", "body", doc.threatBody, true)) requireObject(doc.threatBody, "entities.threat.body", "box", true);
        if (r.ReadString(*threat, "entities.threat", "head", doc.threatHead, true)) requireObject(doc.threatHead, "entities.threat.head", "box", true);
        if (r.ReadString(*threat, "entities.threat", "path", doc.threatPath, true) && FindById(doc.paths, doc.threatPath, PathId) == nullptr) {
            r.Error(std::format("entities.threat.path: path '{}' does not exist", doc.threatPath));
        }
        if (r.ReadString(*threat, "entities.threat", "parkAt", doc.threatParkAt, true)) requireMarker(doc.threatParkAt, "entities.threat.parkAt");
    }
    if (const json::Value* mirror = section("mirror")) {
        if (r.ReadString(*mirror, "entities.mirror", "object", doc.mirrorObject, true) && requireObject(doc.mirrorObject, "entities.mirror.object", "box", false)) {
            const ObjectSpec* o = FindById(doc.objects, doc.mirrorObject, ObjectId);
            const MaterialSpec* m = o != nullptr ? FindById(doc.materials, o->material, MaterialId) : nullptr;
            if (m == nullptr || m->material.type != MaterialType::Mirror) r.Error("entities.mirror.object: the mirror object needs a mirror material");
            if (o != nullptr && !o->centre) r.Error("entities.mirror.object: the mirror needs a 'centre'");
        }
        if (r.ReadString(*mirror, "entities.mirror", "checkCamera", doc.mirrorCheckCamera, true)) requireMarker(doc.mirrorCheckCamera, "entities.mirror.checkCamera");
        if (r.ReadString(*mirror, "entities.mirror", "aimAt", doc.mirrorAimAt, true)) requireMarker(doc.mirrorAimAt, "entities.mirror.aimAt");
        if (r.ReadString(*mirror, "entities.mirror", "hallFloor", doc.hallFloor, true)) requireObject(doc.hallFloor, "entities.mirror.hallFloor", "slab", false);
    }
    // Every box or quad without a static placement must be placed by an entity; static boxes need a centre.
    for (const ObjectSpec& o : doc.objects) {
        if ((o.kind == "box" || o.kind == "quad") && !o.placedByEntity && !o.centre) {
            r.Error(std::format("object '{}': a {} needs a 'centre' unless an entity places it", o.id, o.kind));
        }
        if (o.aim) {
            requireMarker(o.aim->camera, ("object '" + o.id + "' aim.camera").c_str());
            requireMarker(o.aim->target, ("object '" + o.id + "' aim.target").c_str());
        }
    }
    // Objectives (spec §15, §16): one "exit" entry names the marker the player must reach with the
    // lamp; the phases before it (lamp acquired, placed, retrieved) come from the lamp entity.
    const json::Value* objectives = root.Get("objectives");
    if (objectives == nullptr || !objectives->IsArray()) {
        r.Error("objectives: missing array");
        return;
    }
    std::size_t exits = 0;
    for (std::size_t i = 0; i < objectives->Size(); ++i) {
        const json::Value& v = *objectives->At(i);
        const std::string where = std::format("objectives[{}]", i);
        if (!v.IsObject()) {
            r.Error(where + ": must be an object");
            continue;
        }
        std::string id;
        std::string kind;
        r.ReadString(v, where.c_str(), "id", id, true);
        if (!r.ReadString(v, where.c_str(), "kind", kind, true)) continue;
        if (kind != "exit") {
            r.Error(std::format("{}: unknown kind '{}' (known: exit)", where, kind));
            continue;
        }
        if (++exits > 1) {
            r.Error(where + ": only one exit objective is allowed");
            continue;
        }
        if (r.ReadString(v, where.c_str(), "marker", doc.exitMarker, true)) requireMarker(doc.exitMarker, (where + ".marker").c_str());
        r.ReadNumber(v, where.c_str(), "radius", doc.exitRadius, false, 0.1f, 10.0f);
    }
}

// ---------------------------------------------------------------------------------------------
// Build (only from a validated document).

Mat4 PoseTransform(const PoseSpec& pose) {
    return Mat4::Translation(pose.position) * Mat4::RotationY(pose.yaw) * Mat4::RotationX(pose.pitch);
}

TwoRoomLevel BuildLevel(const Document& doc, std::string_view sourceName, std::uint64_t contentHash) {
    TwoRoomLevel level;
    SceneDescription& d = level.description;
    Scene& s = d.scene;
    d.name = doc.name;
    d.needsLighting = true;
    level.sceneFile = std::string(sourceName);
    level.contentHash = contentHash;

    std::map<std::string, std::uint32_t> materialIndex;
    for (const MaterialSpec& m : doc.materials) {
        materialIndex[m.id] = s.AddMaterial(m.material);
    }
    for (const MaterialSpec& m : doc.materials) {
        if (m.material.type != MaterialType::Emitter) continue;
        for (const CircuitSpec& c : doc.circuits) {
            if (c.id == m.circuit && !c.on) s.SetEmitterOn(materialIndex[m.id], false);
        }
    }
    level.circuits.clear();
    for (const CircuitSpec& c : doc.circuits) level.circuits.push_back({c.id, c.on});
    level.sockets = doc.sockets;

    auto marker = [&](const std::string& id) { return FindById(doc.markers, id, MarkerId); };
    auto socket = [&](const std::string& id) { return FindById(doc.sockets, id, SocketId); };
    const PathSpec* path = FindById(doc.paths, doc.threatPath, PathId);
    const MarkerSpec* threatPark = marker(doc.threatParkAt);
    const MarkerSpec* checkCam = marker(doc.mirrorCheckCamera);
    const MarkerSpec* aimAt = marker(doc.mirrorAimAt);
    const MarkerSpec* start = marker(doc.playerStart);
    const SocketSpec* startSocket = socket(doc.lampStartSocket);
    const SocketSpec* placeSocket = socket(doc.lampPlaceSocket);

    level.threatPath = path->points;
    level.threatSpeed = path->speed;
    level.threatCheckPosition = threatPark->position;
    if (!doc.exitMarker.empty()) {
        level.hasExit = true;
        level.exitPosition = marker(doc.exitMarker)->position;
        level.exitRadius = doc.exitRadius;
    }
    level.mirrorCheckCamera = {checkCam->position, checkCam->hasYaw ? checkCam->yaw : 0.0f, 0.0f};
    level.playerStart = {start->position, start->hasYaw ? start->yaw : 0.0f, 0.0f};
    level.floorSocket = *startSocket;
    level.shelfSocket = *placeSocket;
    level.lampMaterial = materialIndex[doc.lampMaterial];
    level.lampFaceOffset = doc.lampFaceOffset;
    const PoseSpec lampPose{startSocket->position, startSocket->yaw, 0.0f};
    const PoseSpec threatPose{threatPark->position, 0.0f, 0.0f};

    for (const ObjectSpec& o : doc.objects) {
        const std::uint32_t mat = materialIndex.at(o.material);
        InstanceId id;
        bool single = true;
        if (o.kind == "slab") {
            id = AddSlab(s, o.id, o.min, o.max, mat);
        } else if (o.kind == "wall_opening") {
            AddWallWithOpening(s, o.id, o.min, o.max, o.axisX, o.opening, mat);
            single = false;
            if (o.collider) {
                for (const Instance& inst : s.Instances()) {
                    if (inst.name.rfind(o.id, 0) == 0) level.colliders.push_back(inst.id);
                }
            }
        } else if (o.kind == "door_leaf") {
            level.door = AddDoorLeaf(s, o.id, o.leaf, mat);
            id = level.door.id;
        } else if (o.kind == "emitter_rect") {
            id = AddRectangleEmitter(s, o.id, o.width, o.height, o.position, o.facing, mat);
        } else if (o.kind == "box") {
            if (o.id == doc.lampHousing) {
                level.lampHousingHalf = o.half;
                const MeshId mesh = s.AddMesh(MakeBox(o.id, o.half));
                id = s.AddInstance(o.id, mesh, LampHousingTransform(lampPose), mat);
                level.lampHousing = id;
            } else if (o.id == doc.threatBody) {
                const MeshId mesh = s.AddMesh(MakeBox(o.id, o.half));
                id = s.AddInstance(o.id, mesh, ThreatBodyTransform(threatPose), mat);
                level.threatBody = id;
            } else if (o.id == doc.threatHead) {
                const MeshId mesh = s.AddMesh(MakeBox(o.id, o.half));
                id = s.AddInstance(o.id, mesh, ThreatHeadTransform(threatPose), mat);
                level.threatHead = id;
            } else if (!doc.playerTorso.empty() && (o.id == doc.playerTorso || o.id == doc.playerHandLeft || o.id == doc.playerHandRight)) {
                const MeshId mesh = s.AddMesh(MakeBox(o.id, o.half));
                const PoseSpec feet{start->position, start->hasYaw ? start->yaw : 0.0f, 0.0f};
                const Mat4 transform = o.id == doc.playerTorso ? PlayerTorsoTransform(feet) : PlayerHandTransform(feet, o.id == doc.playerHandRight);
                id = s.AddInstance(o.id, mesh, transform, mat);
                if (o.id == doc.playerTorso) level.playerTorso = id;
                else if (o.id == doc.playerHandLeft) level.playerHandLeft = id;
                else level.playerHandRight = id;
            } else {
                float yaw = o.yaw.value_or(0.0f);
                if (o.aim) {
                    const MarkerSpec* cam = marker(o.aim->camera);
                    const MarkerSpec* target = marker(o.aim->target);
                    const Vec3 eye = cam->position + Vec3{0.0f, o.aim->cameraHeight, 0.0f};
                    const Vec3 aimPoint = target->position + Vec3{0.0f, o.aim->targetHeight, 0.0f};
                    const Vec3 toCamera = math::Normalize(eye - *o.centre);
                    const Vec3 toTarget = math::Normalize(aimPoint - *o.centre);
                    const Vec3 normal = math::Normalize(toCamera + toTarget);
                    yaw = std::atan2(-normal.x, -normal.z);  // RotationY(yaw) maps -Z to (-sin, 0, -cos).
                    if (o.id == doc.mirrorObject) {
                        level.mirrorCheckPoint = *o.centre - normal * o.half.z;  // On the reflecting face.
                        level.mirrorNormal = -normal;
                    }
                }
                const MeshId mesh = s.AddMesh(MakeBox(o.id, o.half));
                id = s.AddInstance(o.id, mesh, Mat4::Translation(*o.centre) * Mat4::RotationY(yaw), mat);
                if (o.id == doc.mirrorObject) {
                    level.mirror = id;
                    if (!o.aim) {
                        const Vec3 n = Mat4::RotationY(yaw).TransformDirection({0.0f, 0.0f, -1.0f});
                        level.mirrorNormal = n;
                        level.mirrorCheckPoint = *o.centre + n * o.half.z;
                    }
                }
            }
        } else if (o.kind == "quad") {
            const MeshId mesh = s.AddMesh(MakeQuadXZ(o.id, o.half.x, o.half.z));
            id = s.AddInstance(o.id, mesh, LampFaceTransform(lampPose, doc.lampFaceOffset), mat);
            if (o.id == doc.lampFace) level.lampFace = id;
        }
        if (single && o.collider) level.colliders.push_back(id);
        if (single && o.id == doc.hallFloor) level.hallFloorId = id;
    }
    (void)aimAt;
    level.hallCheckPoint = level.threatCheckPosition;

    // Static description: the camera at the mirror check position and the static proof's expectations.
    d.camera.position = level.mirrorCheckCamera.position + Vec3{0.0f, 1.6f, 0.0f};
    d.camera.yawRadians = level.mirrorCheckCamera.yaw;
    d.camera.pitchRadians = level.mirrorCheckCamera.pitch;
    HitExpectation seesThreat;
    seesThreat.worldPoint = level.mirrorCheckPoint;
    seesThreat.expectedStableId = level.threatBody.value;
    seesThreat.description = "mirror shows the threat in the hall (outside the direct view)";
    d.expectations = {seesThreat};
    RadianceExpectation lit;
    lit.kind = RadianceExpectation::Kind::PositivePatch;
    lit.description = "mirror image of the threat is lit by the hall fixture";
    lit.point = level.mirrorCheckPoint;
    lit.minimum = 1e-4f;  // Derived in docs/TESTS.md from the emergency fixture, the threat albedo, and the mirror.
    d.radianceExpectations = {lit};
    d.statsPatches = {{"mirror_threat", level.mirrorCheckPoint, 2}};
    return level;
}

}  // namespace

std::uint64_t Fnv1a64(std::string_view bytes) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (const unsigned char c : bytes) {
        h ^= c;
        h *= 0x100000001B3ull;
    }
    return h;
}

std::string SceneFileResult::ErrorText() const {
    std::string text;
    for (const std::string& e : errors) {
        text += sourceName + ": " + e + "\n";
    }
    return text;
}

SceneFileResult ParseSceneFile(std::string_view text, std::string_view sourceName, const SceneFileLimits& limits) {
    SceneFileResult result;
    result.sourceName = std::string(sourceName);
    result.contentHash = Fnv1a64(text);
    if (text.size() > limits.maxFileBytes) {
        result.errors.push_back(std::format("{} bytes exceed the scene file limit of {} bytes", text.size(), limits.maxFileBytes));
        return result;
    }
    const json::ParseResult parsed = json::Parse(text);
    if (!parsed.value) {
        result.errors.push_back("JSON: " + parsed.error);
        return result;
    }
    const json::Value& root = *parsed.value;
    if (!root.IsObject()) {
        result.errors.push_back("the document must be an object");
        return result;
    }
    Reader r(result.errors, limits);
    const json::Value* schema = root.Get("schema");
    if (schema == nullptr || !schema->IsNumber() || schema->AsNumber() != static_cast<double>(kSceneSchemaVersion)) {
        result.errors.push_back(std::format("schema: expected {} (got {})", kSceneSchemaVersion,
                                            schema != nullptr && schema->IsNumber() ? std::to_string(static_cast<int>(schema->AsNumber())) : "missing"));
        return result;  // A different schema is not interpreted further.
    }
    Document doc;
    if (!r.ReadString(root, "document", "name", doc.name, true) || !Reader::IsIdentifier(doc.name)) {
        r.Error("name: must be a lowercase identifier");
    }
    if (const json::Value* units = root.Get("units"); units != nullptr && (!units->IsString() || units->AsString() != "metres")) {
        r.Error("units: only \"metres\" is supported");
    }
    ParseMaterials(r, root, doc);
    ParseCircuits(r, root, doc);
    ParseObjects(r, root, doc);
    ParseSockets(r, root, doc);
    ParsePaths(r, root, doc);
    ParseMarkers(r, root, doc);
    ParseEntities(r, root, doc);
    if (!result.errors.empty()) {
        return result;
    }
    result.level = BuildLevel(doc, sourceName, result.contentHash);
    return result;
}

bool PathWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root) {
    std::error_code ec;
    const std::filesystem::path absPath = std::filesystem::absolute(path, ec).lexically_normal();
    const std::filesystem::path absRoot = std::filesystem::absolute(root, ec).lexically_normal();
    auto it = absRoot.begin();
    auto pit = absPath.begin();
    for (; it != absRoot.end(); ++it, ++pit) {
        if (it->empty()) continue;  // Trailing separator.
        if (pit == absPath.end() || *pit != *it) return false;
    }
    return true;
}

SceneFileResult LoadSceneFile(const std::filesystem::path& path, const std::filesystem::path& assetRoot, const SceneFileLimits& limits) {
    SceneFileResult result;
    const std::filesystem::path resolved = path.is_absolute() ? path : assetRoot / path;
    result.sourceName = resolved.generic_string();
    if (!PathWithinRoot(resolved, assetRoot)) {
        result.errors.push_back(std::format("path is outside the asset root '{}'", assetRoot.generic_string()));
        return result;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(resolved, ec);
    if (ec) {
        result.errors.push_back("cannot open the scene file");
        return result;
    }
    if (size > limits.maxFileBytes) {
        result.errors.push_back(std::format("{} bytes exceed the scene file limit of {} bytes", size, limits.maxFileBytes));
        return result;
    }
    std::ifstream in(resolved, std::ios::binary);
    if (!in) {
        result.errors.push_back("cannot read the scene file");
        return result;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    SceneFileResult parsed = ParseSceneFile(text, result.sourceName, limits);
    return parsed;
}

void SetAssetRoot(std::filesystem::path root) { g_assetRoot = std::move(root); }

std::filesystem::path AssetRoot() {
    if (!g_assetRoot.empty()) return g_assetRoot;
#if defined(LC_ASSET_ROOT)
    return std::filesystem::path(LC_ASSET_ROOT);
#else
    return std::filesystem::path("assets");
#endif
}

}  // namespace lc
