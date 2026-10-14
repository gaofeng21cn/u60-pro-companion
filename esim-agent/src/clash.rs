//! The existing controller supplies the live, locally owned proxy endpoint.
//! This worker never starts or reconfigures Mihomo.
use std::sync::OnceLock;
static PROXY: OnceLock<Option<String>> = OnceLock::new();
pub fn set_proxy(url: Option<String>) {
    let _ = PROXY.set(url);
}
pub fn proxy_url() -> Option<&'static str> {
    PROXY.get().and_then(|v| v.as_deref())
}
pub fn core_running() -> bool {
    proxy_url().is_some()
}
