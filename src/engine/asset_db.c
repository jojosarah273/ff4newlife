/**
 * @file asset_db.c
 * @brief Phase 2: typed asset database loader (cJSON).
 *
 * Everything data-driven: values come from the `make rip` JSON, not from
 * hand-picked ROM constants. When the JSON disagrees with a header enum,
 * the JSON wins (it is the cartridge); enum drift is caught by test_assets.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "ff4/core/ff4_types.h"
#include "ff4/engine/asset_db.h"

/* ------------------------------------------------------------------ */
/*  Small cJSON helpers                                               */
/* ------------------------------------------------------------------ */

static cJSON *jobj(cJSON *parent, const char *key)
{
    return parent ? cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
}

static int jint(cJSON *item, int def)
{
    return (item && cJSON_IsNumber(item)) ? item->valueint : def;
}

static bool jbool(cJSON *item, bool def)
{
    if (!item)
        return def;
    if (cJSON_IsBool(item))
        return cJSON_IsTrue(item);
    if (cJSON_IsNumber(item))
        return item->valueint != 0;
    return def;
}

static const char *jstr(cJSON *item, const char *def)
{
    return (item && cJSON_IsString(item)) ? item->valuestring : def;
}

/* Bounds-checking array accessor used for *every* array read. */
static cJSON *jarr(cJSON *arr, int idx)
{
    if (!arr || !cJSON_IsArray(arr))
        return NULL;
    if (idx < 0 || idx >= cJSON_GetArraySize(arr))
        return NULL;
    return cJSON_GetArrayItem(arr, idx);
}

/* ------------------------------------------------------------------ */
/*  Base64 decoding                                                   */
/* ------------------------------------------------------------------ */

