//! Screenshot (golden-image) tests — the egui/eframe equivalent of Playwright's
//! `toHaveScreenshot()`.
//!
//! How it works:
//! * eframe's `__screenshot` feature (enabled in Cargo.toml) makes the binary
//!   read back real GPU pixels on the 2nd render pass when `EFRAME_SCREENSHOT_TO`
//!   is set, write a PNG, and `exit(0)`.
//! * `DESKBUDDY_SCENE=<name>` makes the binary seed a deterministic
//!   `BuddyState` and skip the poller (see `main.rs::scene_for`), so a render
//!   never depends on the live server.
//! * Pixel ratio is pinned to 2.0 and the host line is fixed in scene mode, so
//!   output is reproducible on this machine.
//!
//! Run:      `cargo test --test screenshot_test`
//! Re-baseline (after an intended UI change):
//!           `UPDATE_GOLDENS=1 cargo test --test screenshot_test`
//!
//! On a mismatch the actual image is saved next to the golden as
//! `<scene>.actual.png` so you can open both side by side.

use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

const BIN: &str = env!("CARGO_BIN_EXE_deskbuddy_widget");
const GOLDEN_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/snapshots");
const CAPTURE_TIMEOUT: Duration = Duration::from_secs(60);
/// Mean per-channel (0–255) difference allowed between actual and golden.
/// GPU text antialiasing can jitter single pixels; 1.5 average with a 2%
/// outlier budget (pixels differing by >16) is stable run-to-run.
const MAX_MEAN_DIFF: f64 = 1.5;
const MAX_OUTLIER_FRACTION: f64 = 0.02;
const OUTLIER_DIFF: u8 = 16;

/// One render pass: spawn the binary in scene mode, wait for the PNG, return
/// its path. Fails the test with the child's stderr on timeout/non-zero exit.
/// `mode` is `None` for the data card or `Some("scene")` for the pixel-art
/// display mode.
fn capture(scene: &str, mode: Option<&str>) -> PathBuf {
    let out = PathBuf::from(std::env::temp_dir()).join(format!("db_shot_{scene}.png"));
    let _ = std::fs::remove_file(&out);

    let mut cmd = Command::new(BIN);
    cmd.env("DESKBUDDY_SCENE", scene)
        .env("EFRAME_SCREENSHOT_TO", &out)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    if let Some(mode) = mode {
        cmd.env("DESKBUDDY_MODE", mode);
    }
    let mut child = cmd
        .spawn()
        .unwrap_or_else(|e| panic!("failed to spawn widget binary: {e}"));

    let deadline = Instant::now() + CAPTURE_TIMEOUT;
    loop {
        match child.try_wait() {
            Ok(Some(status)) => {
                let stderr = child.stderr.as_mut().map(|s| {
                    use std::io::Read;
                    let mut b = String::new();
                    s.read_to_string(&mut b).ok();
                    b
                });
                assert!(
                    status.success(),
                    "widget exited non-zero for scene {scene:?}: {status}\nstderr: {stderr:?}"
                );
                break;
            }
            Ok(None) => {
                assert!(Instant::now() < deadline, "widget did not capture within {CAPTURE_TIMEOUT:?} for scene {scene:?}");
                std::thread::sleep(Duration::from_millis(100));
            }
            Err(e) => panic!("wait failed for scene {scene:?}: {e}"),
        }
    }
    let _ = child.wait();
    assert!(out.exists(), "no screenshot written for scene {scene:?}");
    out
}

/// Load a PNG as RGBA8 (dims + bytes).
fn load_rgba(path: &Path) -> (u32, u32, Vec<u8>) {
    let img = image::open(path).unwrap_or_else(|e| panic!("cannot open PNG {path:?}: {e}"));
    let rgba = img.to_rgba8();
    (rgba.width(), rgba.height(), rgba.as_raw().to_vec())
}

/// Compare actual vs golden; returns `None` if within tolerance.
fn compare(actual: &Path, golden: &Path) -> Option<String> {
    let (aw, ah, a) = load_rgba(actual);
    let (gw, gh, g) = load_rgba(golden);
    if (aw, ah) != (gw, gh) {
        return Some(format!("size mismatch: actual {aw}x{ah}, golden {gw}x{gh}"));
    }
    let mut sum = 0u64;
    let mut outliers = 0u64;
    for i in 0..a.len() / 4 {
        let mut px_max = 0u64;
        for c in 0..4 {
            let d = a[i * 4 + c] as i32 - g[i * 4 + c] as i32;
            let d = d.abs() as u64;
            sum += d;
            px_max = px_max.max(d);
        }
        if px_max > OUTLIER_DIFF as u64 {
            outliers += 1;
        }
    }
    let px = (a.len() / 4) as f64;
    let mean = sum as f64 / (px * 4.0);
    let frac = outliers as f64 / px;
    if mean > MAX_MEAN_DIFF || frac > MAX_OUTLIER_FRACTION {
        Some(format!("mean per-channel diff {mean:.2} (max {MAX_MEAN_DIFF}), outlier fraction {frac:.4} (max {MAX_OUTLIER_FRACTION})"))
    } else {
        None
    }
}

/// All scenes the widget can render deterministically.
const SCENES: &[&str] = &["generating", "prompting", "idle", "down"];

#[test]
fn card_matches_golden_for_each_phase() {
    let update = std::env::var("UPDATE_GOLDENS").is_ok();
    std::fs::create_dir_all(GOLDEN_DIR).expect("create golden dir");

    let mut failures = Vec::new();
    for scene in SCENES {
        // Two display modes per phase: the data card (`None`) and the
        // pixel-art scene (`Some("scene")`).
        let labels = [scene.to_string(), format!("scene_{scene}")];
        for (label, mode) in labels.iter().zip([None::<&str>, Some("scene")]) {
            let golden = Path::new(GOLDEN_DIR).join(format!("{label}.png"));
            let actual = capture(scene, mode);

            if update {
                std::fs::copy(&actual, &golden)
                    .unwrap_or_else(|e| panic!("failed to update golden {golden:?}: {e}"));
                println!("updated golden for {label:?}");
            } else if !golden.exists() {
                let actual_copy = Path::new(GOLDEN_DIR).join(format!("{label}.actual.png"));
                std::fs::copy(&actual, &actual_copy).ok();
                failures.push(format!(
                    "{label}: no golden {golden:?} — run `UPDATE_GOLDENS=1 cargo test --test screenshot_test` to create it"
                ));
            } else if let Some(msg) = compare(&actual, &golden) {
                let actual_copy = Path::new(GOLDEN_DIR).join(format!("{label}.actual.png"));
                std::fs::copy(&actual, &actual_copy).ok();
                failures.push(format!("{label}: {msg} (actual saved to {actual_copy:?})"));
            } else {
                println!("{label}: matches golden");
            }
            let _ = std::fs::remove_file(&actual);
        }
    }

    assert!(
        failures.is_empty(),
        "screenshot test failures:\n{}",
        failures.join("\n")
    );
}
