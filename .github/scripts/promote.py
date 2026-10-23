#!/usr/bin/env python3
"""flyitalyadsb: promote the `canary` image to `stable` once cell B has run it
long enough and stayed in line with cell A.

flyitalyadsb runs two cells (nesso-infra docs/FLYITALYADSB-TWO-CELLS.md,
"Automatic release of readsb and tar1090"). argocd-image-updater deploys
`canary` to cell B and `stable` to cell A. This script is the gate between
them. It runs in GitHub Actions, never in the cluster, and reads the public,
read-only https://api.flyitalyadsb.com/api/cells.json (stats-api).

Promote only if all of these hold:
  - B runs the canary's commit, and B's readsb started after the canary image
    was built (a weekly rebuild keeps the commit: only the start time tells
    the new image from the old one);
  - B has run it without a restart for SOAK_HOURS;
  - B's data is fresh, and its aircraft and messages/s are at least MIN_RATIO
    of A's (A must answer: with A down there is nothing to compare with).
"Not yet" (not deployed, still soaking, A down) ends quietly. "B is unwell
on the new version" fails the job, so GitHub reports it, and stable stays
where it is.

Env: IMAGE (where builds and `stable` live), CANARY_IMAGE (where `canary`
lives, default IMAGE-canary: a separate repository, see docker.yaml), CELLS_URL,
COMMIT_FIELD, STARTED_FIELD, SOAK_HOURS, MIN_RATIO, MAX_DATA_AGE_S,
BOOTSTRAP_DIGEST (optional, once: point `stable` at this digest, i.e. what
cell A already runs), BOOTSTRAP_CANARY_DIGEST (optional, once: point `canary`
at this digest, i.e. what cell B already runs), DRY_RUN=1 (decide, don't
retag).
Stdlib only, plus `docker buildx imagetools`.
"""
import json
import os
import subprocess
import sys
import time
import urllib.request

IMAGE = os.environ["IMAGE"]
CANARY_IMAGE = os.environ.get("CANARY_IMAGE") or IMAGE + "-canary"
CELLS_URL = os.environ.get("CELLS_URL", "https://api.flyitalyadsb.com/api/cells.json")
COMMIT_FIELD = os.environ.get("COMMIT_FIELD", "readsb_commit")
STARTED_FIELD = os.environ.get("STARTED_FIELD", "readsb_started")
SOAK_HOURS = float(os.environ.get("SOAK_HOURS", "20"))
MIN_RATIO = float(os.environ.get("MIN_RATIO", "0.85"))
MAX_DATA_AGE_S = float(os.environ.get("MAX_DATA_AGE_S", "30"))
DRY_RUN = os.environ.get("DRY_RUN") == "1"
SUMMARY = os.environ.get("GITHUB_STEP_SUMMARY")


def say(msg):
    print(msg, flush=True)
    if SUMMARY:
        with open(SUMMARY, "a") as f:
            f.write(msg + "\n\n")


