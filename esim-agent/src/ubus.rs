//! Minimal read-only ubus bridge needed to compare the modem's ICCID after an
//! eUICC profile switch. No request data is accepted from a network caller.

use serde_json::Value;
use std::{process::Command, time::Duration};

pub fn call(object: &str, method: &str, params: Option<&str>) -> Result<Value, String> {
    let mut command = Command::new("ubus");
    command.args(["-t", "5", "call", object, method]);
    if let Some(params) = params {
        command.arg(params);
    }
    let output =
        crate::process_runner::output(&mut command, None, Duration::from_secs(8), 64 * 1024)
            .map_err(|e| format!("ubus exec: {e}"))?;
    if !output.status.success() {
        return Err(format!("ubus call {object} {method} failed"));
    }
    let text = String::from_utf8_lossy(&output.stdout);
    if text.trim().is_empty() {
        return Ok(Value::Null);
    }
    serde_json::from_str(text.trim()).map_err(|e| format!("ubus JSON parse: {e}"))
}
