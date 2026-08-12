#ifndef SEMVER_H
#define SEMVER_H

#include "kernel.h"   // fixed-width types

// A strict Semantic Versioning 2.0.0 (semver.org) parser + precedence comparator.
//
// A version is  MAJOR.MINOR.PATCH[-prerelease][+build]  where MAJOR/MINOR/PATCH are
// non-negative integers with no leading zeros, `prerelease` is a dot-separated list of
// identifiers ([0-9A-Za-z-], each non-empty, numeric ones without leading zeros), and
// `build` is dot-separated identifiers ignored for precedence. semver_t.pre points into
// the original string (which must outlive the struct); pre_len is 0 when absent.
typedef struct {
    uint64_t    major, minor, patch;
    const char* pre;       // prerelease substring (NOT NUL-terminated), or NULL
    int         pre_len;   // its length, 0 if none
    int         valid;
} semver_t;

// Parse `s` into `v`. Returns 0 on success, -1 if `s` is not valid semver.
int semver_parse(const char* s, semver_t* v);

// Compare two parsed versions by semver precedence (§11): core numeric, then a version
// WITH a prerelease is lower than the same version without, then prerelease identifiers
// (numeric < alphanumeric; numeric compared as numbers; a longer identifier list wins
// when the shared prefix is equal). Build metadata is ignored. Returns -1 / 0 / +1.
int semver_cmp(const semver_t* a, const semver_t* b);

// Known-answer self-test (the `semver` CI KAT): 0 on success, else the failing case.
int semver_selftest(void);

#endif // SEMVER_H