def inspect(ref, fmt):
    out = subprocess.run(["docker", "buildx", "imagetools", "inspect", ref, "--format", fmt],
                         check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def digest(tag, image=IMAGE):
    try:
        return inspect(f"{image}:{tag}", "{{json .Manifest}}")["digest"]
    except subprocess.CalledProcessError:
        return None


def labels(ref):
    """The OCI labels metadata-action sets: revision (the commit) and created."""
    image = inspect(ref, "{{json .Image}}")
    configs = image.values() if "config" not in image else [image]
    for c in configs:
        found = (c.get("config") or {}).get("Labels") or {}
        if found.get("org.opencontainers.image.revision"):
            return found
    return {}


def epoch(iso):
    """'2026-10-19T10:38:36.539Z' -> seconds, or None."""
    from datetime import datetime
    try:
        return datetime.fromisoformat(iso.replace("Z", "+00:00")).timestamp()
    except (AttributeError, ValueError):
        return None


def retag(tag, dgst, image=IMAGE):
    """Point image:tag at a digest the build pushed to IMAGE."""
    if DRY_RUN:
        say(f"DRY_RUN: would point `{image}:{tag}` at {dgst}")
        return
    subprocess.run(["docker", "buildx", "imagetools", "create", "--tag", f"{image}:{tag}", f"{IMAGE}@{dgst}"],
                   check=True)


def decide(canary_commit, cells, now, canary_created=None):
    """('promote' | 'wait' | 'fail', reason). Pure: tested in test_promote.py."""
    a, b = (cells or {}).get("a"), (cells or {}).get("b")
    if b is None:
        return "wait", "cell B doesn't answer"
    if a is None:
        return "wait", "cell A doesn't answer: nothing to compare with"
    running = b.get(COMMIT_FIELD)
    if not running or not canary_commit or not canary_commit.startswith(running):
        return "wait", f"cell B runs {running}, not the canary {canary_commit[:12] if canary_commit else None} yet"
    started = b.get(STARTED_FIELD)
    if not isinstance(started, (int, float)):
        return "wait", "cell B doesn't say since when it runs"
    if canary_created is None:
        return "wait", "the canary image doesn't say when it was built"
    if started < canary_created:
        return "wait", "cell B started before the canary was built: it still runs an older image of that commit"
    soaked_h = (now - started) / 3600
    if soaked_h < SOAK_HOURS:
        return "wait", f"cell B has run the canary for {soaked_h:.1f} h of {SOAK_HOURS:g}"
    problems = []
    age = b.get("data_age_s")
    if not isinstance(age, (int, float)) or age > MAX_DATA_AGE_S:
        problems.append(f"data age {age} s (max {MAX_DATA_AGE_S:g})")
    for field in ("aircraft", "messages_per_s"):
        va, vb = a.get(field), b.get(field)
        if not isinstance(va, (int, float)) or not isinstance(vb, (int, float)):
            problems.append(f"{field} unknown (A {va}, B {vb})")
        elif va > 0 and vb < MIN_RATIO * va:
            problems.append(f"{field} B {vb} < {MIN_RATIO:g} × A {va}")
    if problems:
        return "fail", "cell B on the canary is not in line with A: " + "; ".join(problems)
    return "promote", f"cell B has run {canary_commit[:12]} for {soaked_h:.1f} h, in line with A"


def main():
    bootstrap = os.environ.get("BOOTSTRAP_DIGEST", "").strip()
    bootstrap_canary = os.environ.get("BOOTSTRAP_CANARY_DIGEST", "").strip()
    if bootstrap or bootstrap_canary:
        if bootstrap:
            retag("stable", bootstrap)
            say(f"Bootstrap: `{IMAGE}:stable` now points at {bootstrap}.")
        if bootstrap_canary:
            retag("canary", bootstrap_canary, CANARY_IMAGE)
            say(f"Bootstrap: `{CANARY_IMAGE}:canary` now points at {bootstrap_canary}.")
        return 0

    canary, stable = digest("canary", CANARY_IMAGE), digest("stable")
    if canary is None:
        say("No `canary` image yet: nothing to promote.")
        return 0
    if stable is None:
        say("No `stable` image: run this workflow once with bootstrap_digest set to what cell A runs.")
        return 1
    if canary == stable:
        say(f"`stable` is already the canary ({canary[:19]}): nothing to do.")
        return 0

    found = labels(f"{IMAGE}@{canary}")
    commit = found.get("org.opencontainers.image.revision")
    created = epoch(found.get("org.opencontainers.image.created"))
    req = urllib.request.Request(CELLS_URL, headers={"User-Agent": "flyitalyadsb-promote/1.0"})
    with urllib.request.urlopen(req, timeout=20) as r:
        cells = json.load(r).get("cells")
    verdict, reason = decide(commit, cells, time.time(), created)
    say(f"Canary {commit} ({canary[:19]}), stable {stable[:19]}: **{verdict}**, {reason}.")
    if verdict == "promote":
        retag("stable", canary)
        return 0
    return 1 if verdict == "fail" else 0


if __name__ == "__main__":
    sys.exit(main())
