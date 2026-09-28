# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B459 §2.2.1 — the manifest at the freeze against the PRE-CHANGE identity set (derive_prechange.py). They must be
# equal, and the ONE permitted instrumentation change is P4a: the base binary executed ONE label `P4a` ten times per
# arm (a multiplicity, not ten identities); the manifest names those ten executions P4a/0 … P4a/9.
# Usage: python3 manifest_compare.py <expected.tsv> <prechange.json> <out.json>
import collections, json, re, sys
man = [tuple(l.split('\t')[:2]) for l in open(sys.argv[1], encoding='utf-8').read().split('\n') if l and not l.startswith('#')]
pre = json.load(open(sys.argv[2]))
pids = [tuple(x) for x in pre['identities']]
# the base binary's own view: P4a keys folded back to the one label it printed, with its multiplicity
fold = lambda i: re.sub(r'^(canvas/v[34]/P4a)/\d+$', r'\1', i)
base_multiset = collections.Counter(fold(i) for i, _ in pids)
man_multiset = collections.Counter(fold(i) for i, _ in man)
res = {'manifest': len(man), 'prechange': len(pids),
       'equal_as_sets_with_outcomes': sorted(man) == sorted(pids),
       'only_in_manifest': sorted(set(man) - set(pids)), 'only_in_prechange': sorted(set(pids) - set(man)),
       'p4a': {arm: {'base_label_executions': base_multiset[f'canvas/{arm}/P4a'],
                     'manifest_keys': sorted(i for i, _ in man if i.startswith(f'canvas/{arm}/P4a/'))} for arm in ('v3', 'v4')},
       'folded_multisets_equal': base_multiset == man_multiset,
       'counts': pre['counts'], 's3_slug_table': pre['s3_slug_table']}
res['verdict'] = 'PASS' if res['equal_as_sets_with_outcomes'] and res['folded_multisets_equal'] else 'FAIL'
json.dump(res, open(sys.argv[3], 'w'), indent=1)
print(f"manifest {len(man)} vs pre-change {len(pids)}: equal {res['equal_as_sets_with_outcomes']}; "
      f"P4a: base label executed {res['p4a']['v3']['base_label_executions']}x (v3) / {res['p4a']['v4']['base_label_executions']}x (v4) "
      f"-> manifest keys {len(res['p4a']['v3']['manifest_keys'])} + {len(res['p4a']['v4']['manifest_keys'])}; "
      f"folded multisets equal {res['folded_multisets_equal']} -> {res['verdict']}")
