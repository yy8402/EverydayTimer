# Changes

## v1.4.0 — 2026-11-03

- Replaced normal status text with a bottom-center state icon: hollow circle ready, triangle running, square stopped. Button hints remain separate action labels.
- Removed the duration-format caption, preserving automatic width fitting and the requested time formatting.
- Kept clock, storage and save errors explicit as text. A zero-millisecond completed run is correctly stopped, not ready.
- Added icon/state/long-value/error tests and previews. The timer, daily-star persistence, partition layout and native full-refresh fallback are unchanged.

## v1.3.0 — 2026-11-03

- Removed the visible UTC-offset subtitle while preserving configured local-time and reset calculations.
- Explicit side-button-lock tests cover active runs while display work is pending, blocked, completed or failed; front stop remains independent.
- Bottom daily-star row across activities: first benchmark and strictly shorter personal bests each earn one star. Up to five icons plus an exact overflow remainder.
- Stars reset at local 23:59, including overnight/offline catch-up. New records after that boundary belong to the new interval and survive midnight. Permanent bests and history do not reset.
- Awards are journaled with completed records; reset generations persist separately. Recovery deduplicates, clock corrections never revive cleared awards, and cosmetic state errors preserve normal records.
- Added reset/recovery/clock-correction and raster-layout tests, plus actual-renderer star previews.
- Same partition layout and compatible history schema; older history receives no retrospective stars. No physical hardware testing, and native full refresh remains the only enabled panel mode.

## v1.2.0 — 2026-11-03

- Centered local date/time and UTC-offset label. Clock updates are requested each minute boundary while running or idle.
- Mac time-sync automatically sends its current UTC offset; optional explicit override and timezone-only command. Offset persists separately from UTC RTC/history, with fixed-offset/DST limitations documented. Default is UTC+08:00 until overridden.
- Independent clock and number partial-region interfaces with bitwise coalescing; both capabilities remain false and native full refresh remains the fallback. Minute clock+timer updates share one full refresh.
- Added 72,322 clock/scheduler assertions, timezone persistence/validation tests, and invalid-clock preview.
- No partition/history schema change. Minute full refreshes increase idle flicker and battery use; hardware still untested.

## v1.1.0 — 2026-11-03

- Time display now omits zero leading units: `32.7 s`, `30:25.0`, `1:02:03.4`.
- Current and best use the same formatting. No rounding across the next minute/hour; saved milliseconds are unchanged.
- Adaptive numeric sizes keep long durations inside the screen. Colon formats include a small unit-format legend.
- Added 30,208 formatter/renderer assertions and minute/hour/seconds previews.
- Same partition layout and JSON schema as v1.0.0. Use the helper's `--update` only if this timer is already installed; its history storage is not erased by that mode.
- Hardware status unchanged: no physical-device test; partial-refresh capability remains false with native full-refresh fallback.

## v1.0.0 — 2026-11-03

Initial offline timer, English/basic Chinese action labels, stable IDs, shortest records, timestamped JSON history, USB clock/configuration/export manager, Mac full-backup flashing helper, and partial-display integration seam.
