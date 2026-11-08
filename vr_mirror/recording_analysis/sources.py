# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Finding what belongs to a recording: the OBS log that recorded it (its start and stop) and the
plugin's frame trace that was open then. Knows nothing about the analysis: it only reads the log.

A recording '2026-11-04 19-35-51_s2.mp4' (OBS names files by their start; '_s1',
'_s2', ... may follow) belongs to the log with a '==== Recording Start' within 5 s of 19:35:51 on that
day. Its trace is the one named in the last 'frame trace:' line of the plugin before that start.
Times are wall-clock seconds since the midnight of the log's first day (past midnight they keep
counting up).
"""

import datetime
import glob
import os
import re

LOGS = os.path.expandvars(r"%APPDATA%\obs-studio\logs")
LINE = re.compile(r"(\d\d):(\d\d):(\d\d\.\d+): (.*)")


def LogLines(path):
    """(wall s, text) of every timed line of an OBS log."""
    last, day = 0.0, 0.0
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LINE.match(line)
            if not m:
                continue
            t = int(m[1]) * 3600 + int(m[2]) * 60 + float(m[3]) + day
            if t < last - 43200:
                day += 86400
                t += 86400

            last = t
            yield t, m[4].rstrip("\r\n")


def RecordingStart(video):
    """The date and time in a recording's file name."""
    m = re.search(r"(\d{4})-(\d\d)-(\d\d) (\d\d)-(\d\d)-(\d\d)(?:_\w+)?\.\w+$", video)
    if not m:
        raise SystemExit(f"no 'YYYY-MM-DD HH-MM-SS' in the file name {video}")
    return datetime.datetime(*map(int, m.groups()))


def LogStart(path):
    m = re.search(r"(\d{4})-(\d\d)-(\d\d) (\d\d)-(\d\d)-(\d\d)\.txt$", path)
    return datetime.datetime(*map(int, m.groups())) if m else None


def FindInLog(path, video):
    """The recording in one log: {'log', 'start', 'stop', 'trace', 'trace_opened'} or None. 'trace' is
    the trace's path prefix (without '_pimax.csv'), None if no trace was open; 'stop' None if the log
    ends first."""

    when = RecordingStart(video)
    day0 = LogStart(path)
    if day0 is None:
        return None
    want = (when - datetime.datetime(day0.year, day0.month, day0.day)).total_seconds()
    trace = opened = rec = None
    for t, text in LogLines(path):
        if rec is None:
            m = re.search(r"\[simoptix_vr_mirror\] frame trace: (.*)_ticks\.csv", text)
            if m:
                trace, opened = m[1], t
            elif text.endswith("[simoptix_vr_mirror] frame trace closed"):
                trace = opened = None
            elif text.startswith("==== Recording Start") and abs(t - want) <= 5:
                rec = {"log": path, "start": t, "stop": None, "trace": trace, "trace_opened": opened}
        elif text.startswith("==== Recording Stop"):
            rec["stop"] = t
            break

    return rec


def Find(video, logs=LOGS):
    """The recording's log entry (FindInLog), searching the logs that began up to two days before it."""
    when = RecordingStart(video)
    found = []
    for path in sorted(glob.glob(os.path.join(logs, "*.txt"))):
        begun = LogStart(path)
        if begun is None or not when - datetime.timedelta(days=2) <= begun <= when:
            continue
        rec = FindInLog(path, video)
        if rec:
            found.append(rec)

    if not found:
        raise SystemExit(f"no OBS log in {logs} has a recording starting at {when}")
    return found[-1]  # the latest log that has it (an older one can't have recorded at that moment)
