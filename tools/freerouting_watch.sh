#!/bin/sh
# freerouting_watch.sh -- has Freerouting released its fixes yet? (D83)
#
# P32W routes with its own build of Freerouting's master plus two open pull
# requests (generators/build_freerouting.sh): #1021 (the fix for issue #1017,
# the stalling swap loop) and #1055 (issue #1054, ripped-up connections not
# restored). This read-only check reports the latest release, the two pull
# requests and issues, whether the latest release contains both fixes, and
# what changed since the last run (state in ~/freerouting/p32w-fixes/
# watch-state.json). It downloads and changes nothing else.
# Needs gh (logged in) or curl, and python3.
STATE=$HOME/freerouting/p32w-fixes/watch-state.json
api() { if command -v gh > /dev/null 2>&1; then gh api "repos/freerouting/freerouting/$1" 2> /dev/null
        else curl -fsS "https://api.github.com/repos/freerouting/freerouting/$1"; fi; }
REL=$(api releases/latest); P1=$(api pulls/1021); P2=$(api pulls/1055); I1=$(api issues/1017); I2=$(api issues/1054)
TAG=$(printf '%s' "$REL" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tag_name",""))')
contains() {   # contains MERGE_SHA: does the latest release have it?
    [ -n "$1" ] || { echo "no"; return; }
    api "compare/$1...$TAG" | python3 -c 'import json,sys; print("yes" if json.load(sys.stdin).get("status") in ("ahead","identical") else "no")'
}
M1=$(printf '%s' "$P1" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["merge_commit_sha"] if d.get("merged") else "")')
M2=$(printf '%s' "$P2" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["merge_commit_sha"] if d.get("merged") else "")')
C1=$(contains "$M1"); C2=$(contains "$M2")
python3 - "$STATE" "$REL" "$P1" "$P2" "$I1" "$I2" "$C1" "$C2" <<'EOF'
import json, sys, datetime, os
state, rel, p1, p2, i1, i2, c1, c2 = sys.argv[1], *[json.loads(a) for a in sys.argv[2:7]], sys.argv[7], sys.argv[8]
def pr(d): return "merged" if d.get("merged") else d.get("state", "?")
new = {"checked": datetime.date.today().isoformat(), "latest_release": rel.get("tag_name"),
       "latest_release_published": rel.get("published_at"), "pr_1021": pr(p1), "pr_1055": pr(p2),
       "issue_1017": i1.get("state"), "issue_1054": i2.get("state")}
old = json.load(open(state)) if os.path.exists(state) else {}
changes = ["%s: %s -> %s" % (k, old.get(k), new[k]) for k in new if k != "checked" and k in old and old[k] != new[k]]
os.makedirs(os.path.dirname(state), exist_ok=True)
json.dump(new, open(state, "w"), indent=2)
both = c1 == "yes" and c2 == "yes"
print("Freerouting: latest release %s (%s); PR #1021 %s, PR #1055 %s; issues #1017 %s, #1054 %s." %
      (new["latest_release"], (new["latest_release_published"] or "")[:10], new["pr_1021"], new["pr_1055"],
       new["issue_1017"], new["issue_1054"]))
print("Changed since %s: %s" % (old.get("checked", "never"), "; ".join(changes) if changes else "nothing"))
if both:
    print("The latest release contains BOTH fixes: P32W can go back to it (download its jar, point FRJAR's "
          "default in generators/build_board.sh at it, re-route a card to confirm, update D83 and the backlog).")
elif "closed" in (new["pr_1021"], new["pr_1055"]):
    print("A fix's pull request was closed without merging: P32W's own build rests on an abandoned change.")
else:
    print("The latest release does not contain both fixes yet: keep P32W's own build.")
EOF
