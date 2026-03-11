// PACE Profile — JSON parser + data structures
// Reads pace_profile.json describing workload-driven injection parameters.

#ifndef __PACE_PROFILE_HH__
#define __PACE_PROFILE_HH__

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <algorithm>

namespace garnet {

// ============================================================
// Minimal recursive-descent JSON parser
// ============================================================

struct JsonVal {
    enum Type { Null_, Bool_, Int_, Double_, Str_, Arr_, Obj_ } type;
    bool b; int64_t i; double d; std::string s;
    std::vector<JsonVal> arr;
    std::map<std::string, JsonVal> obj;

    JsonVal() : type(Null_), b(false), i(0), d(0.0) {}

    bool isNull()   const { return type == Null_; }
    bool isBool()   const { return type == Bool_; }
    bool isInt()    const { return type == Int_; }
    bool isDouble() const { return type == Double_; }
    bool isNum()    const { return type == Int_ || type == Double_; }
    bool isStr()    const { return type == Str_; }
    bool isArr()    const { return type == Arr_; }
    bool isObj()    const { return type == Obj_; }

    int64_t asInt() const {
        if (type == Int_) return i;
        if (type == Double_) return static_cast<int64_t>(d);
        throw std::runtime_error("JsonVal::asInt() — not a number");
    }
    double asDouble() const {
        if (type == Double_) return d;
        if (type == Int_)    return static_cast<double>(i);
        throw std::runtime_error("JsonVal::asDouble() — not a number");
    }
    const std::string& asStr() const { return s; }
    bool asBool() const { return b; }

    bool hasKey(const std::string& key) const {
        return type == Obj_ && obj.count(key) > 0;
    }
    const JsonVal& operator[](const std::string& key) const {
        auto it = obj.find(key);
        if (it == obj.end())
            throw std::runtime_error("JsonVal: key not found: " + key);
        return it->second;
    }
    const JsonVal& operator[](size_t idx) const { return arr[idx]; }
    size_t size() const {
        if (type == Arr_) return arr.size();
        if (type == Obj_) return obj.size();
        return 0;
    }
    const std::map<std::string, JsonVal>& asObj() const { return obj; }
    const std::vector<JsonVal>&           asArr() const { return arr; }
};

// --- parser internals (static functions local to this header) ---

static inline void json_skip_ws(const char*& p)
{
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
}

static inline std::string json_parse_string_tok(const char*& p)
{
    assert(*p == '"'); ++p;
    std::string result;
    while (*p && *p != '"') {
        if (*p == '\\') {
            ++p;
            switch (*p) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case '/':  result += '/';  break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                default:   result += *p;   break;
            }
        } else {
            result += *p;
        }
        ++p;
    }
    if (*p == '"') ++p;
    return result;
}

static JsonVal json_parse_value(const char*& p);

static inline JsonVal json_parse_object(const char*& p)
{
    assert(*p == '{'); ++p;
    JsonVal val; val.type = JsonVal::Obj_;
    json_skip_ws(p);
    if (*p == '}') { ++p; return val; }
    while (true) {
        json_skip_ws(p);
        if (*p != '"')
            throw std::runtime_error("JSON: expected string key in object");
        std::string key = json_parse_string_tok(p);
        json_skip_ws(p);
        if (*p != ':')
            throw std::runtime_error("JSON: expected ':' after key");
        ++p;
        json_skip_ws(p);
        val.obj[key] = json_parse_value(p);
        json_skip_ws(p);
        if (*p == '}') { ++p; break; }
        if (*p == ',') { ++p; continue; }
        throw std::runtime_error("JSON: expected ',' or '}' in object");
    }
    return val;
}

static inline JsonVal json_parse_array(const char*& p)
{
    assert(*p == '['); ++p;
    JsonVal val; val.type = JsonVal::Arr_;
    json_skip_ws(p);
    if (*p == ']') { ++p; return val; }
    while (true) {
        json_skip_ws(p);
        val.arr.push_back(json_parse_value(p));
        json_skip_ws(p);
        if (*p == ']') { ++p; break; }
        if (*p == ',') { ++p; continue; }
        throw std::runtime_error("JSON: expected ',' or ']' in array");
    }
    return val;
}

