#!/usr/bin/env python3
from pathlib import Path

path = Path("tools/one_shot_patch_duplex_review_fixes.py")
text = path.read_text(encoding="utf-8")
old = '''replace_once(
    a2dp,
    "record_rejected_unbound_event(bound->peer_mac);",
    "record_rejected_unbound_event(\\n            bound->peer_mac, \\"PROFILE_NO_ACTIVE_BINDING\\");",
)
# There are two remaining unbound calls: generation refresh and audio capture.
'''
new = '''text = Path(a2dp).read_text(encoding="utf-8")
needle = "record_rejected_unbound_event(bound->peer_mac);"
if text.count(needle) != 2:
    raise RuntimeError(
        f"expected two bound-peer unbound calls, found {text.count(needle)}"
    )
text = text.replace(
    needle,
    "record_rejected_unbound_event(\\n            bound->peer_mac, \\"PROFILE_NO_ACTIVE_BINDING\\");",
    1,
)
Path(a2dp).write_text(text, encoding="utf-8")
# There are two remaining unbound calls: generation refresh and audio capture.
'''
if text.count(old) != 1:
    raise RuntimeError(f"expected one patch-script block, found {text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
