# Feature: goal-bar

Locator: odd/tasks/goal-bar.md · Engram topic: odd/goal-bar/tasks · Branch: feat/goal-bar

## Objective
Native OBS plugin "Goal Bar": an on-scene progress bar toward a live goal (YouTube likes/subs first, Twitch followers/subs second) that fires actions when the goal is reached. macOS arm64 first, then Windows x64 and Linux x86_64 through GitHub Actions, same mold as ../multi-stream.

## Specs (verbatim)
- S1: "crear un plugin para meta de subs o me gustas en vivo, de manera nativa en obs, iniciemos en mac y luego sacamos windows y linux, como lo hemos hecho, manten buenas practicas y una ux ui simple"
- S2: "agrega que al cumplir la meta lance un audio o video o verifica tambien que mas se puede hacer"
- S3: YouTube (subs + likes) is provider 1, Twitch is provider 2 (user: "si").

## Constraints
- Pure C++ core (no OBS, no Qt) with CTest and test-first; OBS/Qt/network live in adapters.
- Reuse multi-stream building blocks: OAuth PKCE + localhost, secret store, auto-update, buildspec/CMake presets, CI workflow.
- OBS 33.0.1 / obs-deps 2026-10-23, macOS 13.0+ arm64, Xcode generator.
- Simple UX: one source ("Goal Bar"), minimal properties, accounts dialog under Tools, manual mode always available.
- Artifacts (code, UI copy, docs) in English; en-US + es-ES locale.
- RDD is off (global) → no native review ceremony; ordinary checks only.

## Verified facts
- YouTube `statistics.subscriberCount` is rounded down to 3 significant figures above 1000, even for the authorized owner (YouTube revision history, 2019-09-10). Sub goals move in steps; likes are the default YouTube metric.
- `channels.list` costs 1 quota unit; `liveBroadcasts.list?mine=true&broadcastStatus=active` works with `youtube.readonly`.
- OBS APIs for goal actions exist: `obs_sceneitem_set_visible`, media `restart`, `media_ended` signal, `obs_frontend_set_current_scene`, `obs_frontend_replay_buffer_save`, `obs_frontend_take_screenshot`, frontend hotkeys.

## T4 research (2026-11-09)
- Quota: 10,000 units/day per Google Cloud project, reset at midnight PT; reads cost 1 unit; more quota requires a compliance audit. It is per project, so every user of a shared client ID drains the same pool. (developers.google.com/youtube/data/getting-started, quota_and_compliance_audits)
- Desktop OAuth: loopback redirect + PKCE remain supported for desktop apps; `client_secret` is listed optional for code exchange but Google's desktop samples send it and the refresh reference marks it required → send it (desktop clients are public, the secret is not confidential). Not compiled into git: inject at build time. (developers.google.com/identity/protocols/oauth2/native-app)
- Verification: in Testing mode the app needs no verification but is capped at 100 lifetime test users, shows the unverified screen, and test authorizations/refresh tokens expire after 7 days. YouTube scopes may be "sensitive"; confirm the lock icon in Cloud Console. (support.google.com/cloud/answer/15549945, 7454865, 9110914)
- Polling budget: likes via one `videos.list` (statistics + liveStreamingDetails) per 15 s ≈ 240 units/h per streamer; subs via `channels.list` far less often (rounded anyway).
- Decision (user, 2026-11-09): public mode like multi-chat — read `/@channel/live` without OAuth. Verified on a live stream: page carries `"likeCount":"5150"` (exact) and `subscriberCountText` "15.8 million subscribers" (rounded, same as API). Tradeoffs accepted: against YouTube ToS (same as multi-chat), breaks if HTML changes (auto-update mitigates), hidden likes unavailable. The lighter `youtubei/v1/updated_metadata` poll was NOT verified (blocked by auto-mode classifier); use full-page polling for now.

