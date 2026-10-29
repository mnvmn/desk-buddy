//! Widget configuration: the inference servers to watch plus the extracted
//! poll-cadence values.
//!
//! Loaded from a JSON file (see [`config_path`]) so the server list is
//! **data, not code** — adding a second inference server is an edit, not a
//! recompile. Missing file → defaults (and the defaults are written back so
//! there is a file to edit next time). Malformed JSON → defaults + a warning;
//! we never crash on our own config.
//!
//! Env overrides (tests / point-at-a-different-host without a file):
//! * `DESKBUDDY_CONFIG` — path to the JSON config file;

use crate::buddy_state::PollTuning;
use serde::Deserialize;

/// Default hosts (LAN hosts, NOT 127.0.0.1 — spec §3.1): the three current
/// inference machines (see docs-report/docs/servers). Row order = card order.
///   * `DEFAULT_HOST`   — tower1: llama.cpp `server` (Qwen3.8-27B-UD-Q5_K_XL).
///   * `DEFAULT_HOST2`  — strix1: Gufo `serve llm` (Qwen3.8-27B-UD-Q6_K_XL +
///     DFlash2; the `m.strix` box, reinstalled + renamed 2026-10-25).
///   * `DEFAULT_HOST3`  — strix2: Gufo `serve llm` (Qwen3.8-Flash-Next-
///     UD-Q4_K_XL + MTP + vision).
pub const DEFAULT_HOST: &str = "m.tower1:11444";
pub const DEFAULT_HOST2: &str = "m.strix1:11444";
pub const DEFAULT_HOST3: &str = "m.strix2:11444";

/// One inference server the card watches.
#[derive(Debug, Clone)]
pub struct ServerCfg {
    pub host: String,
    /// Short name for the card's per-server footer; defaults to `host`.
    pub label: String,
}

/// The whole config: the (ordered) server list + poll cadence.
#[derive(Debug, Clone)]
pub struct Config {
    pub servers: Vec<ServerCfg>,
    pub poll: PollTuning,
}

impl Default for Config {
    fn default() -> Self {
        // The three current inference machines (the "three hosts" baseline):
        // row 1 = tower1 (llama.cpp), row 2 = strix1 (Gufo, the reinstalled
        // `m.strix` box), row 3 = strix2 (Gufo). Edit widget.json to change.
        Self {
            servers: vec![
                ServerCfg { host: DEFAULT_HOST.to_string(), label: "tower1".to_string() },
                ServerCfg { host: DEFAULT_HOST2.to_string(), label: "strix1".to_string() },
                ServerCfg { host: DEFAULT_HOST3.to_string(), label: "strix2".to_string() },
            ],
            poll: PollTuning::default(),
        }
    }
}

/// The poll-cadence values extracted from `buddy_state` so they're tunable
/// from the config file. `None` → built-in default.
#[derive(Debug, Default, Deserialize)]
#[serde(default, deny_unknown_fields)]
struct PollJson {
    working_ms: Option<u64>,
    idle_ms: Option<u64>,
    error_ms: Option<u64>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct ServerJson {
    host: String,
    #[serde(default)]
    label: Option<String>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct ConfigJson {
    #[serde(default)]
    servers: Vec<ServerJson>,
    #[serde(default)]
    poll: PollJson,
}

/// Resolve the config file path (env override, then per-OS defaults).
pub fn config_path() -> std::path::PathBuf {
    if let Ok(p) = std::env::var("DESKBUDDY_CONFIG") {
        return std::path::PathBuf::from(p);
    }
    if let Ok(appdata) = std::env::var("APPDATA") {
        return std::path::PathBuf::from(appdata).join("DeskBuddy").join("widget.json");
    }
    if let Ok(xd) = std::env::var("XDG_CONFIG_HOME") {
        return std::path::PathBuf::from(xd).join("deskbuddy").join("widget.json");
    }
    std::path::PathBuf::from("widget.json")
}

/// Load the config. Returns `Ok` on success or a written-defaults fallback
/// (so the caller always gets a usable config); `Err` only if the file is
/// present but malformed (defaults are used too, but the caller logs).
pub fn load() -> Config {
    let path = config_path();
    match std::fs::read_to_string(&path) {
        Ok(text) => match serde_json::from_str::<ConfigJson>(&text) {
            Ok(json) => return from_json(json),
            Err(e) => eprintln!("[buddy] ignoring malformed config {path:?}: {e}; using defaults"),
        },
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
            let c = Config::default();
            write_default(&path).ok(); // so there's a file to edit next time
            return c;
        }
        Err(_) => {
            eprintln!("[buddy] cannot read config {path:?}; using defaults");
        }
    }
    Config::default()
}

/// Clamp a poll-cadence value to the 200–10 000 ms range, or its default.
fn clamp_ms(v: Option<u64>, default: u64) -> u64 {
    v.map(|x| x.clamp(200, 10_000)).unwrap_or(default)
}

/// Build a `Config` from a parsed `ConfigJson`, validating/coercing fields.
fn from_json(json: ConfigJson) -> Config {
    let servers: Vec<ServerCfg> = json
        .servers
        .into_iter()
        .filter_map(|s| {
            if s.host.trim().is_empty() {
                return None; // skip blank hosts rather than poll ""
            }
            let label = s.label.filter(|l| !l.trim().is_empty()).unwrap_or_else(|| s.host.clone());
            Some(ServerCfg { host: s.host, label })
        })
        .collect();
    // Zero (or all-blank) servers is a broken config → fall back to default.
    let servers = if servers.is_empty() {
        Config::default().servers
    } else {
        servers
    };
    let d = PollTuning::default();
    let poll = PollTuning {
        working_ms: clamp_ms(json.poll.working_ms, d.working_ms),
        idle_ms: clamp_ms(json.poll.idle_ms, d.idle_ms),
        error_ms: clamp_ms(json.poll.error_ms, d.error_ms),
    };
    Config { servers, poll }
}

/// Write the default config to `path` (creating parent dirs). Best-effort.
fn write_default(path: &std::path::Path) -> std::io::Result<()> {
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir)?;
    }
    std::fs::write(path, DEFAULT_CONFIG_TEXT)
}