static int b64val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t b64_decode(const char *src, uint8_t *out, size_t out_cap)
{
    size_t o = 0;
    int acc = 0, bits = 0;

    /* Standard 6-bit bitstream decoder; '=' and whitespace are skipped,
       so padded final groups ('/w==') decode correctly. */
    for (; *src; ++src) {
        int v = b64val(*src);
        if (v < 0)
            continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o < out_cap)
                out[o++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    return o;
}

static uint8_t *b64_alloc(const char *src, size_t *len_out)
{
    size_t cap = (strlen(src) / 4 + 1) * 3;
    uint8_t *buf = (uint8_t *)malloc(cap + 1);

    if (!buf)
        return NULL;
    *len_out = b64_decode(src, buf, cap);
    return buf;
}

/* ------------------------------------------------------------------ */
/*  Blob registry                                                     */
/* ------------------------------------------------------------------ */

static int blob_add(FF4_AssetDB *db, const char *key, const uint8_t *data,
                    size_t len)
{
    int i;

    if (db->blob_count >= FF4_DB_BLOBS)
        return -1;
    for (i = 0; i < db->blob_count; ++i)
        if (strcmp(db->blobs[i].key, key) == 0)
            return i;

    i = db->blob_count++;
    snprintf(db->blobs[i].key, sizeof db->blobs[i].key, "%s", key);
    db->blobs[i].data = (uint8_t *)malloc(len ? len : 1);
    if (!db->blobs[i].data)
        return -1;
    memcpy(db->blobs[i].data, data, len);
    db->blobs[i].len = len;
    return i;
}

const FF4_Blob *ff4_db_blob(const FF4_AssetDB *db, const char *key)
{
    int i;

    for (i = 0; i < db->blob_count; ++i)
        if (strcmp(db->blobs[i].key, key) == 0)
            return &db->blobs[i];
    return NULL;
}

static bool blob_key_match(const char *key)
{
    static const char *tails[] = {
        "Graphics", "Palette", "Palettes", "Tilemap", "Layout", "Tilesets",
        "SpriteData", "Script", "Sine", "HDMA", "Gfx", "Sprites", "Action"
    };
    size_t i;

    if (strcmp(key, "rngTable") == 0 || strcmp(key, "prophecyText") == 0 ||
        strcmp(key, "spcData") == 0 || strcmp(key, "songSamples") == 0 ||
        strcmp(key, "songScripts") == 0 || strcmp(key, "windowPalette") == 0 ||
        strcmp(key, "windowGraphics") == 0 ||
        strcmp(key, "vehicleGraphics") == 0 ||
        strcmp(key, "summonGraphicsMap") == 0 ||
        strcmp(key, "summonFrame") == 0 ||
        strcmp(key, "eventScript") == 0 ||
        strcmp(key, "brrSamples") == 0 ||
        strcmp(key, "brrSampleFrequencyMultiplier") == 0 ||
        strcmp(key, "brrSampleLoopStart") == 0 ||
        strcmp(key, "monsterShadowGfx") == 0)
        return true;
    for (i = 0; i < sizeof tails / sizeof tails[0]; ++i)
        if (strstr(key, tails[i]) != NULL)
            return true;
    return false;
}

/* Binary blobs that later subsystems actually consume, in load priority
   (audio samples, world geometry, summons, monster graphics). */
static const char *blob_array_priority[] = {
    "songSamples", "songScripts",
    "worldGraphics", "worldLayout1", "worldLayout2", "worldLayout3",
    "worldTilesets", "worldPalettes", "worldPaletteAssignments",
    "worldSpriteGraphics",
    "summonFrame", "summonGraphicsMap",
    "monsterGraphics1", "monsterGraphics2", "monsterGraphics3",
    "monsterGraphics4", "monsterGraphics5",
    "portraitGraphics",
    NULL
};

static void blobs_add_array(FF4_AssetDB *db, cJSON *arr, const char *key)
{
    cJSON *item;
    int i = 0;
    uint8_t *data;
    size_t len;
    char keybuf[48];

    if (cJSON_GetArraySize(arr) > 2048)
        return;      /* 500+ entry script lists are not useful as blobs. */
    cJSON_ArrayForEach(item, arr) {
        if (!cJSON_IsString(item))
            continue;
        data = b64_alloc(item->valuestring, &len);
        if (data) {
            snprintf(keybuf, sizeof keybuf, "%s[%d]", key, i);
            blob_add(db, keybuf, data, len);
            free(data);
        }
        ++i;
    }
}

static void load_blobs(FF4_AssetDB *db, cJSON *obj)
{
    cJSON *item;
    size_t len;
    uint8_t *data;
    int p;

    /* Pass 1: scalar base64 blobs (graphics, palettes, spc data...),
       and the priority arrays, so they can never be crowded out. */
    cJSON_ArrayForEach(item, obj) {
        const char *key = item->string;

        if (!key || !blob_key_match(key))
            continue;
        if (!cJSON_IsString(item))
            continue;

        if (strcmp(key, "rngTable") == 0) {
            data = b64_alloc(item->valuestring, &len);
            if (data) {
                if (len >= 256) {
                    memcpy(db->rng_table, data, 256);
                    db->rng_table_valid = true;
                }
                blob_add(db, key, data, len);
                free(data);
            }
            continue;
        }

        data = b64_alloc(item->valuestring, &len);
        if (data) {
            blob_add(db, key, data, len);
            free(data);
        }
    }
    for (p = 0; blob_array_priority[p]; ++p) {
        item = jobj(obj, blob_array_priority[p]);
        if (item && cJSON_IsArray(item))
            blobs_add_array(db, item, blob_array_priority[p]);
    }

    /* Pass 2: any remaining bounded arrays (visual assets only). */
    cJSON_ArrayForEach(item, obj) {
        const char *key = item->string;

        if (!key || !blob_key_match(key) || !cJSON_IsArray(item))
            continue;
        blobs_add_array(db, item, key);
    }
}

/* ------------------------------------------------------------------ */
/*  Character table (rip companion file)                               */
/* ------------------------------------------------------------------ */

static void copy_glyph(char *dst, size_t n, const char *src)
{
    size_t i;

    for (i = 0; i + 1 < n && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

static void parse_char_section(FF4_CharTable *ct, cJSON *section, bool dte)
{
    cJSON *chars, *entry;

    chars = jobj(section, "char");
    if (!chars)
        return;
    cJSON_ArrayForEach(entry, chars) {
        const char *hex = entry->string;
        unsigned code;
        const char *glyph = jstr(entry, "?");

        if (!hex || sscanf(hex, "%x", &code) != 1 || code > 255)
            continue;
        if (dte) {
            ct->dte_valid[code] = true;
            copy_glyph(ct->dte_glyph[code], sizeof ct->dte_glyph[code],
                       glyph);
        } else {
            ct->text_valid[code] = true;
            copy_glyph(ct->text_glyph[code], sizeof ct->text_glyph[code],
                       glyph);
        }
    }
}

int ff4_db_load_rip(FF4_AssetDB *db, const char *rip_json_path)
{
    FILE *fp;
    long sz;
    char *buf;
    cJSON *root, *ct;

    fp = fopen(rip_json_path, "rb");
    if (!fp) {
        snprintf(db->error, sizeof db->error, "cannot open %s", rip_json_path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    buf[sz] = '\0';

    root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return -1;

    ct = jobj(root, "charTable");
    if (ct) {
        parse_char_section(&db->char_table, jobj(ct, "text"), false);
        parse_char_section(&db->char_table, jobj(ct, "dte"), true);
        parse_char_section(&db->char_table, jobj(ct, "menuText"), false);
    }
    cJSON_Delete(root);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Tables                                                             */
/* ------------------------------------------------------------------ */

static void load_items(FF4_AssetDB *db, cJSON *obj)
{
    cJSON *names = jobj(obj, "itemNames");
    cJSON *props = jobj(obj, "itemProperties");
    cJSON *equipab = jobj(obj, "itemEquipability");
    cJSON *consum = jobj(obj, "itemConsumable");
    cJSON *desc = jobj(obj, "itemDescriptionIndex");
    int i;

    for (i = 0; i < FF4_DB_ITEMS; ++i) {
        FF4_ItemDef *it = &db->items[i];
        cJSON *e = jarr(names, i);
        cJSON *p = jarr(props, i);

        memset(it, 0, sizeof *it);
        it->id = (uint8_t)i;
        snprintf(it->name, sizeof it->name, "%s", jstr(jobj(e, "itemName"), ""));
        it->symbol = (uint8_t)jint(jobj(e, "itemSymbol"), 0);
        it->price10 = (uint8_t)jint(jobj(p, "price10"), 0);
        it->price1000 = (uint8_t)jint(jobj(p, "price1000"), 0);
        it->price_multiplier = (uint8_t)jint(jobj(p, "multiplier"), 0);
        if (it->symbol < 32) {
            cJSON *eq = jarr(equipab, it->symbol);
            it->equip_mask = (uint16_t)jint(eq, 0);
        }

        if (i >= 0xB0 && i <= 0xDF) {
            cJSON *c = jarr(consum, i - 0xB0);
            if (c) {
                it->is_consumable = true;
                it->consumable.casting_time = (uint8_t)jint(jobj(c, "castingTime"), 0);
                it->consumable.target = (uint8_t)jint(jobj(c, "target"), 0);
                it->consumable.power = (uint8_t)jint(jobj(c, "power"), 0);
                it->consumable.hit = (uint8_t)jint(jobj(c, "hit"), 0);
                it->consumable.effect = (uint8_t)jint(jobj(c, "effect"), 0);
                it->consumable.element = (uint8_t)jint(jobj(c, "element"), 0);
                it->consumable.mp_cost = (uint8_t)jint(jobj(c, "mpCost"), 0);
            }
            it->description_index = (uint8_t)jint(jarr(desc, i - 0xB0), 0);
        } else {
            it->description_index = (uint8_t)jint(jarr(desc, i), 0xFF);
        }
    }
}

static void load_equip(FF4_AssetDB *db, cJSON *obj)
{
    cJSON *ep = jobj(obj, "weaponArmorProperties");
    cJSON *es = jobj(obj, "attackElementStatus");
    int i;

    for (i = 0; i <= FF4_DB_EQUIP; ++i) {   /* indexed by item id ($00-$AF) */
        FF4_EquipDef *eq = &db->equip[i];
        cJSON *e = jarr(ep, i);

        memset(eq, 0, sizeof *eq);
        eq->id = (uint8_t)i;
        if (!e)
            continue;
        eq->metallic = jbool(jobj(e, "metallic"), false);
        eq->shield = jbool(jobj(e, "shield"), false);
        eq->flags = (uint8_t)jint(jobj(e, "flags"), 0);
        eq->magic_defense_pct = (uint8_t)jint(jobj(e, "magicDefensePct"), 0);
        eq->attack = (uint8_t)jint(jobj(e, "attack"), 0);
        eq->attack_pct = (uint8_t)jint(jobj(e, "attackPct"), 0);
        eq->spell_cast = (uint8_t)jint(jobj(e, "spellCast"), 0);
        eq->magic_defense = (uint8_t)jint(jobj(e, "magicDefense"), 0);
        eq->element_status = (uint8_t)jint(jobj(e, "elementStatus"), 0);
        eq->creature_type = (uint8_t)jint(jobj(e, "creatureType"), 0);
        eq->equipability = (uint8_t)jint(jobj(e, "equipability"), 0);
        eq->weapon_type = (uint8_t)jint(jobj(e, "weaponType"), 0);
        eq->stats = (uint8_t)jint(jobj(e, "stats"), 0);
        eq->stat_mod = (uint8_t)jint(jobj(e, "statMod"), 0);

        if (eq->element_status < 128) {
            cJSON *s = jarr(es, eq->element_status);
            int st = jint(jobj(s, "status"), 0);
            eq->element = (uint8_t)jint(jobj(s, "element"), 0);
            eq->status1 = (uint8_t)(st & 0xFF);       /* byte1 of the pair  */
            eq->status2 = (uint8_t)((st >> 8) & 0xFF);/* byte2 of the pair  */
        }
    }
}

static void load_spells(FF4_AssetDB *db, cJSON *obj)
{
    cJSON *names = jobj(obj, "spellNames");
    cJSON *initial = jobj(obj, "spellListInitial");
    cJSON *learned = jobj(obj, "spellListLearned");
    int i, j;

    for (i = 0; i < FF4_DB_SPELLS; ++i) {
        cJSON *e = jarr(names, i);
        db->spells[i].id = (uint8_t)i;
        db->spells[i].symbol = (uint8_t)jint(jobj(e, "spellSymbol"), 0);
        snprintf(db->spells[i].name, sizeof db->spells[i].name, "%s",
                 jstr(jobj(e, "spellName"), ""));
    }
    for (i = 0; i < FF4_DB_LEVEL_CHARS; ++i) {
        cJSON *init = jarr(initial, i);
        if (cJSON_IsArray(init)) {
            int n = cJSON_GetArraySize(init);
            if (n > FF4_SPELLS_PER_LIST) n = FF4_SPELLS_PER_LIST;
            for (j = 0; j < n; ++j) {
                cJSON *s = cJSON_GetArrayItem(init, j);
                if (cJSON_IsNumber(s))
                    db->spell_initial[i][db->spell_initial_count[i]++] =
                        (uint8_t)s->valueint;
            }
        }
        cJSON *lrn = jarr(learned, i);
        if (cJSON_IsArray(lrn)) {
            int n = cJSON_GetArraySize(lrn);
            if (n > 24) n = 24;
            for (j = 0; j < n; ++j) {
                cJSON *s = cJSON_GetArrayItem(lrn, j);
                uint8_t sp = (uint8_t)jint(jobj(s, "spell"), 0);
                uint8_t lv = (uint8_t)jint(jobj(s, "level"), 0);
                if (!sp) continue;
                db->spell_learned[i][db->spell_learned_count[i]].spell = sp;
                db->spell_learned[i][db->spell_learned_count[i]].level = lv;
                db->spell_learned_count[i]++;
            }
        }
    }
}

static void load_attacks(FF4_AssetDB *db, cJSON *obj)
{
    cJSON *props = jobj(obj, "attackProperties");
    cJSON *names = jobj(obj, "attackNames");
    int i;

    for (i = 0; i < FF4_DB_ATTACKS; ++i) {
        FF4_AttackDef *a = &db->attacks[i];
        cJSON *e = jarr(props, i);

        memset(a, 0, sizeof *a);
        if (!e)
            continue;
        a->casting_time = (uint8_t)jint(jobj(e, "castingTime"), 0);
        a->target = (uint8_t)jint(jobj(e, "target"), 0);
        a->power = (uint8_t)jint(jobj(e, "power"), 0);
        a->hit = (uint8_t)jint(jobj(e, "hit"), 0);
        a->effect = (uint8_t)jint(jobj(e, "effect"), 0);
        a->element = (uint8_t)jint(jobj(e, "element"), 0);
        a->mp_cost = (uint8_t)jint(jobj(e, "mpCost"), 0);
        a->boss = jbool(jobj(e, "boss"), false);
        a->damage = jbool(jobj(e, "damage"), false);
        a->split_damage = jbool(jobj(e, "splitDamage"), false);
        a->no_reflect = jbool(jobj(e, "noReflect"), false);
    }
    for (i = 0; i < FF4_DB_ATTACK_NAMES; ++i) {
        cJSON *e = jarr(names, i);
        snprintf(db->attack_names[i], sizeof db->attack_names[i], "%s",
                 jstr(e, ""));
    }
}

static void load_monsters(FF4_AssetDB *db, cJSON *obj)
{
    cJSON *names = jobj(obj, "monsterName");
    cJSON *props = jobj(obj, "monsterProperties");
    cJSON *agil = jobj(obj, "monsterAgility");
    cJSON *ad = jobj(obj, "monsterAttackDefense");
    cJSON *items = jobj(obj, "monsterItems");
    cJSON *exp = jobj(obj, "monsterExperience");
    cJSON *gp = jobj(obj, "monsterGP");
    int i;

    for (i = 0; i < FF4_DB_MONSTERS; ++i) {
        db->monster_exp[i] = (uint16_t)jint(jarr(exp, i), 0);
        db->monster_gp[i] = (uint16_t)jint(jarr(gp, i), 0);
    }

    for (i = 0; i < FF4_DB_MONSTERS; ++i) {
        FF4_MonsterDef *m = &db->monsters[i];
        cJSON *e = jarr(props, i);

        memset(m, 0, sizeof *m);
        m->id = (uint8_t)i;
        snprintf(m->name, sizeof m->name, "%s", jstr(jarr(names, i), ""));
        if (!e)
            continue;
        m->level = (uint8_t)jint(jobj(e, "level"), 0);
        m->boss = jbool(jobj(e, "boss"), false);
        m->hp = (uint16_t)jint(jobj(e, "hp"), 0);
        m->attack = (uint8_t)jint(jobj(e, "attack"), 0);
        m->magic_attack = (uint8_t)jint(jobj(e, "magicAttack"), 0);
        m->defense = (uint8_t)jint(jobj(e, "defense"), 0);
        m->magic_defense = (uint8_t)jint(jobj(e, "magicDefense"), 0);
        m->agility = (uint8_t)jint(jobj(e, "agility"), 0);
        m->item_rate = (uint8_t)jint(jobj(e, "itemRate"), 0);
        m->script = (uint8_t)jint(jobj(e, "script"), 0);
        m->counter = (uint8_t)jint(jobj(e, "counter"), 0);
        m->attack_elements = (uint8_t)jint(jobj(e, "attackElements"), 0);
        m->strong_elements = (uint8_t)jint(jobj(e, "strongElements"), 0);
        m->weak_elements = (uint8_t)jint(jobj(e, "weakElements"), 0);
        m->attack_status = (uint8_t)jint(jobj(e, "attackStatus"), 0);
        m->status_immunity = (uint16_t)jint(jobj(e, "statusImmunity"), 0);
        m->creature_type = (uint8_t)jint(jobj(e, "creatureType"), 0);

        if (m->agility < 64) {
            cJSON *a = jarr(agil, m->agility);
            m->agility_min = (uint8_t)jint(jobj(a, "min"), 0);
            m->agility_max = (uint8_t)jint(jobj(a, "max"), 0);
        }
        {
            cJSON *d = jarr(ad, i);
            m->atk_def_base = (uint8_t)jint(jobj(d, "base"), 0);
            m->atk_def_percent = (uint8_t)jint(jobj(d, "percent"), 0);
            m->atk_def_multi = (uint8_t)jint(jobj(d, "multiplier"), 0);
        }
        {
            int item_set = jint(jobj(e, "items"), 0xFF);
            if (item_set >= 0 && item_set < 64) {
                cJSON *d = jarr(items, item_set);
                m->drop[0] = (uint8_t)jint(jobj(d, "drop1"), 0xFF);
                m->drop[1] = (uint8_t)jint(jobj(d, "drop2"), 0xFF);
                m->drop[2] = (uint8_t)jint(jobj(d, "drop3"), 0xFF);
                m->drop[3] = (uint8_t)jint(jobj(d, "drop4"), 0xFF);
            } else {
                m->drop[0] = m->drop[1] = m->drop[2] = m->drop[3] = 0xFF;
            }
        }
    }
}

static void load_formations(FF4_AssetDB *db, cJSON *obj)
{
    cJSON *props = jobj(obj, "battleProperties");
    cJSON *pos = jobj(obj, "monsterPosition");
    cJSON *grp = jobj(obj, "battleGroup");
    cJSON *grpw = jobj(obj, "battleGroupWorld");
    int i, j;

    for (i = 0; i < FF4_DB_FORMATIONS; ++i) {
        FF4_FormationDef *f = &db->formations[i];
        cJSON *e = jarr(props, i);

        memset(f, 0, sizeof *f);
        for (j = 0; j < FF4_DB_TEAMS; ++j)
            f->mons[j] = 0xFF;
        if (!e)
            continue;
        f->mons[0] = (uint8_t)jint(jobj(e, "monster1"), 0xFF);
        f->mons[1] = (uint8_t)jint(jobj(e, "monster2"), 0xFF);
        f->mons[2] = (uint8_t)jint(jobj(e, "monster3"), 0xFF);
        f->counts[0] = (uint8_t)jint(jobj(e, "monster1Count"), 0);
        f->counts[1] = (uint8_t)jint(jobj(e, "monster2Count"), 0);
        f->counts[2] = (uint8_t)jint(jobj(e, "monster3Count"), 0);
        f->eggs[0] = jbool(jobj(e, "monster1Egg"), false);
        f->eggs[1] = jbool(jobj(e, "monster2Egg"), false);
        f->eggs[2] = jbool(jobj(e, "monster3Egg"), false);
        f->position = (uint8_t)jint(jobj(e, "monsterPosition"), 0);
        f->cursor_properties = (uint8_t)jint(jobj(e, "cursorProperties"), 0);
        f->vram_map = (uint8_t)jint(jobj(e, "vramMap"), 0);
        f->hidden = (uint8_t)jint(jobj(e, "hiddenMonsters"), 0);
        f->song = (uint8_t)jint(jobj(e, "song"), 0);
        f->flags1 = (uint8_t)jint(jobj(e, "flags1"), 0);
        f->flags2 = (uint8_t)jint(jobj(e, "flags2"), 0);
    }

    for (i = 0; i < 256; ++i) {
        cJSON *entry = jarr(pos, i);
        int slots = cJSON_IsArray(entry) ? cJSON_GetArraySize(entry) : 0;
        for (j = 0; j < slots && j < FF4_DB_SLOTS_IN_FORMATION; ++j) {
            cJSON *p = cJSON_GetArrayItem(entry, j);
            db->monster_positions[i][j][0] = (uint8_t)jint(jobj(p, "x"), 0);
            db->monster_positions[i][j][1] = (uint8_t)jint(jobj(p, "y"), 0);
        }
    }

    for (i = 0; i < FF4_DB_BATTLE_GROUPS; ++i) {
        cJSON *g = jarr(grp, i);   /* object: {battle1..battle8} */
        for (j = 0; j < 8; ++j) {
            char key[8];
            snprintf(key, sizeof key, "battle%d", j + 1);
            db->formation_groups[i][j] =
                (uint8_t)jint(jobj(g, key), 0);
        }
    }
    for (i = 0; i < FF4_DB_BATTLE_GROUPS_WORLD; ++i) {
        cJSON *g = jarr(grpw, i);  /* object: {battle1..battle8} */
        for (j = 0; j < 8; ++j) {
            char key[8];
            snprintf(key, sizeof key, "battle%d", j + 1);
            db->formation_groups_world[i][j] =
                (uint8_t)jint(jobj(g, key), 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Character start data (CharProp)                                    */
/*                                                                     */
/*  The 14 CharProp rows are ordered by character ID with re-used      */
/*  instances skipped: {01,02,03,04,05,06,07,08,09,0B,0E,12,13,15}.    */
/*  Names come from the 6-byte name table (14 unique names, Anna       */
/*  included) via the per-id name index below.                         */
/* ------------------------------------------------------------------ */

static const uint8_t char_id_map[FF4_DB_CHARS] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0B, 0x0E, 0x12, 0x13, 0x15
};

/* name-table index for each character id present in chars[]. */
static const uint8_t char_name_idx[FF4_DB_CHARS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8,      /* ids $01-$09 */
    0,                             /* $0B Cecil (paladin, same name) */
    9,                             /* $0E Cid */
    10,                            /* $12 Edge */
    11,                            /* $13 FuSoYa */
    12                             /* $15 Golbez */
};

static void load_characters(FF4_AssetDB *db, cJSON *obj)
{
    cJSON *props = jobj(obj, "CharProp");
    cJSON *equip = jobj(obj, "CharInitEquip");
    cJSON *names = jobj(obj, "characterNames");
    cJSON *classes = jobj(obj, "className");
    int i;

    for (i = 0; i < FF4_DB_CHARS; ++i) {
        FF4_CharDef *c = &db->chars[i];
        cJSON *e = jarr(props, i);
        int eq = jint(jobj(e, "initEquip"), 0) - 1; /* 1-based row id */
        cJSON *q = (eq >= 0 && eq < 21) ? jarr(equip, eq) : NULL;

        memset(c, 0, sizeof *c);
        c->id = char_id_map[i];
        c->instance = (uint8_t)i;
        snprintf(c->name, sizeof c->name, "%s",
                 jstr(jarr(names, char_name_idx[i]), ""));
        snprintf(c->class_name, sizeof c->class_name, "%s",
                 jstr(jarr(classes, jint(jobj(e, "class"), 0)), ""));
        if (!e)
            continue;
        c->init_equip = (uint8_t)eq;
        c->job = (uint8_t)jint(jobj(e, "class"), 0);
        c->right_handed = jbool(jobj(e, "rightHanded"), false);
        c->left_handed = jbool(jobj(e, "leftHanded"), false);
        c->level = (uint8_t)jint(jobj(e, "level"), 1);
        c->hp = (uint16_t)jint(jobj(e, "hp"), 1);
        c->mp = (uint16_t)jint(jobj(e, "mp"), 0);
        c->cur_hp = (uint16_t)jint(jobj(e, "currentHP"), c->hp);
        c->cur_mp = (uint16_t)jint(jobj(e, "currentMP"), c->mp);
        c->str = (uint8_t)jint(jobj(e, "strength"), 0);
        c->agi = (uint8_t)jint(jobj(e, "agility"), 0);
        c->sta = (uint8_t)jint(jobj(e, "stamina"), 0);
        c->intel = (uint8_t)jint(jobj(e, "intellect"), 0);
        c->spr = (uint8_t)jint(jobj(e, "spirit"), 0);
        c->crit_pct = (uint8_t)jint(jobj(e, "critPct"), 0);
        c->crit_bonus = (uint8_t)jint(jobj(e, "critBonus"), 0);
        c->speed_mod = (uint8_t)jint(jobj(e, "speedMod"), 16);
        c->exp = (uint32_t)jint(jobj(e, "experience"), 0);
        c->exp_last_level = (uint32_t)jint(jobj(e, "expLastLevel"), 0);
        c->start_status = (uint8_t)jint(jobj(e, "status"), 0);

        if (q) {
            c->eq_right = (uint8_t)jint(jobj(q, "rightHand"), 0);
            c->eq_right_qty = (uint8_t)jint(jobj(q, "rightHandQty"), 0);
            c->eq_left = (uint8_t)jint(jobj(q, "leftHand"), 0);
            c->eq_left_qty = (uint8_t)jint(jobj(q, "leftHandQty"), 0);
            c->eq_head = (uint8_t)jint(jobj(q, "helmet"), 0);
            c->eq_body = (uint8_t)jint(jobj(q, "armor"), 0);
            c->eq_finger = (uint8_t)jint(jobj(q, "shield"), 0);
        }
    }
}

static void load_levelup(FF4_AssetDB *db, cJSON *obj)
{
    cJSON *low = jobj(obj, "LevelUpPropLow");
    cJSON *high = jobj(obj, "LevelUpPropHigh");
    int i, j;

    for (i = 0; i < FF4_DB_LEVEL_CHARS; ++i) {
        cJSON *row = jarr(low, i);
        int n = cJSON_IsArray(row) ? cJSON_GetArraySize(row) : 0;
        int k = 0;

        if (n > FF4_DB_LEVEL_MAX) n = FF4_DB_LEVEL_MAX;
        for (j = 0; j < n; ++j) {
            cJSON *e = cJSON_GetArrayItem(row, j);
            db->levelup[i][k].exp_low = (uint16_t)jint(jobj(e, "exp"), 0);
            db->levelup[i][k].exp_msb = (uint8_t)jint(jobj(e, "expMSB"), 0);
            db->levelup[i][k].hp = (uint8_t)jint(jobj(e, "hp"), 0);
            db->levelup[i][k].mp = (uint8_t)jint(jobj(e, "mp"), 0);
            db->levelup[i][k].stat_mod = (uint8_t)jint(jobj(e, "statMod"), 0);
            db->levelup[i][k].stats = (uint8_t)jint(jobj(e, "stats"), 0);
            ++k;
        }
        row = jarr(high, i);
        n = cJSON_IsArray(row) ? cJSON_GetArraySize(row) : 0;
        for (j = 0; j < n && k < FF4_DB_LEVEL_MAX; ++j) {
            cJSON *e = cJSON_GetArrayItem(row, j);
            /* High-table rows carry stat growth; EXP thresholds continue
               from the low table (Phase 3 resolves the tail formula). */
            db->levelup[i][k].exp_low = 0xFFFF;
            db->levelup[i][k].exp_msb = 0xFF;
            db->levelup[i][k].hp = 0;
            db->levelup[i][k].mp = 0;
            db->levelup[i][k].stat_mod = (uint8_t)jint(jobj(e, "statMod"), 0);
            db->levelup[i][k].stats = (uint8_t)jint(jobj(e, "stats"), 0);
            ++k;
        }
        db->levelup_rows[i] = (uint8_t)k;
    }
}

static void load_item_classes(FF4_AssetDB *db, cJSON *obj)
{
    cJSON *eq = jobj(obj, "itemEquipability");
    int i;

    for (i = 0; i < 32; ++i)
        db->item_classes[i] = (uint16_t)jint(jarr(eq, i), 0);
}

static void load_maps(FF4_AssetDB *db, cJSON *obj)
{
    cJSON *props = jobj(obj, "mapProperties");
    cJSON *titles = jobj(obj, "mapTitle");
    cJSON *tileprops = jobj(obj, "mapTileProperties");
    cJSON *wtp = jobj(obj, "worldTileProperties");
    cJSON *npcprops = jobj(obj, "npcProperties");
    int i, j;

    for (i = 0; i < FF4_DB_MAP_TITLES; ++i)
        snprintf(db->map_titles[i], sizeof db->map_titles[i], "%s",
                 jstr(jarr(titles, i), ""));

    for (i = 0; i < FF4_DB_MAPS; ++i) {
        FF4_MapDef *m = &db->maps[i];
        cJSON *e = jarr(props, i);
        int title = jint(jobj(e, "title"), -1);
        cJSON *npc_list = jarr(npcprops, i);
        int nn = cJSON_IsArray(npc_list) ? cJSON_GetArraySize(npc_list) : 0;
        int j2;

        memset(m, 0, sizeof *m);
        m->id = (uint8_t)i;
        if (!e)
            continue;
        snprintf(m->title, sizeof m->title, "%s",
                 (title >= 0 && title < FF4_DB_MAP_TITLES)
                     ? db->map_titles[title]
                     : "");
        m->hide_title = jbool(jobj(e, "hideTitle"), false);
        m->song = (uint8_t)jint(jobj(e, "song"), 0);
        m->spell_flags = (uint8_t)jint(jobj(e, "spellFlags"), 0);
        m->layout1 = (uint8_t)jint(jobj(e, "layout1"), 0);
        m->layout2 = (uint8_t)jint(jobj(e, "layout2"), 0);
        m->layout_msb = (uint8_t)jint(jobj(e, "layoutMSB"), 0);
        m->graphics = (uint8_t)jint(jobj(e, "graphics"), 0);
        m->fill = (uint8_t)jint(jobj(e, "fill"), 0);
        m->passable_fill = (uint8_t)jint(jobj(e, "passableFill"), 0);
        m->palette = (uint8_t)jint(jobj(e, "palette"), 0);
        m->addition = (uint8_t)jint(jobj(e, "addition"), 0);

        /* npc index for the map + the per-map npc list */
        if (nn > 14)
            nn = 14;
        db->npc_count[i] = (uint8_t)nn;
        for (j2 = 0; j2 < nn; ++j2) {
            cJSON *n = cJSON_GetArrayItem(npc_list, j2);
            FF4_NPCDef *np = &db->npcs[i][j2];

            np->x = (uint8_t)jint(jobj(n, "x"), 0);
            np->y = (uint8_t)jint(jobj(n, "y"), 0);
            np->speed = (uint8_t)jint(jobj(n, "speed"), 1);
            np->direction = (uint8_t)jint(jobj(n, "direction"), 0);
            np->reaction = (uint8_t)jint(jobj(n, "reaction"), 0);
            np->palette = (uint8_t)jint(jobj(n, "palette"), 0);
            np->npc_msb = jbool(jobj(n, "npcMSB"), false);
            np->random_movement = jbool(jobj(n, "randomMovement"), false);
            np->animated = jbool(jobj(n, "animated"), false);
            np->switch0 = (uint16_t)jint(jobj(n, "switch0"), 0);
            np->switch1 = (uint16_t)jint(jobj(n, "switch1"), 0);
        }
    }

    for (i = 0; i < FF4_DB_TILESETS; ++i) {
        cJSON *ts = jarr(tileprops, i);
        int n = cJSON_IsArray(ts) ? cJSON_GetArraySize(ts) : 0;
        for (j = 0; j < n && j < FF4_DB_TILES; ++j) {
            cJSON *t = cJSON_GetArrayItem(ts, j);
            db->map_tile_props[i][j][0] = (uint8_t)jint(jobj(t, "byte1"), 0);
            db->map_tile_props[i][j][1] = (uint8_t)jint(jobj(t, "byte2"), 0);
        }
    }
    for (i = 0; i < 3; ++i) {
        cJSON *ts = jarr(wtp, i);
        int n = cJSON_IsArray(ts) ? cJSON_GetArraySize(ts) : 0;
        for (j = 0; j < n && j < FF4_DB_TILES; ++j) {
            cJSON *t = cJSON_GetArrayItem(ts, j);
            db->world_tile_props[i][j][0] =
                (uint8_t)jint(jobj(t, "byte1"), 0);
            db->world_tile_props[i][j][1] =
                (uint8_t)jint(jobj(t, "byte2"), 0);
        }
    }
}

static void load_shop_and_text(FF4_AssetDB *db, cJSON *obj)
{
    cJSON *shops = jobj(obj, "shopProperties");
    cJSON *status = jobj(obj, "statusName");
    cJSON *dialog = jobj(obj, "battleDialog");
    cJSON *msg = jobj(obj, "battleMessage");
    cJSON *mapdlg = jobj(obj, "mapDialog");
    cJSON *evt1 = jobj(obj, "eventDialog1");
    cJSON *evt2 = jobj(obj, "eventDialog2");
    cJSON *cmdn = jobj(obj, "battleCommandNames");
    cJSON *ccmd = jobj(obj, "characterBattleCommands");
    int i, j;

    for (i = 0; i < FF4_DB_SHOPS; ++i) {
        cJSON *s = jarr(shops, i);
        for (j = 0; j < 8; ++j) {
            char key[8];
            snprintf(key, sizeof key, "item%d", j + 1);
            db->shops[i].items[j] =
                (uint8_t)jint(jobj(s, key), 0xFF);
        }
    }
    for (i = 0; i < 512; ++i) {
        db->map_battle[i] = (uint8_t)jint(jarr(jobj(obj, "mapBattle"), i), 0);
        db->map_battle_rates[i] =
            (uint8_t)jint(jarr(jobj(obj, "mapBattleProbability"), i), 0);
    }
    for (i = 0; i < 32; ++i)
        snprintf(db->status_names[i], sizeof db->status_names[i], "%s",
                 jstr(jarr(status, i), ""));
    for (i = 0; i < 186; ++i)
        snprintf(db->battle_dialog[i], sizeof db->battle_dialog[i], "%s",
                 jstr(jarr(dialog, i), ""));
    for (i = 0; i < 59; ++i)
        snprintf(db->battle_messages[i], sizeof db->battle_messages[i], "%s",
                 jstr(jarr(msg, i), ""));
    for (i = 0; i < 32; ++i)
        snprintf(db->battle_command_names[i], sizeof db->battle_command_names[i],
                 "%s", jstr(jarr(cmdn, i), ""));
    for (i = 0; i < 22; ++i)
        for (j = 0; j < 5; ++j) {
            cJSON *row = jarr(ccmd, i);
            cJSON *c = jarr(row, j);

            db->class_commands[i][j] = (uint8_t)jint(c, 0xFF);
        }

    for (i = 0; i < 384; ++i)
        snprintf(db->map_dialog[i], sizeof db->map_dialog[i], "%s",
                 jstr(jarr(mapdlg, i), ""));
    for (i = 0; i < 512; ++i) {
        snprintf(db->event_dialog1[i], sizeof db->event_dialog1[i], "%s",
                 jstr(jarr(evt1, i), ""));
        snprintf(db->event_dialog2[i], sizeof db->event_dialog2[i], "%s",
                 jstr(jarr(evt2, i), ""));
    }
}

/* ------------------------------------------------------------------ */
/*  Load entry point                                                   */
/* ------------------------------------------------------------------ */

int ff4_db_load(FF4_AssetDB *db, const char *path)
{
    FILE *fp;
    long sz;
    char *buf;
    cJSON *root, *obj;

    if (!db)
        return -1;
    memset(db, 0, sizeof *db);
    snprintf(db->source, sizeof db->source, "%s", path ? path : "");

    fp = fopen(path, "rb");
    if (!fp) {
        snprintf(db->error, sizeof db->error, "cannot open %s",
                 path ? path : "(null)");
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    buf[sz] = '\0';

    root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        snprintf(db->error, sizeof db->error, "cJSON parse failed in %s",
                 path ? path : "(null)");
        return -1;
    }

    obj = jobj(root, "obj");
    if (!obj) {
        snprintf(db->error, sizeof db->error, "no 'obj' root in %s",
                 path ? path : "(null)");
        cJSON_Delete(root);
        return -1;
    }

    load_items(db, obj);
    load_equip(db, obj);
    load_spells(db, obj);
    load_attacks(db, obj);
    load_monsters(db, obj);
    load_formations(db, obj);
    load_characters(db, obj);
    load_levelup(db, obj);
    load_item_classes(db, obj);
    load_maps(db, obj);
    load_shop_and_text(db, obj);
    load_blobs(db, obj);

    cJSON_Delete(root);
    db->loaded = true;
    return 0;
}

void ff4_db_free(FF4_AssetDB *db)
{
    int i;

    if (!db)
        return;
    for (i = 0; i < db->blob_count; ++i)
        free(db->blobs[i].data);
    db->blob_count = 0;
    db->loaded = false;
}

/* ----------------------- convenience accessors ---------------------- */

const char *ff4_item_name(const FF4_AssetDB *db, uint8_t id)
{
    /* every byte value is a valid slot id (0..255) */
    if (!db)
        return "";
    return db->items[id].name;
}

const FF4_MonsterDef *ff4_monster(const FF4_AssetDB *db, uint8_t id)
{
    if (!db || (int)id >= FF4_DB_MONSTERS)
        return NULL;
    return &db->monsters[id];
}

const FF4_CharDef *ff4_char(const FF4_AssetDB *db, uint8_t id)
{
    int i;

    if (!db)
        return NULL;
    for (i = 0; i < FF4_DB_CHARS; ++i)
        if (db->chars[i].id == id)
            return &db->chars[i];
    return NULL;
}

const FF4_LevelUpEntry *ff4_level_entry(const FF4_AssetDB *db,
                                        uint8_t char_idx, uint8_t level)
{
    if (!db || char_idx >= FF4_DB_LEVEL_CHARS)
        return NULL;
    if (level == 0 || level > db->levelup_rows[char_idx])
        return NULL;
    return &db->levelup[char_idx][level - 1];
}

uint32_t ff4_level_exp(const FF4_LevelUpEntry *e)
{
    if (!e)
        return 0;
    return (uint32_t)e->exp_low | ((uint32_t)e->exp_msb << 16);
}

uint8_t ff4_glyph(const FF4_AssetDB *db, uint8_t byte)
{
    if (db->char_table.text_valid[byte])
        return (uint8_t)db->char_table.text_glyph[byte][0];
    return '?';
}