import random, sys, json
sys.dont_write_bytecode = True
sys.path.insert(0, "tools")
import test_probe_inbox_transcript as P
t = P.load()
good = open("docs/superpowers/evidence/2026-10-01-b478-b487-b488-qa/ledger-full_headless.txt").read()
lines = good[:-1].split("\n")
rng = random.Random(491)
alphabet = "0123456789=[]\\x -+aZ\n\r" + "9" * 20
escapes, refused, accepted = [], 0, 0
for k in range(4000):
    ls = list(lines)
    i = rng.randrange(len(ls))
    op = rng.choice(["insert", "delete", "replace", "dup", "drop", "digits", "append"])
    s = ls[i]
    j = rng.randrange(len(s) + 1)
    if op == "insert": ls[i] = s[:j] + rng.choice(alphabet) + s[j:]
    elif op == "delete": ls[i] = s[:j] + s[j + 1:]
    elif op == "replace": ls[i] = s[:j] + rng.choice(alphabet) + s[j + 1:]
    elif op == "dup": ls.insert(i, s)
    elif op == "drop": del ls[i]
    elif op == "digits": ls[i] = s[:j] + "9" * rng.choice([10, 4400, 5000]) + s[j:]
    elif op == "append": ls[i] = s + " " + rng.choice(["x", "=", "a=b", "malformed", "9" * 50])
    text = "\n".join(ls) + "\n"
    try:
        t.validate_ledger(text, "fuzz")
        accepted += 1
    except t.LedgerError:
        refused += 1
    except Exception as e:
        escapes.append({"case": k, "op": op, "line": i, "exception": type(e).__name__, "message": str(e)[:200]})
print(json.dumps({"cases": 4000, "refused": refused, "accepted_valid": accepted, "escapes": len(escapes), "first": escapes[:5]}))
