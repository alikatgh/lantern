// obj.cpp — minimal Wavefront OBJ loader → lantern vertex format
// (pos3 normal3 uv2 rgba4). Supports v / vt / vn / f with any of the
// face index forms (a, a/b, a//c, a/b/c) incl. negative (relative) indices;
// polygons fan-triangulate. Missing normals get flat face normals.
// Materials/objects/groups/smoothing are ignored by design — texture and
// tint come from lt_draw_mesh.
#include "lantern_math.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace lt {

struct ObjIndex { int v = 0, t = 0, n = 0; };

static bool parseIndex(const char* tok, ObjIndex& out) {
    // forms: "1", "1/2", "1//3", "1/2/3" (also negative)
    out = ObjIndex{};
    out.v = std::atoi(tok);
    const char* s1 = std::strchr(tok, '/');
    if (s1) {
        if (s1[1] != '/') out.t = std::atoi(s1 + 1);
        const char* s2 = std::strchr(s1 + 1, '/');
        if (s2) out.n = std::atoi(s2 + 1);
    }
    return out.v != 0;
}

static int resolve(int idx, int count) { // 1-based or negative-relative
    if (idx > 0) return idx - 1;
    return count + idx; // idx < 0
}

bool loadObj(const char* path, std::vector<float>& out) {
    FILE* f = std::fopen(path, "r");
    if (!f) {
        std::fprintf(stderr, "obj: cannot open %s\n", path);
        return false;
    }
    std::vector<Vec3> pos, nrm;
    std::vector<float> uv; // pairs
    char line[1024];
    auto emit = [&](const ObjIndex& a, Vec3 faceN) {
        int vi = resolve(a.v, (int)pos.size());
        if (vi < 0 || vi >= (int)pos.size()) return;
        Vec3 p = pos[vi];
        Vec3 n = faceN;
        if (a.n != 0) {
            int ni = resolve(a.n, (int)nrm.size());
            if (ni >= 0 && ni < (int)nrm.size()) n = nrm[ni];
        }
        float u = 0, v = 0;
        if (a.t != 0) {
            int ti = resolve(a.t, (int)uv.size() / 2);
            if (ti >= 0 && ti * 2 + 1 < (int)uv.size()) {
                u = uv[ti * 2];
                v = 1.0f - uv[ti * 2 + 1]; // OBJ v is bottom-up
            }
        }
        out.insert(out.end(), {p.x, p.y, p.z, n.x, n.y, n.z, u, v, 1, 1, 1, 1});
    };
    while (std::fgets(line, sizeof line, f)) {
        if (line[0] == 'v' && line[1] == ' ') {
            Vec3 p;
            if (std::sscanf(line + 2, "%f %f %f", &p.x, &p.y, &p.z) == 3)
                pos.push_back(p);
        } else if (line[0] == 'v' && line[1] == 'n') {
            Vec3 n;
            if (std::sscanf(line + 3, "%f %f %f", &n.x, &n.y, &n.z) == 3)
                nrm.push_back(n);
        } else if (line[0] == 'v' && line[1] == 't') {
            float u, v;
            if (std::sscanf(line + 3, "%f %f", &u, &v) == 2) {
                uv.push_back(u);
                uv.push_back(v);
            }
        } else if (line[0] == 'f' && line[1] == ' ') {
            std::vector<ObjIndex> face;
            char* save = nullptr;
            for (char* tok = strtok_r(line + 2, " \t\r\n", &save); tok;
                 tok = strtok_r(nullptr, " \t\r\n", &save)) {
                ObjIndex ix;
                if (parseIndex(tok, ix)) face.push_back(ix);
            }
            if (face.size() < 3) continue;
            // flat face normal from the first three vertices (fallback)
            Vec3 fn{0, 1, 0};
            {
                int i0 = resolve(face[0].v, (int)pos.size());
                int i1 = resolve(face[1].v, (int)pos.size());
                int i2 = resolve(face[2].v, (int)pos.size());
                if (i0 >= 0 && i1 >= 0 && i2 >= 0 && i0 < (int)pos.size() &&
                    i1 < (int)pos.size() && i2 < (int)pos.size())
                    fn = norm(cross(sub(pos[i1], pos[i0]),
                                    sub(pos[i2], pos[i0])));
            }
            for (size_t i = 1; i + 1 < face.size(); i++) { // fan
                emit(face[0], fn);
                emit(face[i], fn);
                emit(face[i + 1], fn);
            }
        }
    }
    std::fclose(f);
    if (out.empty()) {
        std::fprintf(stderr, "obj: %s produced no triangles\n", path);
        return false;
    }
    return true;
}

} // namespace lt