static JsonVal json_parse_value(const char*& p)
{
    json_skip_ws(p);
    if (!*p) throw std::runtime_error("JSON: unexpected end of input");

    if (*p == '{') return json_parse_object(p);
    if (*p == '[') return json_parse_array(p);
    if (*p == '"') {
        JsonVal v; v.type = JsonVal::Str_;
        v.s = json_parse_string_tok(p);
        return v;
    }
    if (*p == 't') {
        JsonVal v; v.type = JsonVal::Bool_; v.b = true;
        p += 4; return v;
    }
    if (*p == 'f') {
        JsonVal v; v.type = JsonVal::Bool_; v.b = false;
        p += 5; return v;
    }
    if (*p == 'n') { p += 4; return JsonVal(); }

    // Number
    if (*p == '-' || isdigit(*p)) {
        const char* start = p;
        bool is_float = false;
        if (*p == '-') ++p;
        while (isdigit(*p)) ++p;
        if (*p == '.') { is_float = true; ++p; while (isdigit(*p)) ++p; }
        if (*p == 'e' || *p == 'E') {
            is_float = true; ++p;
            if (*p == '+' || *p == '-') ++p;
            while (isdigit(*p)) ++p;
        }
        JsonVal v;
        if (is_float) {
            v.type = JsonVal::Double_;
            v.d = strtod(start, nullptr);
        } else {
            v.type = JsonVal::Int_;
            v.i = strtoll(start, nullptr, 10);
        }
        return v;
    }
    throw std::runtime_error(std::string("JSON: unexpected character '") + *p + "'");
}

static inline JsonVal parse_json(const std::string& text)
{
    const char* p = text.c_str();
    return json_parse_value(p);
}

// ============================================================
// PACE Profile data structures
// ============================================================

struct PacePhase {
    int       phase_index;
    int64_t   total_packets;
    int64_t   total_flits;
    double    flits_per_packet;
    double    data_pct;
    double    ctrl_pct;
    int64_t   sim_ticks;
    uint64_t  network_cycles;
    double    lambda;
    double    avg_packet_latency;

    std::map<int, int64_t> vnet_packets;        // vnet_id -> count
    std::map<int, double>  per_router_injection; // router_id -> fraction (sums to 1)
    std::map<int, double>  dir_fractions;        // dir_id -> fraction (sums to 1)

    // --- derived at load time ---
    std::map<int, double>              per_router_prob;   // router_id -> prob/cycle
    double                             vnet0_prob;
    double                             vnet1_prob;
    double                             vnet2_prob;
    std::vector<std::pair<int,double>> dir_cumulative;    // {dir_id, cumulative_prob}
    double                             response_data_prob; // P(response is 5 flits)
};

struct PaceModelAssumptions {
    int flit_width_bytes  = 16;
    int cacheline_bytes   = 64;
    int data_packet_flits = 5;
    int ctrl_packet_flits = 1;
};

struct PaceProfile {
    int num_cpus;
    int num_dirs;
    int mesh_rows;
    int mesh_cols;
    int mem_channels;
    int num_phases;

    std::vector<PacePhase>      phases;
    PaceModelAssumptions        model;
    std::map<int, int>          directory_remapping; // dir_id -> router_id in target topo

