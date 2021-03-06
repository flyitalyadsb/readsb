#include "readsb.h"

/*
__attribute__ ((format(printf, 3, 0))) static char *safe_vsnprintf(char *p, char *end, const char *format, va_list ap) {
    p += vsnprintf(p < end ? p : NULL, p < end ? (size_t) (end - p) : 0, format, ap);
    return p;
}
*/

static const char *trimSpace(const char *in, char *out, int len) {

    out[len] = '\0';
    int found = 0;

    for (int i = len - 1; i >= 0; i--) {
        if (!found && in[i] == ' ') {
            out[i] = '\0';
        } else if (in[i] == '\0') {
            out[i] = '\0';
        } else {
            out[i] = in[i];
            found = 1; // found non space character
        }
    }

    return out;
}
//
//=========================================================================
//
// Return a description of planes in json. No metric conversion
//
static const char *jsonEscapeString(const char *str, char *buf, int len) {
    const char *in = str;
    char *out = buf, *end = buf + len - 10;

    for (; *in && out < end; ++in) {
        unsigned char ch = *in;
        if (ch == '"' || ch == '\\') {
            *out++ = '\\';
            *out++ = ch;
        } else if (ch < 32 || ch > 126) {
            out = safe_snprintf(out, end, "\\u%04x", ch);
        } else {
            *out++ = ch;
        }
    }

    *out++ = 0;
    return buf;
}

static char *append_flags(char *p, char *end, struct aircraft *a, datasource_t source) {
    p = safe_snprintf(p, end, "[");

    char *start = p;
    if (a->callsign_valid.source == source)
        p = safe_snprintf(p, end, "\"callsign\",");
    if (a->altitude_baro_valid.source == source)
        p = safe_snprintf(p, end, "\"altitude\",");
    if (a->altitude_geom_valid.source == source)
        p = safe_snprintf(p, end, "\"alt_geom\",");
    if (a->gs_valid.source == source)
        p = safe_snprintf(p, end, "\"gs\",");
    if (a->ias_valid.source == source)
        p = safe_snprintf(p, end, "\"ias\",");
    if (a->tas_valid.source == source)
        p = safe_snprintf(p, end, "\"tas\",");
    if (a->mach_valid.source == source)
        p = safe_snprintf(p, end, "\"mach\",");
    if (a->track_valid.source == source)
        p = safe_snprintf(p, end, "\"track\",");
    if (a->track_rate_valid.source == source)
        p = safe_snprintf(p, end, "\"track_rate\",");
    if (a->roll_valid.source == source)
        p = safe_snprintf(p, end, "\"roll\",");
    if (a->mag_heading_valid.source == source)
        p = safe_snprintf(p, end, "\"mag_heading\",");
    if (a->true_heading_valid.source == source)
        p = safe_snprintf(p, end, "\"true_heading\",");
    if (a->baro_rate_valid.source == source)
        p = safe_snprintf(p, end, "\"baro_rate\",");
    if (a->geom_rate_valid.source == source)
        p = safe_snprintf(p, end, "\"geom_rate\",");
    if (a->squawk_valid.source == source)
        p = safe_snprintf(p, end, "\"squawk\",");
    if (a->emergency_valid.source == source)
        p = safe_snprintf(p, end, "\"emergency\",");
    if (a->nav_qnh_valid.source == source)
        p = safe_snprintf(p, end, "\"nav_qnh\",");
    if (a->nav_altitude_mcp_valid.source == source)
        p = safe_snprintf(p, end, "\"nav_altitude_mcp\",");
    if (a->nav_altitude_fms_valid.source == source)
        p = safe_snprintf(p, end, "\"nav_altitude_fms\",");
    if (a->nav_heading_valid.source == source)
        p = safe_snprintf(p, end, "\"nav_heading\",");
    if (a->nav_modes_valid.source == source)
        p = safe_snprintf(p, end, "\"nav_modes\",");
    if (a->position_valid.source == source)
        p = safe_snprintf(p, end, "\"lat\",\"lon\",\"nic\",\"rc\",");
    if (a->nic_baro_valid.source == source)
        p = safe_snprintf(p, end, "\"nic_baro\",");
    if (a->nac_p_valid.source == source)
        p = safe_snprintf(p, end, "\"nac_p\",");
    if (a->nac_v_valid.source == source)
        p = safe_snprintf(p, end, "\"nac_v\",");
    if (a->sil_valid.source == source)
        p = safe_snprintf(p, end, "\"sil\",\"sil_type\",");
    if (a->gva_valid.source == source)
        p = safe_snprintf(p, end, "\"gva\",");
    if (a->sda_valid.source == source)
        p = safe_snprintf(p, end, "\"sda\",");
    if (p != start)
        --p;
    p = safe_snprintf(p, end, "]");
    return p;
}

static struct {
    nav_modes_t flag;
    const char *name;
} nav_modes_names[] = {
    { NAV_MODE_AUTOPILOT, "autopilot"},
    { NAV_MODE_VNAV, "vnav"},
    { NAV_MODE_ALT_HOLD, "althold"},
    { NAV_MODE_APPROACH, "approach"},
    { NAV_MODE_LNAV, "lnav"},
    { NAV_MODE_TCAS, "tcas"},
    { 0, NULL}
};

