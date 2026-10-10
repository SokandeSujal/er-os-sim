/* Assignment 1: scenario parsing uses ordinary Linux text-file processing ideas.
 * Configuration lines set simulator defaults and CSV rows define patients.
 * Validation rejects malformed or impossible claims before any process is forked.
 * This keeps all later algorithms within fixed, checked array bounds. */
#define _DEFAULT_SOURCE
#include "common.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static int parse_int(const char *s, int *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    while (isspace((unsigned char)*end)) end++;
    if (errno || end == s || *end || v < 0 || v > 1000000) return -1;
    *out = (int)v;
    return 0;
}

static int config(Scenario *s, char *key, char *value) {
    int v;
    if (!strcmp(key, "policy")) {
        if (!strcmp(value, "srtf")) s->policy = POLICY_SRTF;
        else if (!strcmp(value, "fcfs")) s->policy = POLICY_FCFS;
        else return -1;
        return 0;
    }
    if (parse_int(value, &v)) return -1;
    if (!strcmp(key, "tick_ms")) s->tick_ms = v;
    else if (!strcmp(key, "ventilators")) s->ventilators = v;
    else if (!strcmp(key, "total_blood")) s->total[0] = v;
    else if (!strcmp(key, "total_icu")) s->total[1] = v;
    else if (!strcmp(key, "total_surgery")) s->total[2] = v;
    else if (!strcmp(key, "readers")) s->readers = v;
    else if (!strcmp(key, "writers")) s->writers = v;
    else if (!strcmp(key, "cache")) s->cache_size = v;
    else return -1;
    return 0;
}

int load_scenario(const char *path, Scenario *s, char *err, size_t err_len) {
    memset(s, 0, sizeof *s);
    s->policy = POLICY_SRTF; s->tick_ms = 300; s->ventilators = 5;
    s->readers = 3; s->writers = 2; s->cache_size = 4;
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, err_len, "cannot open %s: %s", path, strerror(errno)); return -1; }
    char line[1024]; int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++; char *p = trim(line);
        if (!*p || *p == '#') continue;
        if (*p == '@') {
            char *eq = strchr(++p, '=');
            if (!eq) { snprintf(err, err_len, "line %d: expected @key=value", lineno); goto bad; }
            *eq = '\0';
            if (config(s, trim(p), trim(eq + 1))) { snprintf(err, err_len, "line %d: invalid configuration", lineno); goto bad; }
            continue;
        }
        if (s->patient_count >= MAX_PATIENTS) { snprintf(err, err_len, "line %d: more than %d patients", lineno, MAX_PATIENTS); goto bad; }
        int v[15], n = 0;
        char *save = NULL;
        for (char *tok = strtok_r(p, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            if (n >= 15 || parse_int(trim(tok), &v[n])) { snprintf(err, err_len, "line %d: invalid numeric field", lineno); goto bad; }
            n++;
        }
        if (n != 15) { snprintf(err, err_len, "line %d: expected 15 CSV fields, got %d", lineno, n); goto bad; }
        PatientSpec *x = &s->patients[s->patient_count++];
        x->id=v[0]; x->arrival=v[1]; x->burst=v[2]; x->severity=v[3]; x->needs_vent=v[4];
        for (int j=0;j<3;j++) { x->max[j]=v[5+j]; x->req1[j]=v[8+j]; x->req2[j]=v[11+j]; }
        x->req2_at=v[14];
        if (x->id <= 0 || x->burst <= 0 || x->severity <= 0 || x->needs_vent > 1) { snprintf(err, err_len, "line %d: invalid id, burst, severity, or ventilator flag", lineno); goto bad; }
        for (int j=0;j<3;j++) if (x->req1[j] > x->max[j] || x->req1[j]+x->req2[j] > x->max[j]) { snprintf(err, err_len, "line %d: requests exceed maximum claim", lineno); goto bad; }
        if (x->req2_at >= x->burst) { snprintf(err, err_len, "line %d: req2_at must be below burst", lineno); goto bad; }
    }
    if (ferror(f)) { snprintf(err, err_len, "read error: %s", strerror(errno)); goto bad; }
    fclose(f);
    if (!s->patient_count) { snprintf(err, err_len, "scenario has no patients"); return -1; }
    if (s->tick_ms < 1 || s->cache_size < 1 || s->cache_size > MAX_PATIENTS ||
        s->readers > MAX_WORKERS || s->writers > MAX_WORKERS) { snprintf(err, err_len, "configuration value out of range"); return -1; }
    for (int i=0;i<s->patient_count;i++) for (int j=0;j<3;j++) if (s->patients[i].max[j] > s->total[j]) { snprintf(err, err_len, "patient %d maximum exceeds total resource %d", s->patients[i].id, j); return -1; }
    for (int i=0;i<s->patient_count;i++) for (int j=i+1;j<s->patient_count;j++) if (s->patients[i].id == s->patients[j].id) { snprintf(err, err_len, "duplicate patient id %d", s->patients[i].id); return -1; }
    return 0;
bad:
    fclose(f); return -1;
}
