/*
 * vr_config.cpp - key=value settings file for the VR layer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "vr_config.h"
#include "vr_log.h"

#define MAX_ENTRIES 64

static char sKeys[MAX_ENTRIES][48];
static char sVals[MAX_ENTRIES][64];
static int sCount = 0;

static char* trim(char* s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

extern "C" {

void VrConfigLoad(const char* path)
{
    sCount = 0;
    FILE* f = fopen(path, "r");
    if (!f) {
        ALOGI("VrConfig: %s not found, using defaults", path);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f) && sCount < MAX_ENTRIES) {
        char* hash = strchr(line, '#');
        if (hash) *hash = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* k = trim(line);
        char* v = trim(eq + 1);
        if (!*k) continue;
        snprintf(sKeys[sCount], sizeof(sKeys[0]), "%s", k);
        snprintf(sVals[sCount], sizeof(sVals[0]), "%s", v);
        ALOGI("VrConfig: %s = %s", sKeys[sCount], sVals[sCount]);
        sCount++;
    }
    fclose(f);
}

static const char* get(const char* key)
{
    for (int i = 0; i < sCount; i++) {
        if (strcmp(sKeys[i], key) == 0) return sVals[i];
    }
    return NULL;
}

float VrConfigGetFloat(const char* key, float def)
{
    const char* v = get(key);
    return v ? (float)atof(v) : def;
}

int VrConfigGetInt(const char* key, int def)
{
    const char* v = get(key);
    return v ? atoi(v) : def;
}

const char* VrConfigGetStr(const char* key, const char* def)
{
    const char* v = get(key);
    return v ? v : def;
}

} /* extern "C" */
