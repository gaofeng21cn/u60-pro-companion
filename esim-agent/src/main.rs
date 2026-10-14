//! One-shot eUICC worker. Request secrets stay on stdin; readback and job
//! progress live in a root-only volatile directory, never in a system daemon.
use serde_json::{json, Value};
use std::{
    env, fs,
    io::{self, Read},
    os::fd::AsRawFd,
    os::unix::fs::OpenOptionsExt,
    time::{Duration, Instant},
};
mod clash;
mod esim;
mod lpac;
mod process_runner;
mod qmi;
mod ubus;
const STATE: &str = "/tmp/u60-esim/state.json";

fn read_stdin() -> Result<Vec<u8>, String> {
    let mut body = Vec::new();
    io::stdin()
        .take(16 * 1024 + 1)
        .read_to_end(&mut body)
        .map_err(|_| "无法读取请求")?;
    if body.len() > 16 * 1024 {
        return Err("请求过大".into());
    }
    Ok(body)
}
fn persist(value: &Value) -> Result<(), String> {
    let tmp = format!("{STATE}.{}", std::process::id());
    fs::write(&tmp, value.to_string()).map_err(|_| "无法保存 eSIM 进度")?;
    fs::rename(&tmp, STATE).map_err(|_| "无法更新 eSIM 进度".into())
}
fn run(body: &[u8]) -> Result<Value, String> {
    if let Ok(bytes) = fs::read(STATE) {
        if let Ok(previous) = serde_json::from_slice::<Value>(&bytes) {
            esim::restore_hint(&previous);
        }
    }
    let mut request: Value = serde_json::from_slice(body).map_err(|_| "无效 JSON 请求")?;
    let action = request["action"].as_str().ok_or("缺少 action")?.to_string();
    let expected = request
        .as_object_mut()
        .ok_or("请求必须是对象")?
        .remove("expected_eid")
        .and_then(|v| v.as_str().map(str::to_owned));
    let proxy = request
        .as_object_mut()
        .unwrap()
        .remove("proxy_url")
        .and_then(|v| v.as_str().map(str::to_owned));
    if proxy.as_ref().is_some_and(|s| {
        !s.starts_with("http://") || s.len() > 160 || s.contains(['\r', '\n', '"', '\\'])
    }) {
        return Err("代理地址无效".into());
    }
    clash::set_proxy(proxy);
    let confirmation = request
        .as_object_mut()
        .unwrap()
        .remove("confirm_name")
        .and_then(|v| v.as_str().map(str::to_owned));
    // The existing settings controller is also the lock owner for radio,
    // standby and power operations. Hold its lock for the entire eSIM job.
    let file = fs::OpenOptions::new()
        .create(true)
        .truncate(false)
        .read(true)
        .write(true)
        .mode(0o600)
        .custom_flags(libc::O_NOFOLLOW | libc::O_CLOEXEC)
        .open("/tmp/u60-control.lock")
        .map_err(|_| "无法取得设备设置锁")?;
    let until = Instant::now() + Duration::from_secs(5);
    loop {
        if unsafe { libc::flock(file.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) } == 0 {
            break;
        }
        if Instant::now() > until {
            return Err("另一个设备设置正在执行，请稍后重试".into());
        }
        std::thread::sleep(Duration::from_millis(50));
    }
    if fs::metadata("/tmp/u60-standby/active").is_ok() {
        return Err("正在恢复待机服务，请稍后重试".into());
    }
    let firmware = ubus::call("zwrt_zte_mdm.api", "get_zwrt_common_info", Some("{}"))?;
    if firmware["wa_inner_version"].as_str() != Some("BD_CNMU5250V1.0.0B31") {
        return Err("eSIM 当前只支持已匹配的国行 B31 固件".into());
    }
    if action != "scan" {
        let eid = expected
            .as_deref()
            .filter(|s| s.len() == 32 && s.bytes().all(|b| b.is_ascii_digit()))
            .ok_or("请先读取并确认当前 eUICC 卡")?;
        persist(
            &json!({"installed":true,"job":{"state":"running","kind":action,"step":"reading"},"view":null}),
        )?;
        let view = esim::refresh();
        if action == "delete" {
            let selected = view
                .profiles
                .iter()
                .find(|p| p.iccid == request["iccid"].as_str().unwrap_or(""));
            let name = selected.map(|p| {
                if !p.nickname.is_empty() {
                    p.nickname.as_str()
                } else if !p.name.is_empty() {
                    p.name.as_str()
                } else {
                    "未命名配置"
                }
            });
            if confirmation.as_deref() != name || name.is_none() {
                return Err("输入的名称与卡片当前配置不一致，未删除".into());
            }
        }
        if view.card != "euicc" || view.eid != eid {
            return Err("卡片已变化或无法读取，未修改；请重新读取卡片".into());
        }
    }
    if action == "delete" {
        request.as_object_mut().unwrap().remove("action");
        esim::start_delete(&serde_json::to_vec(&request).unwrap())?;
    } else {
        esim::start_action(&serde_json::to_vec(&request).unwrap())?;
    }
    loop {
        let status = esim::status();
        persist(&status)?;
        if status["job"]["state"]
            .as_str()
            .is_none_or(|s| s != "running")
        {
            return Ok(status);
        }
        std::thread::sleep(Duration::from_millis(300));
    }
}
fn main() {
    let result = (|| -> Result<Value, String> {
        match env::args().nth(1).as_deref() {
            Some("status") => Ok(esim::status()),
            Some("queued") => {
                let mut value = fs::read(STATE)
                    .ok()
                    .and_then(|b| serde_json::from_slice::<Value>(&b).ok())
                    .unwrap_or_else(esim::status);
                value["job"] = json!({"state":"running","step":"queued"});
                Ok(value)
            }
            Some("run") => run(&read_stdin()?),
            Some("version") => Ok(json!({"ok":true,"version":env!("CARGO_PKG_VERSION")})),
            _ => Err("usage: u60-esim status|run|version".into()),
        }
    })();
    match result {
        Ok(value) => println!("{}", value),
        Err(error) => {
            let mut value = esim::status();
            value["job"] = json!({"state":"failed","error":error,"step":"rejected"});
            let _ = persist(&value);
            println!("{}", value);
            std::process::exit(1);
        }
    }
}
