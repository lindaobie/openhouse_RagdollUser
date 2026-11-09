# Test recordings

Short recordings the tests replay (`TestCapture()` in `tests/common/test_paths.hpp` looks
here first, then in the local `captures/` folder). Each folder is what `lhcapture` writes:
`input.lhraw` (the raw device data), `device-configs/` (each device's config as read from
it), `lighthousedb.json` (SteamVR's room at the time), `session.json` and `markers.jsonl`.

Some are cut to their first seconds with `lhtool trim IN OUT --to SECONDS`; their
`session.json` and `markers.jsonl` still describe the full recording.

| Folder | Light | What | Length |
|---|---|---|---|
| `controller-moving-hidden` | 1.0, 2 bases | An Index controller waved around and hidden; headset still | full |
| `controller_static_10s.lhraw` | 1.0 | A controller lying still | 10 s |
| `floor-reflection` | 1.0 | Tracker and controller on the floor; one sensor each reads a reflection | full |
| `session-20261016-142931Z-147083` | 1.0, 2 bases | Headset and controller in the shared room | first 60 s |
| `ms-a` | 1.0, lhbs multi-station | 2 stations in slots 0 and 1 of 4 | first 30 s |
| `ms-n2-all` | 1.0, lhbs multi-station | 2 stations, flash policy ALL | first 30 s |
| `ms-n2-startup` | 1.0, lhbs multi-station | Stations woken from standby; light starts ~49 s in | full |
| `lh2-one-controller` | 2.0, 4 bases | One Index controller | full (31 s) |
| `lh2-five-devices` | 2.0, 4 bases | Two Index controllers and three trackers | first 30 s |

To add one: record with `lhcapture`, trim it to what the test needs, put the folder here
and add a row above. Keep them small: the whole folder is part of every clone.