/// The default config, as the text written on first run. Keep in sync with
/// `Config::default` (three default servers + default poll cadence).
pub const DEFAULT_CONFIG_TEXT: &str = r#"{
  "servers": [
    { "host": "m.tower1:11444", "label": "tower1" },
    { "host": "m.strix1:11444", "label": "strix1" },
    { "host": "m.strix2:11444", "label": "strix2" }
  ],
  "poll": { "working_ms": 500, "idle_ms": 2000, "error_ms": 5000 }
}
"#;

#[cfg(test)]
mod tests {
    use super::*;

    fn cfg(text: &str) -> Config {
        from_json(serde_json::from_str::<ConfigJson>(text).unwrap())
    }

    #[test]
    fn two_servers_with_labels() {
        let c = cfg(
            r#"{"servers":[
                 {"host":"a.tower1:11444","label":"alpha"},
                 {"host":"b.tower1:11444"}]}"#,
        );
        assert_eq!(c.servers.len(), 2);
        assert_eq!(c.servers[0].host, "a.tower1:11444");
        assert_eq!(c.servers[0].label, "alpha");
        // label defaults to host when omitted
        assert_eq!(c.servers[1].label, "b.tower1:11444");
    }

    #[test]
    fn blank_hosts_are_skipped_and_empty_falls_back() {
        let c = cfg(r#"{"servers":[{"host":"  "}]}"#);
        assert_eq!(c.servers.len(), 3); // fell back to the 3-server default
        assert_eq!(c.servers[0].host, DEFAULT_HOST);
        assert_eq!(c.servers[1].host, DEFAULT_HOST2);
        assert_eq!(c.servers[2].host, DEFAULT_HOST3);
    }

    #[test]
    fn poll_values_clamped_and_defaulted() {
        let d = PollTuning::default();
        let c = cfg(r#"{"poll":{"working_ms":50,"idle_ms":999999}}"#);
        assert_eq!(c.poll.working_ms, 200); // clamped up from 50
        assert_eq!(c.poll.idle_ms, 10_000); // clamped down from 999999
        assert_eq!(c.poll.error_ms, d.error_ms); // omitted → default
    }

    #[test]
    fn malformed_json_is_rejected() {
        assert!(serde_json::from_str::<ConfigJson>(r#"{"servers":"nope"}"#).is_err());
        assert!(serde_json::from_str::<ConfigJson>(r#"{"bogus":1}"#).is_err()); // deny_unknown
    }

    #[test]
    fn test_config_file_has_three_default_servers() {
        // The repo test fixture the screenshot test points at — assert it's
        // what the golden images were built against.
        let p = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/test_config.json");
        let text = std::fs::read_to_string(&p).expect("tests/test_config.json");
        let c = from_json(serde_json::from_str::<ConfigJson>(&text).unwrap());
        assert_eq!(c.servers.len(), 3);
        assert_eq!(c.servers[0].host, DEFAULT_HOST);
        assert_eq!(c.servers[1].host, DEFAULT_HOST2);
        assert_eq!(c.servers[2].host, DEFAULT_HOST3);
    }

    #[test]
    fn config_path_falls_back_to_local_file() {
        // No DESKBUDY_CONFIG / APPDATA in the test → the last-resort path.
        std::env::remove_var("DESKBUDDY_CONFIG");
        // (APPDATA is unset in the test sandbox on CI; on a real host the
        // APPDATA branch is used, which is fine — we only check it's a path
        // ending in widget.json.)
        let p = config_path();
        assert!(p.file_name().unwrap().to_string_lossy() == "widget.json");
    }
}
