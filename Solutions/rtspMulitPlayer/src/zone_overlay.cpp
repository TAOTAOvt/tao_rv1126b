#include "zone_overlay.h"
#include <fstream>
#include <sstream>

static std::vector<ChnZones_t> g_ChnZones;

static cv::Scalar zoneColor(ZoneLevel_t lvl) {
    switch(lvl) {
        case ZONE_GREEN:  return {0, 180, 0};
        case ZONE_YELLOW: return {0, 200, 200};
        case ZONE_RED:    return {0, 0, 200};
        default:          return {128,128,128};
    }
}

// -------------------------------------------------------
// Mini JSON parser — chỉ đọc zones.json của mình
// Không dùng thư viện ngoài
// -------------------------------------------------------
static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n\"");
    size_t b = s.find_last_not_of(" \t\r\n\"");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static std::string getVal(const std::string &line, const std::string &key) {
    size_t pos = line.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = line.find(":", pos);
    if (pos == std::string::npos) return "";
    return trim(line.substr(pos + 1));
}

int zone_load_config(const char *jsonPath,
                     int cellW, int cellH,
                     float scaleX, float scaleY)
{
    std::ifstream f(jsonPath);
    if (!f.is_open()) {
        printf("[ZONE] Cannot open %s\n", jsonPath);
        return -1;
    }

    // Đọc toàn bộ file vào string
    std::stringstream buf;
    buf << f.rdbuf();
    std::string raw = buf.str();

    g_ChnZones.clear();

    // Parse thủ công — tìm từng block "chnId"
    size_t pos = 0;
    while ((pos = raw.find("\"chnId\"", pos)) != std::string::npos) {
        ChnZones_t cz;
        cz.cacheReady = false;

        // Lấy chnId
        size_t colon = raw.find(":", pos);
        size_t comma = raw.find(",", colon);
        cz.chnId = std::stoi(trim(raw.substr(colon + 1, comma - colon - 1)));
        pos = comma;

        // Tìm "zones" array của camera này
        size_t zonesStart = raw.find("\"zones\"", pos);
        if (zonesStart == std::string::npos) break;
        size_t arrStart = raw.find("[", zonesStart);
        if (arrStart == std::string::npos) break;

        // Tìm hết array zones — đếm dấu []
        int depth = 0;
        size_t arrEnd = arrStart;
        for (size_t i = arrStart; i < raw.size(); i++) {
            if (raw[i] == '[') depth++;
            else if (raw[i] == ']') { depth--; if (depth == 0) { arrEnd = i; break; } }
        }
        std::string zonesStr = raw.substr(arrStart, arrEnd - arrStart + 1);

        // Parse từng zone object {}
        size_t zpos = 0;
        while ((zpos = zonesStr.find("{", zpos)) != std::string::npos) {
            size_t zend = zonesStr.find("}", zpos);
            // Tìm } ngoài cùng (vì points có [])
            int d2 = 0;
            for (size_t k = zpos; k < zonesStr.size(); k++) {
                if (zonesStr[k] == '{') d2++;
                else if (zonesStr[k] == '}') { d2--; if (d2 == 0) { zend = k; break; } }
            }
            std::string zobj = zonesStr.substr(zpos, zend - zpos + 1);

            Zone_t zone;

            // id
            std::string idStr = getVal(zobj, "id");
            zone.id = idStr.empty() ? 0 : std::stoi(idStr);

            // label
            zone.label = getVal(zobj, "label");

            // level
            std::string lvl = getVal(zobj, "level");
            if      (lvl == "red")    zone.level = ZONE_RED;
            else if (lvl == "yellow") zone.level = ZONE_YELLOW;
            else                      zone.level = ZONE_GREEN;

            // points [[x,y],[x,y],...]
            size_t pStart = zobj.find("\"points\"");
            if (pStart != std::string::npos) {
                size_t arrP = zobj.find("[", pStart);
                size_t arrPEnd = zobj.rfind("]");
                std::string ptsStr = zobj.substr(arrP + 1, arrPEnd - arrP - 1);
                // Tìm từng [x,y]
                size_t pp = 0;
                while ((pp = ptsStr.find("[", pp)) != std::string::npos) {
                    size_t pe = ptsStr.find("]", pp);
                    std::string pt = ptsStr.substr(pp + 1, pe - pp - 1);
                    size_t cm = pt.find(",");
                    if (cm != std::string::npos) {
                        int px = (int)(std::stof(trim(pt.substr(0, cm)))  * scaleX);
                        int py = (int)(std::stof(trim(pt.substr(cm + 1))) * scaleY);
                        zone.points.emplace_back(px, py);
                    }
                    pp = pe + 1;
                }
            }

            if (!zone.points.empty())
                cz.zones.push_back(zone);

            zpos = zend + 1;
        }

        // Pre-render cache
        cz.overlayBGR = cv::Mat::zeros(cellH, cellW, CV_8UC3);
        for (auto &zone : cz.zones) {
            cv::Scalar col = zoneColor(zone.level);
            std::vector<std::vector<cv::Point>> pts = {zone.points};
            cv::fillPoly(cz.overlayBGR, pts, col * 0.6);
            cv::polylines(cz.overlayBGR, pts, true, col, 2, cv::LINE_AA);
            cv::Rect bbox = cv::boundingRect(zone.points);
            cv::putText(cz.overlayBGR, zone.label,
                        cv::Point(bbox.x + 4, bbox.y + 20),
                        cv::FONT_HERSHEY_SIMPLEX, 0.55,
                        cv::Scalar(255,255,255), 1, cv::LINE_AA);
        }
        cz.cacheReady = true;
        printf("[ZONE] chnId=%d loaded %zu zones\n", cz.chnId, cz.zones.size());
        g_ChnZones.push_back(cz);

        pos = arrEnd;
    }

    return 0;
}

void zone_apply_overlay(cv::Mat &cell, int chnId)
{
    for (auto &cz : g_ChnZones) {
        if (cz.chnId != chnId || !cz.cacheReady) continue;
        cv::addWeighted(cell, 1.0, cz.overlayBGR, 0.4, 0, cell);
        return;
    }
}

void zone_free() { g_ChnZones.clear(); }