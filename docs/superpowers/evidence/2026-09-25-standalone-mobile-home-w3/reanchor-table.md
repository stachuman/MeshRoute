# W3 re-anchor table (§2.5) — MUTS_MODEL, labels unchanged, meanings preserved

Generated from the registry by AST (the harness is never imported). Old = the W1c candidate (`336e4c13…`, reconstructed byte-exact from the final file); new = the final file. Every other MUTS_MODEL entry (231) and the sliceCbudget entry are byte-identical. REDs: see union-summary.txt.

## M14 — M14 pages FLOORS instead of ceiling
- meaning kept: a partial page is floored away (39 bytes -> 1 page)
- old find:
```
const uint8_t p = uint8_t((n + kDetailPageChars - 1) / kDetailPageChars);
```
- old replace:
```
const uint8_t p = uint8_t(n / kDetailPageChars);
```
- new find:
```
const uint32_t p = (uint32_t(len) + per - 1) / per;
```
- new replace:
```
const uint32_t p = uint32_t(len) / per;
```

## M15 — M15 an empty body yields ZERO pages
- meaning kept: an empty body yields zero pages
- old find:
```
_st.detail_pages = p ? p : uint8_t(1);
```
- old replace:
```
_st.detail_pages = p;
```
- new find:
```
return p ? uint8_t(p) : uint8_t(1);
```
- new replace:
```
return uint8_t(p);
```

## M18 — M18 both body rows render the SAME 19 columns
- meaning kept: every row re-reads row 0's columns (row 0 repeated)
- old find:
```
const uint16_t i = uint16_t(off + uint16_t(row) * kDetailCols + n);
```
- old replace:
```
const uint16_t i = uint16_t(off + n);
```
- new find:
```
const uint32_t i = off + uint32_t(row) * Cols + n;
```
- new replace:
```
const uint32_t i = off + n;
```

## M55 — M55 the §4 gate tests UNSAVED first, so a CONFLICT is told to SAVE (plan §4's conflation, through the ORDER)
- meaning kept: unsaved tested before conflict: a both-flags draft is told SAVE
- old find:
```
                if (_cfg->conflict())               { _st.prov_block = ProvBlock::conflict; break; }
                if (_cfg->config_unsaved())         { _st.prov_block = ProvBlock::unsaved;  break; }
```
- old replace:
```
                if (_cfg->config_unsaved())         { _st.prov_block = ProvBlock::unsaved;  break; }
                if (_cfg->conflict())               { _st.prov_block = ProvBlock::conflict; break; }
```
- new find:
```
        if (_cfg->conflict())       { _st.prov_block = ProvBlock::conflict; return false; }
        if (_cfg->config_unsaved()) { _st.prov_block = ProvBlock::unsaved;  return false; }
```
- new replace:
```
        if (_cfg->config_unsaved()) { _st.prov_block = ProvBlock::unsaved;  return false; }
        if (_cfg->conflict())       { _st.prov_block = ProvBlock::conflict; return false; }
```

## M56 — M56 the UNSAVED cell is dropped — PROVISION opens over an unsaved draft (§3.6.3's precondition gone)
- meaning kept: the unsaved refusal dropped: an unsaved draft is admitted
- old find:
```
                if (_cfg->config_unsaved())         { _st.prov_block = ProvBlock::unsaved;  break; }
```
- old replace:
```
                ;
```
- new find:
```
        if (_cfg->config_unsaved()) { _st.prov_block = ProvBlock::unsaved;  return false; }
```
- new replace:
```
        ;
```

## M57 — M57 the CONFLICT cell is dropped — the two states collapse into one
- meaning kept: the conflict refusal dropped: conflict-only admitted, both-flags mis-noted SAVE
- old find:
```
                if (_cfg->conflict())               { _st.prov_block = ProvBlock::conflict; break; }
```
- old replace:
```
                ;
```
- new find:
```
        if (_cfg->conflict())       { _st.prov_block = ProvBlock::conflict; return false; }
```
- new replace:
```
        ;
```

## M59 — M59 the gate SAVES on the operator's behalf and then opens (the helpful write C2 forbids)
- meaning kept: the unsaved guard saves the draft and admits, so the arm enters PROVISION (the write C2 forbids)
- old find:
```
                if (_cfg->config_unsaved())         { _st.prov_block = ProvBlock::unsaved;  break; }
```
- old replace:
```
                if (_cfg->config_unsaved())         { (void)_cfg->save(); enter_provision(Provision::menu); break; }
```
- new find:
```
        if (_cfg->config_unsaved()) { _st.prov_block = ProvBlock::unsaved;  return false; }
```
- new replace:
```
        if (_cfg->config_unsaved()) { (void)_cfg->save(); return true; }
```

## M100 — M100 [[B232]] the ConfigService is opened only when the MENU is entered (the defer-to-browsing fix)
- meaning kept: the opener runs only off the closed view: passive arrival takes no baseline
- old find:
```
            (void)_cfg->open();
```
- old replace:
```
            if (_st.settings != Settings::closed) (void)_cfg->open();
```
- new find:
```
        ensure_config_open();                                   // ★ ON ARRIVAL, above the closed-view return below
```
- new replace:
```
        if (_st.settings != Settings::closed) ensure_config_open();
```