## Tasks
- [x] T1 Scaffold: CMake + presets + buildspec, plugin entry, core library + CTest target, macOS build. Route: delegated (2+ non-trivial files, reads multi-stream to prepare the write). Evidence: `ctest --test-dir build_tests` 1/1 passed (parent re-ran); `cmake --preset macos` + `cmake --build --preset macos` BUILD SUCCEEDED; bundle build_macos/RelWithDebInfo/obs-goal-bar.plugin present. PENDING: load check inside OBS (not installed yet; done at T3 when there is something visible). RDD: off (no review). ~170 authored lines (template copies excluded). Commit: 1a44786.
- [x] T2 Core domain: `Goal` (start, current, target, progress, reached event fired once and persisted, next goal step), `MetricProvider` port, manual provider, subscriber rounding helpers, settings codec. Test-first. Route: delegated (6 new core units + tests). Evidence: RED per unit via wrong-value stubs, then GREEN; parent fix: `align_target_to_step` now uses the step of max(target, count) (RED observed: got 10050 expected 10100, then GREEN). `ctest --test-dir build_tests` 6/6 passed; `cmake --build --preset macos` BUILD SUCCEEDED. RDD: off. ~680 authored lines (~330 src, ~350 tests; above heuristic because of test coverage). Commit: 918a858.
- [x] T3 Native source "Goal Bar": QPainter → gs_texture, redraw only on change; properties (title, metric, current, goal, style preset, accent/text color, size, reset). Usable in manual mode. Route: delegated (core + render + OBS adapter). Evidence: RED→GREEN for compact_number, goal_view, style_preset, Goal::set_target/reset; `ctest` 9/9 passed (parent re-ran); `cmake --build --preset macos` BUILD SUCCEEDED; offscreen render sheet of the 3 presets inspected by parent (scratchpad smoke/sheet.png). In-OBS check: OBS log 2026-11-09 01-11-39 shows "[obs-goal-bar] loaded (version 0.1.0)", a 'Barra de meta' (goal_bar_source) in the scene, and a clean unload. Known limits: Cancel in properties may revert reached flag; INT_MAX cap on value fields. RDD: off. ~920 authored lines (~250 tests). Commit: 1318246.
- [x] T4 (evidence: RED→GREEN for goal_view status, youtube_page, poll_schedule, provider mapping with fakes; `ctest` 12/12 passed (parent re-ran); `cmake --build --preset macos` BUILD SUCCEEDED; offline parse of a real live page: live=1 likes=5150 subs=15800000. Route: delegated. RDD: off. ~1600 changed lines incl. ~490 copied net layer and ~370 tests. In-OBS check: log 2026-11-09 01-27-37 shows provider for @alejoxcode and "…/live?hl=en: live". Follow-ups moved to T5: first reading after provider start must be a baseline (no celebration if already above target); fetch cancellation so source destroy/OBS quit doesn't wait up to the 10 s timeout. Commit: 2175f6d.) YouTube provider (public mode, no login, like multi-chat): poll the public `/@channel/live` page every 30–60 s via the native net layer (reuse multi-chat src/net, avoids the Qt TLS issue); parse live video id, exact `likeCount`, rounded `subscriberCountText`; states NotLive/Error with backoff; rounding notice + target alignment for subs. Parsers in core with trimmed fixtures. OAuth provider deferred (port stays open for it).
- [x] T5 Goal actions v1: show chosen scene source + restart media + hide on `media_ended` (audio/video) or after N s, built-in celebration (glow + seeded confetti, ~4 s), auto next goal, "Test celebration" button; plus baseline-on-first-reading and abortable fetches (macOS task cancel, WinHTTP handle close, curl multi wakeup). Route: delegated. Evidence: RED→GREEN for goal_reading, action_sequence, celebration, provider prompt-stop (<200 ms); `ctest` 16/16 passed (parent re-ran); `cmake --build --preset macos` BUILD SUCCEEDED; celebration contact sheet inspected by parent (scratchpad smoke5/sheet.png). net-linux syntax-checked only; net-win not compiled (T9). PENDING: in-OBS check of show-source/media restart/media_ended/Test button. Known limits → T7: Reset goal with current ≥ target re-fires actions; source stays visible if bar destroyed mid-celebration; looping media hides only at 300 s cap; properties Cancel may revert an auto-advanced target. RDD: off. ~1500 authored lines (~330 tests).
- [ ] T6 Twitch provider: followers + subs (reuse multi-stream Twitch OAuth).
- [ ] T7 Polish: fill animation, en/es locale, hotkeys (test celebration, reset goal), README.
- [ ] T8 Goal actions v2: switch scene, save replay buffer clip, screenshot (opt-in checkboxes).
- [ ] T9 Windows + Linux: GitHub Actions build + packages + release.

## Acceptance criteria
- Core suite green; macOS plugin builds and loads in OBS 33 without errors in the log.
- Goal Bar renders in a scene, updates on metric change, fires actions exactly once per goal.

## Delivery
- Strategy: ask-on-risk (default). Forecast well above 400 authored lines → chain strategy: stacked-to-main (user choice, 2026-11-09). Slice boundaries recorded here as PRs are planned; no remote exists yet.
- Running authored lines: ~4870 (T1 ~170, T2 ~680, T3 ~920, T4 ~1600, T5 ~1500).
- Slice plan (stacked-to-main): PR1 = T1+T2 (core foundation); PR2 = T3; next slices decided as they land.

## Progress / evidence
- Repo initialized on feat/goal-bar (no commits yet).
- Engram mirror: PENDING (mem_save blocked by auto-mode hook check); resync when available.

## Next step
Confirm in-OBS load/render of T3, then T4 YouTube provider (resolve open OAuth/quota uncertainties first).