    static PaceProfile load(const std::string& path)
    {
        std::ifstream f(path);
        if (!f.is_open())
            throw std::runtime_error("Cannot open PACE profile: " + path);
        std::stringstream buf;
        buf << f.rdbuf();
        JsonVal root = parse_json(buf.str());

        PaceProfile prof;
        prof.num_cpus      = (int)root["num_cpus"].asInt();
        prof.num_dirs      = (int)root["num_dirs"].asInt();
        prof.mesh_rows     = (int)root["mesh_rows"].asInt();
        prof.mesh_cols     = (int)root["mesh_cols"].asInt();
        prof.mem_channels  = (int)root["mem_channels"].asInt();
        prof.num_phases    = (int)root["num_phases"].asInt();

        // model_assumptions (optional)
        if (root.hasKey("model_assumptions")) {
            const auto& ma = root["model_assumptions"];
            prof.model.flit_width_bytes  = (int)ma["flit_width_bytes"].asInt();
            prof.model.cacheline_bytes   = (int)ma["cacheline_bytes"].asInt();
            prof.model.data_packet_flits = (int)ma["data_packet_flits"].asInt();
            prof.model.ctrl_packet_flits = (int)ma["ctrl_packet_flits"].asInt();
        }

        // directory_remapping (optional; default = identity)
        if (root.hasKey("directory_remapping")) {
            for (const auto& kv : root["directory_remapping"].asObj())
                prof.directory_remapping[std::stoi(kv.first)] = (int)kv.second.asInt();
        } else {
            for (int d = 0; d < prof.num_dirs; ++d)
                prof.directory_remapping[d] = d;
        }

        // phases
        for (const auto& pv : root["phases"].asArr()) {
            PacePhase ph;
            ph.phase_index       = (int)pv["phase_index"].asInt();
            ph.total_packets     = pv["total_packets"].asInt();
            ph.total_flits       = pv["total_flits"].asInt();
            ph.flits_per_packet  = pv["flits_per_packet"].asDouble();
            ph.data_pct          = pv["data_pct"].asDouble();
            ph.ctrl_pct          = pv["ctrl_pct"].asDouble();
            ph.sim_ticks         = pv["sim_ticks"].asInt();
            ph.network_cycles    = (uint64_t)pv["network_cycles"].asInt();
            // gem5 22.1 does not emit network.cycles, so extraction profiles
            // often have network_cycles = 0.  Fall back to sim_ticks / 333
            // (3 GHz Ruby clock: 1 cycle = 1e12/3e9 ≈ 333 ticks).
            if (ph.network_cycles == 0 && ph.sim_ticks > 0)
                ph.network_cycles = (uint64_t)(ph.sim_ticks / 333);
            ph.lambda            = pv["lambda"].asDouble();
            ph.avg_packet_latency = pv["avg_packet_latency"].asDouble();

            for (const auto& kv : pv["vnet_packets"].asObj())
                ph.vnet_packets[std::stoi(kv.first)] = kv.second.asInt();

            for (const auto& kv : pv["per_router_injection"].asObj())
                ph.per_router_injection[std::stoi(kv.first)] = kv.second.asDouble();

            for (const auto& kv : pv["dir_fractions"].asObj())
                ph.dir_fractions[std::stoi(kv.first)] = kv.second.asDouble();

            // Derive per-router injection probabilities
            for (const auto& kv : ph.per_router_injection)
                ph.per_router_prob[kv.first] =
                    ph.lambda * prof.num_cpus * kv.second;

            // Derive vnet selection probabilities
            int64_t total_pkt = ph.total_packets > 0 ? ph.total_packets : 1;
            auto vpkt = [&](int v) -> int64_t {
                auto it = ph.vnet_packets.find(v);
                return it != ph.vnet_packets.end() ? it->second : 0;
            };
            ph.vnet0_prob = (double)vpkt(0) / total_pkt;
            ph.vnet1_prob = (double)vpkt(1) / total_pkt;
            ph.vnet2_prob = 1.0 - ph.vnet0_prob - ph.vnet1_prob;
            if (ph.vnet2_prob < 0.0) ph.vnet2_prob = 0.0;

            // Derive cumulative dir_fractions for weighted selection
            double cum = 0.0;
            for (const auto& kv : ph.dir_fractions) {
                cum += kv.second;
                ph.dir_cumulative.push_back({kv.first, cum});
            }

            // Derive response data probability
            // vnet1_flits = total_flits - vnet0_flits(=vpkt(0)) - vnet2_flits(=vpkt(2)*data_flits)
            int64_t v0f = vpkt(0);          // ctrl: 1 flit each
            int64_t v2f = vpkt(2) * prof.model.data_packet_flits; // data: 5 flits each
            int64_t v1f = ph.total_flits - v0f - v2f;
            int64_t v1p = vpkt(1) > 0 ? vpkt(1) : 1;
            double data_resp = (double)(v1f - v1p) / (4.0 * v1p);
            ph.response_data_prob = std::max(0.0, std::min(1.0, data_resp));

            prof.phases.push_back(ph);
        }

        return prof;
    }
};

} // namespace garnet

#endif // __PACE_PROFILE_HH__