static char *append_nav_modes(char *p, char *end, nav_modes_t flags, const char *quote, const char *sep) {
    int first = 1;
    for (int i = 0; nav_modes_names[i].name; ++i) {
        if (!(flags & nav_modes_names[i].flag)) {
            continue;
        }

        if (!first) {
            p = safe_snprintf(p, end, "%s", sep);
        }

        first = 0;
        p = safe_snprintf(p, end, "%s%s%s", quote, nav_modes_names[i].name, quote);
    }

    return p;
}

const char *nav_modes_flags_string(nav_modes_t flags) {
    static char buf[256];
    buf[0] = 0;
    append_nav_modes(buf, buf + sizeof (buf), flags, "", " ");
    return buf;
}

char *sprintAircraftObject(char *p, char *end, struct aircraft *a, uint64_t now, int printMode) {

    // printMode == 0: aircraft.json
    // printMode == 1: trace.json
    // printMode == 2: jsonPositionOutput
    // printMode == 3: globe.json

    p = safe_snprintf(p, end, "\n{");
    if (printMode == 2)
        p = safe_snprintf(p, end, "\"now\" : %.1f,", now / 1000.0);
    if (printMode != 1)
        p = safe_snprintf(p, end, "\"hex\":\"%s%06x\",", (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);
    p = safe_snprintf(p, end, "\"type\":\"%s\"", addrtype_enum_string(a->addrtype));
    if (trackDataValid(&a->callsign_valid)) {
        char buf[128];
        p = safe_snprintf(p, end, ",\"flight\":\"%s\"", jsonEscapeString(a->callsign, buf, sizeof(buf)));
    }
    if (Modes.db) {
        if (printMode != 1) {
            if (a->registration[0])
                p = safe_snprintf(p, end, ",\"r\":\"%.*s\"", (int) sizeof(a->registration), a->registration);
            if (a->typeCode[0])
                p = safe_snprintf(p, end, ",\"t\":\"%.*s\"", (int) sizeof(a->typeCode), a->typeCode);
            if (a->dbFlags)
                p = safe_snprintf(p, end, ",\"dbFlags\":%u", a->dbFlags);

            if (Modes.jsonLongtype && a->typeLong[0])
                p = safe_snprintf(p, end, ",\"desc\":\"%.*s\"", (int) sizeof(a->typeLong), a->typeLong);
        }
    }
    if (printMode != 1) {
        if (trackDataValid(&a->airground_valid) && a->airground == AG_GROUND)
            if (printMode == 2)
                p = safe_snprintf(p, end, ",\"ground\":true");
            else
                p = safe_snprintf(p, end, ",\"alt_baro\":\"ground\"");
        else {
            if (altReliable(a))
                p = safe_snprintf(p, end, ",\"alt_baro\":%d", a->altitude_baro);
            if (printMode == 2)
                p = safe_snprintf(p, end, ",\"ground\":false");
        }
    }
    if (trackDataValid(&a->altitude_geom_valid))
        p = safe_snprintf(p, end, ",\"alt_geom\":%d", a->altitude_geom);
    if (printMode != 1 && trackDataValid(&a->gs_valid))
        p = safe_snprintf(p, end, ",\"gs\":%.1f", a->gs);
    if (trackDataValid(&a->ias_valid))
        p = safe_snprintf(p, end, ",\"ias\":%u", a->ias);
    if (trackDataValid(&a->tas_valid))
        p = safe_snprintf(p, end, ",\"tas\":%u", a->tas);
    if (trackDataValid(&a->mach_valid))
        p = safe_snprintf(p, end, ",\"mach\":%.3f", a->mach);
    if (now < a->wind_updated + TRACK_EXPIRE && abs(a->wind_altitude - a->altitude_baro) < 500) {
        p = safe_snprintf(p, end, ",\"wd\":%.0f", a->wind_direction);
        p = safe_snprintf(p, end, ",\"ws\":%.0f", a->wind_speed);
    }
    if (now < a->oat_updated + TRACK_EXPIRE) {
        p = safe_snprintf(p, end, ",\"oat\":%.0f", a->oat);
        p = safe_snprintf(p, end, ",\"tat\":%.0f", a->tat);
    }

    if (trackDataValid(&a->track_valid))
        p = safe_snprintf(p, end, ",\"track\":%.2f", a->track);
    else if (printMode != 1 && trackDataValid(&a->position_valid) &&
        !(trackDataValid(&a->airground_valid) && a->airground == AG_GROUND))
        p = safe_snprintf(p, end, ",\"calc_track\":%.0f", a->calc_track);

    if (trackDataValid(&a->track_rate_valid))
        p = safe_snprintf(p, end, ",\"track_rate\":%.2f", a->track_rate);
    if (trackDataValid(&a->roll_valid))
        p = safe_snprintf(p, end, ",\"roll\":%.2f", a->roll);
    if (trackDataValid(&a->mag_heading_valid))
        p = safe_snprintf(p, end, ",\"mag_heading\":%.2f", a->mag_heading);
    if (trackDataValid(&a->true_heading_valid))
        p = safe_snprintf(p, end, ",\"true_heading\":%.2f", a->true_heading);
    if (trackDataValid(&a->baro_rate_valid))
        p = safe_snprintf(p, end, ",\"baro_rate\":%d", a->baro_rate);
    if (trackDataValid(&a->geom_rate_valid))
        p = safe_snprintf(p, end, ",\"geom_rate\":%d", a->geom_rate);
    if (trackDataValid(&a->squawk_valid))
        p = safe_snprintf(p, end, ",\"squawk\":\"%04x\"", a->squawk);
    if (trackDataValid(&a->emergency_valid))
        p = safe_snprintf(p, end, ",\"emergency\":\"%s\"", emergency_enum_string(a->emergency));
    if (a->category != 0)
        p = safe_snprintf(p, end, ",\"category\":\"%02X\"", a->category);
    if (trackDataValid(&a->nav_qnh_valid))
        p = safe_snprintf(p, end, ",\"nav_qnh\":%.1f", a->nav_qnh);
    if (trackDataValid(&a->nav_altitude_mcp_valid))
        p = safe_snprintf(p, end, ",\"nav_altitude_mcp\":%d", a->nav_altitude_mcp);
    if (trackDataValid(&a->nav_altitude_fms_valid))
        p = safe_snprintf(p, end, ",\"nav_altitude_fms\":%d", a->nav_altitude_fms);
    if (trackDataValid(&a->nav_heading_valid))
        p = safe_snprintf(p, end, ",\"nav_heading\":%.2f", a->nav_heading);
    if (trackDataValid(&a->nav_modes_valid)) {
        p = safe_snprintf(p, end, ",\"nav_modes\":[");
        p = append_nav_modes(p, end, a->nav_modes, "\"", ",");
        p = safe_snprintf(p, end, "]");
    }
    if (printMode != 1) {
        if (posReliable(a)) {
            p = safe_snprintf(p, end, ",\"lat\":%f,\"lon\":%f,\"nic\":%u,\"rc\":%u,\"seen_pos\":%.1f",
                    a->lat, a->lon, a->pos_nic, a->pos_rc,
                    (now < a->position_valid.updated) ? 0 : ((now - a->position_valid.updated) / 1000.0));
        } else if (now < a->rr_seen + 2 * MINUTES) {
            p = safe_snprintf(p, end, ",\"rr_lat\":%.1f,\"rr_lon\":%.1f", a->rr_lat, a->rr_lon);
        }
    }

    if (printMode == 1 && trackDataValid(&a->position_valid)) {
        p = safe_snprintf(p, end, ",\"nic\":%u,\"rc\":%u",
                a->pos_nic, a->pos_rc);
    }
    if (a->adsb_version >= 0)
        p = safe_snprintf(p, end, ",\"version\":%d", a->adsb_version);
    if (trackDataValid(&a->nic_baro_valid))
        p = safe_snprintf(p, end, ",\"nic_baro\":%u", a->nic_baro);
    if (trackDataValid(&a->nac_p_valid))
        p = safe_snprintf(p, end, ",\"nac_p\":%u", a->nac_p);
    if (trackDataValid(&a->nac_v_valid))
        p = safe_snprintf(p, end, ",\"nac_v\":%u", a->nac_v);
    if (trackDataValid(&a->sil_valid))
        p = safe_snprintf(p, end, ",\"sil\":%u", a->sil);
    if (a->sil_type != SIL_INVALID)
        p = safe_snprintf(p, end, ",\"sil_type\":\"%s\"", sil_type_enum_string(a->sil_type));
    if (trackDataValid(&a->gva_valid))
        p = safe_snprintf(p, end, ",\"gva\":%u", a->gva);
    if (trackDataValid(&a->sda_valid))
        p = safe_snprintf(p, end, ",\"sda\":%u", a->sda);
    if (trackDataValid(&a->alert_valid))
        p = safe_snprintf(p, end, ",\"alert\":%u", a->alert);
    if (trackDataValid(&a->spi_valid))
        p = safe_snprintf(p, end, ",\"spi\":%u", a->spi);

    /*
    if (a->position_valid.source == SOURCE_JAERO)
        p = safe_snprintf(p, end, ",\"jaero\": true");
    if (a->position_valid.source == SOURCE_SBS)
        p = safe_snprintf(p, end, ",\"sbs_other\": true");
    */
    if (Modes.netReceiverIdPrint) {
        p = safe_snprintf(p, end, ",\"rId\":%016"PRIx64"", a->lastPosReceiverId);
    }

    if (printMode != 1) {
        p = safe_snprintf(p, end, ",\"mlat\":");
        p = append_flags(p, end, a, SOURCE_MLAT);
        p = safe_snprintf(p, end, ",\"tisb\":");
        p = append_flags(p, end, a, SOURCE_TISB);

        p = safe_snprintf(p, end, ",\"messages\":%u,\"seen\":%.1f,\"rssi\":%.1f}",
                a->messages, (now < a->seen) ? 0 : ((now - a->seen) / 1000.0),
                10 * log10((a->signalLevel[0] + a->signalLevel[1] + a->signalLevel[2] + a->signalLevel[3] +
                        a->signalLevel[4] + a->signalLevel[5] + a->signalLevel[6] + a->signalLevel[7]) / 8 + 1.125e-5));
    } else {
        p = safe_snprintf(p, end, "}");
    }

    return p;
}

/*
static void check_state_all(struct aircraft *test, uint64_t now) {
    size_t buflen = 4096;
    char buffer1[buflen];
    char buffer2[buflen];
    char *buf, *p, *end;

    struct aircraft abuf = *test;
    struct aircraft *a = &abuf;

    buf = buffer1;
    p = buf;
    end = buf + buflen;
    p = sprintAircraftObject(p, end, a, now, 1);

    buf = buffer2;
    p = buf;
    end = buf + buflen;


    struct state_all state_buf;
    memset(&state_buf, 0, sizeof(struct state_all));
    struct state_all *new_all = &state_buf;
    to_state_all(a, new_all, now);

    struct aircraft bbuf;
    memset(&bbuf, 0, sizeof(struct aircraft));
    struct aircraft *b = &bbuf;

    from_state_all(new_all, b, now);

    p = sprintAircraftObject(p, end, b, now, 1);

    if (strncmp(buffer1, buffer2, buflen)) {
        fprintf(stderr, "%s\n%s\n", buffer1, buffer2);
    }
}
*/
struct char_buffer generateGlobeBin(int globe_index, int mil) {
    struct char_buffer cb;
    uint64_t now = mstime();
    struct aircraft *a;
    size_t buflen = 1*1024*1024; // The initial buffer is resized as needed
    char *buf = (char *) malloc(buflen), *p = buf, *end = buf + buflen;

    uint32_t elementSize = sizeof(struct binCraft);
    memset(p, 0, elementSize);

#define memWrite(p, var) do { memcpy(p, &var, sizeof(var)); p += sizeof(var); } while(0)

    memWrite(p, now);

    memWrite(p, elementSize);

    uint32_t ac_count_pos = Modes.globalStatsCount.json_ac_count_pos;
    memWrite(p, ac_count_pos);

    uint32_t index = globe_index < 0 ? 42777 : globe_index;
    memWrite(p, index);

    int16_t south = -90;
    int16_t west = -180;
    int16_t north = 90;
    int16_t east = 180;

    if (globe_index >= GLOBE_MIN_INDEX) {
        int grid = GLOBE_INDEX_GRID;
        south = ((globe_index - GLOBE_MIN_INDEX) / GLOBE_LAT_MULT) * grid - 90;
        west = ((globe_index - GLOBE_MIN_INDEX) % GLOBE_LAT_MULT) * grid - 180;
        north = south + grid;
        east = west + grid;
    } else if (globe_index >= 0) {
        struct tile *tiles = Modes.json_globe_special_tiles;
        struct tile tile = tiles[globe_index];
        south = tile.south;
        west = tile.west;
        north = tile.north;
        east = tile.east;
    }

    memWrite(p, south);
    memWrite(p, west);
    memWrite(p, north);
    memWrite(p, east);

    if (p - buf > (int) elementSize)
        fprintf(stderr, "buffer overrun globeBin\n");

    p = buf + elementSize;

    struct craftArray *ca = NULL;
    int good;
    if (globe_index == -1) {
        ca = &Modes.aircraftActive;
        good = 1;
    } else if (globe_index <= GLOBE_MAX_INDEX) {
        ca = &Modes.globeLists[globe_index];
        good = 1;
    } else {
        fprintf(stderr, "generateAircraftJson: bad globe_index: %d\n", globe_index);
        good = 0;
    }
    if (good && ca->list) {
        for (int i = 0; i < ca->len; i++) {
            a = ca->list[i];

            if (a == NULL)
                continue;
            if (mil && !(a->dbFlags & 1))
                continue;

            int use = 0;

            if (a->position_valid.source == SOURCE_JAERO)
                use = 1;
            if (now < a->seenPosReliable + 2 * MINUTES)
                use = 1;

            if (!use)
                continue;

            // check if we have enough space
            if ((p + 1000) >= end) {
                int used = p - buf;
                buflen *= 2;
                buf = (char *) realloc(buf, buflen);
                p = buf + used;
                end = buf + buflen;
            }

            struct binCraft bin;
            toBinCraft(a, &bin, now);

            memWrite(p, bin);

            if (p >= end)
                fprintf(stderr, "buffer overrun globeBin\n");
        }
    }

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;

#undef memWrite
}

struct char_buffer generateGlobeJson(int globe_index){
    struct char_buffer cb;
    uint64_t now = mstime();
    struct aircraft *a;
    size_t buflen = 1*1024*1024; // The initial buffer is resized as needed
    char *buf = (char *) malloc(buflen), *p = buf, *end = buf + buflen;

    p = safe_snprintf(p, end,
            "{ \"now\" : %.1f,\n"
            "  \"messages\" : %u,\n",
            now / 1000.0,
            Modes.stats_current.messages_total + Modes.stats_alltime.messages_total);

    p = safe_snprintf(p, end,
            "  \"global_ac_count_withpos\" : %d,\n",
            Modes.globalStatsCount.json_ac_count_pos
            );

    p = safe_snprintf(p, end, "  \"globeIndex\" : %d, ", globe_index);
    if (globe_index >= GLOBE_MIN_INDEX) {
        int grid = GLOBE_INDEX_GRID;
        int lat = ((globe_index - GLOBE_MIN_INDEX) / GLOBE_LAT_MULT) * grid - 90;
        int lon = ((globe_index - GLOBE_MIN_INDEX) % GLOBE_LAT_MULT) * grid - 180;
        p = safe_snprintf(p, end,
                "\"south\" : %d, "
                "\"west\" : %d, "
                "\"north\" : %d, "
                "\"east\" : %d,\n",
                lat,
                lon,
                lat + grid,
                lon + grid);
    } else {
        struct tile *tiles = Modes.json_globe_special_tiles;
        struct tile tile = tiles[globe_index];
        p = safe_snprintf(p, end,
                "\"south\" : %d, "
                "\"west\" : %d, "
                "\"north\" : %d, "
                "\"east\" : %d,\n",
                tile.south,
                tile.west,
                tile.north,
                tile.east);
    }

    p = safe_snprintf(p, end, "  \"aircraft\" : [");

    struct craftArray *ca = NULL;
    int good;
    if (globe_index <= GLOBE_MAX_INDEX) {
        ca = &Modes.globeLists[globe_index];
        good = 1;
    } else {
        fprintf(stderr, "generateAircraftJson: bad globe_index: %d\n", globe_index);
        good = 0;
    }
    if (good && ca->list) {
        for (int i = 0; i < ca->len; i++) {
            a = ca->list[i];

            if (a == NULL)
                continue;

            int use = 0;

            if (a->position_valid.source == SOURCE_JAERO)
                use = 1;
            if (now < a->seenPosReliable + 2 * MINUTES)
                use = 1;

            if (!use)
                continue;

            // check if we have enough space
            if ((p + 1000) >= end) {
                int used = p - buf;
                buflen *= 2;
                buf = (char *) realloc(buf, buflen);
                p = buf + used;
                end = buf + buflen;
            }

            p = sprintAircraftObject(p, end, a, now, 3);

            *p++ = ',';

            if (p >= end)
                fprintf(stderr, "buffer overrun aircraft json\n");
        }
    }
    if (*(p-1) == ',')
        p--;

    p = safe_snprintf(p, end, "\n  ]\n}\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

struct char_buffer generateAircraftJson(){
    struct char_buffer cb;
    uint64_t now = mstime();
    struct aircraft *a;
    size_t buflen = 6*1024*1024; // The initial buffer is resized as needed
    char *buf = (char *) malloc(buflen), *p = buf, *end = buf + buflen;

    p = safe_snprintf(p, end,
            "{ \"now\" : %.1f,\n"
            "  \"messages\" : %u,\n",
            now / 1000.0,
            Modes.stats_current.messages_total + Modes.stats_alltime.messages_total);

    p = safe_snprintf(p, end, "  \"aircraft\" : [");

    //for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
    //    for (a = Modes.aircraft[j]; a; a = a->next) {

    struct craftArray *ca = &Modes.aircraftActive;

    for (int i = 0; i < ca->len; i++) {
        a = ca->list[i];

        if (a == NULL)
            continue;
        //fprintf(stderr, "a: %05x\n", a->addr);

        // don't include stale aircraft in the JSON
        if (a->position_valid.source != SOURCE_JAERO
                && now > a->seen + TRACK_EXPIRE / 2
                && now > a->seenPosReliable + TRACK_EXPIRE
           ) {
            continue;
        }
        if (a->messages < 2)
            continue;

        // check if we have enough space
        if ((p + 1000) >= end) {
            int used = p - buf;
            buflen *= 2;
            buf = (char *) realloc(buf, buflen);
            p = buf + used;
            end = buf + buflen;
        }

        p = sprintAircraftObject(p, end, a, now, 0);

        *p++ = ',';

        if (p >= end)
            fprintf(stderr, "buffer overrun aircraft json\n");
    }

    if (*(p-1) == ',')
        p--;

    p = safe_snprintf(p, end, "\n  ]\n}\n");

    //    fprintf(stderr, "%u\n", ac_counter);

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

struct char_buffer generateTraceJson(struct aircraft *a, int start, int last) {
    struct char_buffer cb;
    size_t buflen = a->trace_len * 300 + 1024;

    if (last < 0)
        last = a->trace_len - 1;

    if (!Modes.json_globe_index) {
        cb.len = 0;
        cb.buffer = NULL;
        return cb;
    }

    char *buf = (char *) malloc(buflen), *p = buf, *end = buf + buflen;

    p = safe_snprintf(p, end, "{\"icao\":\"%s%06x\"", (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);

    if (Modes.db) {
        char *regInfo = p;
        if (a->registration[0])
            p = safe_snprintf(p, end, ",\n\"r\":\"%.*s\"", (int) sizeof(a->registration), a->registration);
        if (a->typeCode[0])
            p = safe_snprintf(p, end, ",\n\"t\":\"%.*s\"", (int) sizeof(a->typeCode), a->typeCode);
        if (a->typeLong[0])
            p = safe_snprintf(p, end, ",\n\"desc\":\"%.*s\"", (int) sizeof(a->typeLong), a->typeLong);
        if (a->dbFlags)
            p = safe_snprintf(p, end, ",\n\"dbFlags\":%u", a->dbFlags);
        if (p == regInfo)
            p = safe_snprintf(p, end, ",\n\"noRegData\":true");
    }

    if (start <= last && last < a->trace_len) {
        p = safe_snprintf(p, end, ",\n\"timestamp\": %.3f", (a->trace + start)->timestamp / 1000.0);

        p = safe_snprintf(p, end, ",\n\"trace\":[ ");

        for (int i = start; i <= last; i++) {
            struct state *trace = &a->trace[i];

            int32_t altitude = trace->altitude * 25;
            int32_t rate = trace->rate * 32;
            int rate_valid = trace->flags.rate_valid;
            int rate_geom = trace->flags.rate_geom;
            int stale = trace->flags.stale;
            int on_ground = trace->flags.on_ground;
            int altitude_valid = trace->flags.altitude_valid;
            int gs_valid = trace->flags.gs_valid;
            int track_valid = trace->flags.track_valid;
            int leg_marker = trace->flags.leg_marker;
            int altitude_geom = trace->flags.altitude_geom;

                // in the air
                p = safe_snprintf(p, end, "\n[%.1f,%f,%f",
                        (trace->timestamp - (a->trace + start)->timestamp) / 1000.0, trace->lat / 1E6, trace->lon / 1E6);

                if (on_ground)
                    p = safe_snprintf(p, end, ",\"ground\"");
                else if (altitude_valid)
                    p = safe_snprintf(p, end, ",%d", altitude);
                else
                    p = safe_snprintf(p, end, ",null");

                if (gs_valid)
                    p = safe_snprintf(p, end, ",%.1f", trace->gs / 10.0);
                else
                    p = safe_snprintf(p, end, ",null");

                if (track_valid)
                    p = safe_snprintf(p, end, ",%.1f", trace->track / 10.0);
                else
                    p = safe_snprintf(p, end, ",null");

                int bitfield = (altitude_geom << 3) | (rate_geom << 2) | (leg_marker << 1) | (stale << 0);
                p = safe_snprintf(p, end, ",%d", bitfield);

                if (rate_valid)
                    p = safe_snprintf(p, end, ",%d", rate);
                else
                    p = safe_snprintf(p, end, ",null");

                if (i % 4 == 0) {
                    uint64_t now = trace->timestamp;
                    struct state_all *all = &(a->trace_all[i/4]);
                    struct aircraft b;
                    memset(&b, 0, sizeof(struct aircraft));
                    struct aircraft *ac = &b;
                    from_state_all(all, ac, now);

                    p = safe_snprintf(p, end, ",");
                    p = sprintAircraftObject(p, end, ac, now, 1);
                } else {
                    p = safe_snprintf(p, end, ",null");
                }
                p = safe_snprintf(p, end, "],");
        }

        p--; // remove last comma

        p = safe_snprintf(p, end, " ]\n");
    }

    p = safe_snprintf(p, end, " }\n");

    cb.len = p - buf;
    cb.buffer = buf;

    if (p >= end) {
        fprintf(stderr, "buffer overrun trace json %zu %zu\n", cb.len, buflen);
    }

    return cb;
}


//
// Return a description of the receiver in json.
//
struct char_buffer generateReceiverJson() {
    struct char_buffer cb;
    size_t buflen = 8192;
    char *buf = (char *) malloc(buflen), *p = buf, *end = buf + buflen;

    p = safe_snprintf(p, end, "{ "
            "\"refresh\": %.0f, "
            "\"history\": %d",
            1.0 * Modes.json_interval, Modes.json_aircraft_history_next + 1);


    if (Modes.json_location_accuracy && Modes.userLocationValid) {
        if (Modes.json_location_accuracy == 1) {
            p = safe_snprintf(p, end, ", "
                    "\"lat\": %.2f, "
                    "\"lon\": %.2f",
                    Modes.fUserLat, Modes.fUserLon); // round to 2dp - about 0.5-1km accuracy - for privacy reasons
        } else {
            p = safe_snprintf(p, end, ", "
                    "\"lat\": %.6f, "
                    "\"lon\": %.6f",
                    Modes.fUserLat, Modes.fUserLon); // exact location
        }
    }

    p = safe_snprintf(p, end, ", \"jaeroTimeout\": %.1f", ((double) Modes.trackExpireJaero) / (60 * SECONDS));

    if (Modes.json_globe_index) {
        if (Modes.db || Modes.db2)
            p = safe_snprintf(p, end, ", \"dbServer\": true");

        p = safe_snprintf(p, end, ", \"binCraft\": true");
        p = safe_snprintf(p, end, ", \"globeIndexGrid\": %d", GLOBE_INDEX_GRID);

        p = safe_snprintf(p, end, ", \"globeIndexSpecialTiles\": [ ");
        struct tile *tiles = Modes.json_globe_special_tiles;

        for (int i = 0; tiles[i].south != 0 || tiles[i].north != 0; i++) {
            struct tile tile = tiles[i];
            p = safe_snprintf(p, end, "{ \"south\": %d, ", tile.south);
            p = safe_snprintf(p, end, "\"east\": %d, ", tile.east);
            p = safe_snprintf(p, end, "\"north\": %d, ", tile.north);
            p = safe_snprintf(p, end, "\"west\": %d }, ", tile.west);
        }
        p -= 2; // get rid of comma and space at the end
        p = safe_snprintf(p, end, " ]");
    }

    p = safe_snprintf(p, end, ", \"version\": \"%s\" }\n", MODES_READSB_VERSION);

    if (p >= end)
        fprintf(stderr, "buffer overrun receiver json\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

// Write JSON to file
static inline void writeJsonTo (const char* dir, const char *file, struct char_buffer cb, int gzip) {

    char pathbuf[PATH_MAX];
    char tmppath[PATH_MAX];
    int fd;
    int len = cb.len;
    char *content = cb.buffer;

    if (!dir)
        snprintf(tmppath, PATH_MAX, "%s.%lx", file, random());
    else
        snprintf(tmppath, PATH_MAX, "%s/%s.%lx", dir, file, random());

    tmppath[PATH_MAX - 1] = 0;
    fd = open(tmppath, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        fprintf(stderr, "writeJsonTo open(): ");
        perror(tmppath);
        if (!gzip)
            free(content);
        return;
    }

    if (!dir)
        snprintf(pathbuf, PATH_MAX, "%s", file);
    else
        snprintf(pathbuf, PATH_MAX, "%s/%s", dir, file);

    pathbuf[PATH_MAX - 1] = 0;

    if (gzip < 0) {
        /*
        int brotliLvl = -gzip;
        size_t outSize = len + 4096;
        char *outBuf = malloc(outSize);
        // BROTLI_MODE_TEXT  Compression mode for UTF-8 formatted text input. 
        // BROTLI_MODE_GENERIC
        int rc = BrotliEncoderCompress(
                brotliLvl, 22, BROTLI_DEFAULT_MODE,
                len, (uint8_t *) content, &outSize, (uint8_t *) outBuf);

        if (rc == BROTLI_FALSE) {
            goto error_1;
        }

        if (write(fd, outBuf, outSize) != (ssize_t) outSize)
            goto error_1;

        if (close(fd) < 0)
            goto error_2;
        */
    } else if (gzip > 0) {
        gzFile gzfp = gzdopen(fd, "wb");
        if (!gzfp)
            goto error_1;

        gzbuffer(gzfp, 1024 * 1024);
        gzsetparams(gzfp, gzip, Z_DEFAULT_STRATEGY);

        int res = gzwrite(gzfp, content, len);
        if (res != len) {
            int error;
            fprintf(stderr, "%s: gzwrite of length %d failed: %s (res == %d)\n", pathbuf, len, gzerror(gzfp, &error), res);
        }

        if (gzclose(gzfp) < 0)
            goto error_2;
    } else {
        if (write(fd, content, len) != len) {
            fprintf(stderr, "writeJsonTo write(): ");
            perror(tmppath);
            goto error_1;
        }

        if (close(fd) < 0)
            goto error_2;
    }

    if (rename(tmppath, pathbuf) == -1) {
        fprintf(stderr, "writeJsonTo rename(): %s -> %s", tmppath, pathbuf);
        perror("");
        goto error_2;
    }
    if (!gzip)
        free(content);
    return;

error_1:
    close(fd);
error_2:
    unlink(tmppath);
    if (!gzip)
        free(content);
    return;
}

void writeJsonToFile (const char* dir, const char *file, struct char_buffer cb) {
    writeJsonTo(dir, file, cb, 0);
}

void writeJsonToGzip (const char* dir, const char *file, struct char_buffer cb, int gzip) {
    writeJsonTo(dir, file, cb, gzip);
}

struct char_buffer generateVRS(int part, int n_parts, int reduced_data) {
    struct char_buffer cb;
    uint64_t now = mstime();
    struct aircraft *a;
    size_t buflen = 256*1024; // The initial buffer is resized as needed
    char *buf = (char *) malloc(buflen), *p = buf, *end = buf + buflen;
    char *line_start;
    int first = 1;
    int part_len = AIRCRAFT_BUCKETS / n_parts;
    int part_start = part * part_len;

    //fprintf(stderr, "%02d/%02d reduced_data: %d\n", part, n_parts, reduced_data);

    p = safe_snprintf(p, end,
            "{\"acList\":[");

    for (int j = part_start; j < part_start + part_len; j++) {
        for (a = Modes.aircraft[j]; a; a = a->next) {
            if (a->messages < 2) { // basic filter for bad decodes
                continue;
            }
            if (now > a->seen + 10 * SECONDS) // don't include stale aircraft in the JSON
                continue;

            // For now, suppress non-ICAO addresses
            if (a->addr & MODES_NON_ICAO_ADDRESS)
                continue;

            if (first)
                first = 0;
            else
                *p++ = ',';

retry:
            line_start = p;

            p = safe_snprintf(p, end, "{\"Icao\":\"%s%06X\"", (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);


            if (trackDataValid(&a->position_valid)) {
                p = safe_snprintf(p, end, ",\"Lat\":%f,\"Long\":%f", a->lat, a->lon);
                //p = safe_snprintf(p, end, ",\"PosTime\":%"PRIu64, a->position_valid.updated);
            }

            if (altReliable(a))
                p = safe_snprintf(p, end, ",\"Alt\":%d", a->altitude_baro);

            if (trackDataValid(&a->geom_rate_valid)) {
                p = safe_snprintf(p, end, ",\"Vsi\":%d", a->geom_rate);
            } else if (trackDataValid(&a->baro_rate_valid)) {
                p = safe_snprintf(p, end, ",\"Vsi\":%d", a->baro_rate);
            }

            if (trackDataValid(&a->track_valid)) {
                p = safe_snprintf(p, end, ",\"Trak\":%.1f", a->track);
            } else if (trackDataValid(&a->mag_heading_valid)) {
                p = safe_snprintf(p, end, ",\"Trak\":%.1f", a->mag_heading);
            } else if (trackDataValid(&a->true_heading_valid)) {
                p = safe_snprintf(p, end, ",\"Trak\":%.1f", a->true_heading);
            }

            if (trackDataValid(&a->gs_valid)) {
                p = safe_snprintf(p, end, ",\"Spd\":%.1f", a->gs);
            } else if (trackDataValid(&a->ias_valid)) {
                p = safe_snprintf(p, end, ",\"Spd\":%u", a->ias);
            } else if (trackDataValid(&a->tas_valid)) {
                p = safe_snprintf(p, end, ",\"Spd\":%u", a->tas);
            }

            if (trackDataValid(&a->altitude_geom_valid))
                p = safe_snprintf(p, end, ",\"GAlt\":%d", a->altitude_geom);

            if (trackDataValid(&a->airground_valid) && a->airground == AG_GROUND)
                p = safe_snprintf(p, end, ",\"Gnd\":true");
            else
                p = safe_snprintf(p, end, ",\"Gnd\":false");

            if (trackDataValid(&a->squawk_valid))
                p = safe_snprintf(p, end, ",\"Sqk\":\"%04x\"", a->squawk);

            if (trackDataValid(&a->nav_altitude_mcp_valid)) {
                p = safe_snprintf(p, end, ",\"TAlt\":%d", a->nav_altitude_mcp);
            } else if (trackDataValid(&a->nav_altitude_fms_valid)) {
                p = safe_snprintf(p, end, ",\"TAlt\":%d", a->nav_altitude_fms);
            }

            if (a->position_valid.source != SOURCE_INVALID) {
                if (a->position_valid.source == SOURCE_MLAT)
                    p = safe_snprintf(p, end, ",\"Mlat\":true");
                else if (a->position_valid.source == SOURCE_TISB)
                    p = safe_snprintf(p, end, ",\"Tisb\":true");
                else if (a->position_valid.source == SOURCE_JAERO)
                    p = safe_snprintf(p, end, ",\"Sat\":true");
            }

            if (reduced_data && a->addrtype != ADDR_JAERO && a->position_valid.source != SOURCE_JAERO)
                goto skip_fields;

            if (trackDataAge(now, &a->callsign_valid) < 5 * MINUTES
                    || (a->position_valid.source == SOURCE_JAERO && trackDataAge(now, &a->callsign_valid) < 8 * HOURS)
               ) {
                char buf[128];
                char buf2[16];
                const char *trimmed = trimSpace(a->callsign, buf2, 8);
                if (trimmed[0] != 0) {
                    p = safe_snprintf(p, end, ",\"Call\":\"%s\"", jsonEscapeString(trimmed, buf, sizeof(buf)));
                    p = safe_snprintf(p, end, ",\"CallSus\":false");
                }
            }

            if (trackDataValid(&a->nav_heading_valid))
                p = safe_snprintf(p, end, ",\"TTrk\":%.1f", a->nav_heading);


            if (trackDataValid(&a->geom_rate_valid)) {
                p = safe_snprintf(p, end, ",\"VsiT\":1");
            } else if (trackDataValid(&a->baro_rate_valid)) {
                p = safe_snprintf(p, end, ",\"VsiT\":0");
            }


            if (trackDataValid(&a->track_valid)) {
                p = safe_snprintf(p, end, ",\"TrkH\":false");
            } else if (trackDataValid(&a->mag_heading_valid)) {
                p = safe_snprintf(p, end, ",\"TrkH\":true");
            } else if (trackDataValid(&a->true_heading_valid)) {
                p = safe_snprintf(p, end, ",\"TrkH\":true");
            }

            p = safe_snprintf(p, end, ",\"Sig\":%d", get8bitSignal(a));

            if (trackDataValid(&a->nav_qnh_valid))
                p = safe_snprintf(p, end, ",\"InHg\":%.2f", a->nav_qnh * 0.02952998307);

            p = safe_snprintf(p, end, ",\"AltT\":%d", 0);


            if (a->position_valid.source != SOURCE_INVALID) {
                if (a->position_valid.source != SOURCE_MLAT)
                    p = safe_snprintf(p, end, ",\"Mlat\":false");
                if (a->position_valid.source != SOURCE_TISB)
                    p = safe_snprintf(p, end, ",\"Tisb\":false");
                if (a->position_valid.source != SOURCE_JAERO)
                    p = safe_snprintf(p, end, ",\"Sat\":false");
            }


            if (trackDataValid(&a->gs_valid)) {
                p = safe_snprintf(p, end, ",\"SpdTyp\":0");
            } else if (trackDataValid(&a->ias_valid)) {
                p = safe_snprintf(p, end, ",\"SpdTyp\":2");
            } else if (trackDataValid(&a->tas_valid)) {
                p = safe_snprintf(p, end, ",\"SpdTyp\":3");
            }

            if (a->adsb_version >= 0)
                p = safe_snprintf(p, end, ",\"Trt\":%d", a->adsb_version + 3);
            else
                p = safe_snprintf(p, end, ",\"Trt\":%d", 1);


            //p = safe_snprintf(p, end, ",\"Cmsgs\":%ld", a->messages);


skip_fields:

            p = safe_snprintf(p, end, "}");

            if ((p + 10) >= end) { // +10 to leave some space for the final line
                // overran the buffer
                int used = line_start - buf;
                buflen *= 2;
                buf = (char *) realloc(buf, buflen);
                p = buf + used;
                end = buf + buflen;
                goto retry;
            }
        }
    }

    p = safe_snprintf(p, end, "]}\n");

    if (p >= end)
        fprintf(stderr, "buffer overrun vrs json\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}
