# Sequence Diagrams

Core workflows. Workflows 1–3 are **as implemented** in the widget
(`poller.rs` thread + `buddy_state.rs` + `main.rs`). The card is
**multi-server**: one `BuddyState` per configured server, wrapped in
`WidgetState`, and the poller thread serves the whole fleet.

## Workflow 1: One poll cycle (normal operation, multi-server)

One adaptive cycle from the background thread: **fetch every server without
the lock, then apply the batch under one short lock.**

```mermaid
sequenceDiagram
    participant T as poller thread
    participant S1 as server 1 (/slots)
    participant S2 as server 2 (/slots)
    participant State as WidgetState (Arc<Mutex>)
    participant UI as egui frame (main.rs)

    T->>T: sleep(min interval) then wake
    T->>S1: GET /slots (1500 ms) — NO lock held
    S1-->>T: 200 + JSON
    T->>S2: GET /slots (1500 ms) — NO lock held
    S2-->>T: 200 + JSON
    Note over T: defensive parse each -> (working, decode_total, n_remain)
    T->>State: lock ONCE; feed_ok/feed_error per server
    State->>State: toks=Δdecoded/Δt per row (if valid Δ)
    T->>State: min poll_interval(tuning) across servers -> next sleep
    T->>State: unlock
    T-->>UI: ctx.request_repaint()
    UI->>State: lock; clone() (one row per server)
    UI->>UI: draw per-server rows: big number / dot / model / N left
```

## Workflow 2: Error recovery (one server down, the others keep going)

One endpoint becomes unreachable while the others stay up. Slow-retry, no
crash — and the other rows are unaffected (only that server's `conn` flips).

```mermaid
sequenceDiagram
    participant T as poller thread
    participant S2 as server 2
    participant State as WidgetState
    participant UI as egui frame

    T->>S2: GET /slots (no lock)
    S2--xT: timeout / refused / 4xx / parse fail
    T->>State: lock; feed_error() for server 2 only
    State->>State: row2.conn=Down; toks=0; last_decode_total=None
    T->>State: min poll_interval -> 5000 ms (the down row dominates)
    T->>State: unlock
    T-->>UI: request_repaint
    UI->>UI: row2 draws "--" / "server down" (red); row1 still live
    Note over T: retries that server every 5 s; others poll on their own cadence
    T->>S2: GET /slots
    S2-->>T: 200
    T->>State: feed_ok(...)
    State->>State: row2.conn=Ok; fresh baseline (next working cycle shows —)
    T-->>UI: request_repaint
    UI->>UI: row2 draws live / idle
```

## Workflow 3: Mode toggle (click on card content) — implemented

The user **clicks anywhere on the card content** (below the drag strip, off
the close ×) to flip the whole card between the data card and the
combined-activity scene. There is no dedicated mode button. A real click
only: egui's `primary_clicked()` reports a click when the release lands
close to and near in time with the press, so dragging the strip (a window
move) never toggles. The mode is a single shared
`WidgetState::display_mode` — one click flips every row/band. The close ×'
right-click menu still offers the explicit switch + Quit. (The device `KEY`
toggle is still unverified — see
[modules/button.md](../../esp32c6_480x480/modules/button.md).)

```mermaid
sequenceDiagram
    participant User
    participant Input as card content (click)
    participant State as WidgetState
    participant UI as render (main.rs)

    User->>Input: click content (not strip / close ×)
    Input->>Input: primary_clicked() = true + pos on content
    Input->>UI: toggle mode (Data <-> Scene)
    UI->>State: lock; toggle_mode()
    State->>State: display_mode flips for the whole card
    UI->>State: unlock
    UI->>UI: re-render
    Note over UI: per-server phase still chooses each variant<br/>(data row / scene band: sprint, same-as-working, calm scene, piranha plant)
```
