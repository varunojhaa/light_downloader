use reqwest::blocking::{Client, Response};
use reqwest::header::{CONTENT_LENGTH, CONTENT_RANGE, RANGE};
use serde::{Deserialize, Serialize};
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

const MAGIC: &str = "IDMR1";
const BUFFER_SIZE: usize = 64 * 1024;

#[derive(Clone, Serialize, Deserialize)]
struct Segment { first: u64, last: u64, completed: u64 }

#[derive(Serialize, Deserialize)]
struct State { magic: String, url: String, size: u64, range_supported: bool, segments: Vec<Segment> }

fn usage() { println!("IDM Rust 1.0\nUsage: idm-rust URL [OUTPUT] [--connections N] [--retries N] [--limit BYTES_PER_SECOND]"); }

fn arg_value(args: &[String], name: &str, default: u64) -> u64 {
    args.windows(2).find(|p| p[0] == name).and_then(|p| p[1].parse().ok()).unwrap_or(default)
}

fn state_path(output: &Path) -> PathBuf { PathBuf::from(format!("{}.idm", output.display())) }
fn part_path(output: &Path) -> PathBuf { PathBuf::from(format!("{}.part", output.display())) }

fn save_state(path: &Path, state: &State) -> io::Result<()> {
    let tmp = path.with_extension("idm.tmp");
    let data = serde_json::to_vec_pretty(state).map_err(io::Error::other)?;
    fs::write(&tmp, data)?;
    fs::rename(tmp, path)
}

fn response_size(response: &Response) -> Option<u64> {
    response.headers().get(CONTENT_LENGTH)?.to_str().ok()?.parse().ok()
}

fn probe(client: &Client, url: &str) -> Result<(u64, bool), Box<dyn std::error::Error>> {
    let head = client.head(url).send()?;
    let size = response_size(&head).or_else(|| head.headers().get(CONTENT_RANGE).and_then(|v| v.to_str().ok()).and_then(|v| v.rsplit('/').next()?.parse().ok()));
    if let Some(size) = size { return Ok((size, head.headers().get("accept-ranges").map(|v| v == "bytes").unwrap_or(false))); }
    let probe = client.get(url).header(RANGE, "bytes=0-0").send()?;
    let range = probe.headers().get(CONTENT_RANGE).and_then(|v| v.to_str().ok()).and_then(|v| v.split('/').nth(1)?.parse().ok());
    let supports_ranges = range.is_some();
    Ok((range.or_else(|| response_size(&probe)).ok_or("server did not provide a file size")?, supports_ranges))
}

fn fetch_segment(client: &Client, url: &str, part: &Path, segment: &mut Segment, retries: u64, limit: u64, stop: &AtomicStop) -> Result<(), String> {
    let mut attempt = 0;
    while segment.completed < segment.last - segment.first + 1 {
        if stop.load() { return Ok(()); }
        let start = segment.first + segment.completed;
        let range = format!("bytes={}-{}", start, segment.last);
        let result = client.get(url).header(RANGE, range).send().and_then(|r| r.error_for_status());
        match result {
            Ok(mut response) => {
                let mut file = OpenOptions::new().write(true).open(part).map_err(|e| e.to_string())?;
                file.seek(SeekFrom::Start(start)).map_err(|e| e.to_string())?;
                let mut buffer = [0u8; BUFFER_SIZE];
                loop {
                    if stop.load() { return Ok(()); }
                    let n = response.read(&mut buffer).map_err(|e| e.to_string())?;
                    if n == 0 { break; }
                    file.write_all(&buffer[..n]).map_err(|e| e.to_string())?;
                    segment.completed += n as u64;
                    if limit > 0 { thread::sleep(Duration::from_secs_f64(n as f64 / limit as f64)); }
                }
                return Ok(());
            }
            Err(_error) if attempt < retries => { attempt += 1; thread::sleep(Duration::from_millis(500 * attempt)); }
            Err(error) => return Err(error.to_string()),
        }
    }
    Ok(())
}

struct AtomicStop(Arc<Mutex<bool>>);
impl AtomicStop { fn load(&self) -> bool { *self.0.lock().unwrap() } }

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 || args[1] == "--help" { usage(); return Ok(()); }
    let url = args[1].clone();
    let output = PathBuf::from(args.get(2).filter(|v| !v.starts_with('-')).cloned().unwrap_or_else(|| url.rsplit('/').next().filter(|v| !v.is_empty()).unwrap_or("download.bin").to_string()));
    let connections = arg_value(&args, "--connections", 8).clamp(1, 16) as usize;
    let retries = arg_value(&args, "--retries", 3);
    let limit = arg_value(&args, "--limit", 0);
    let client = Client::builder().user_agent("IDM-Rust/1.0").build()?;
    let part = part_path(&output); let state_file = state_path(&output);
    let stop_flag = Arc::new(Mutex::new(false));
    let handler_flag = stop_flag.clone();
    ctrlc::set_handler(move || { *handler_flag.lock().unwrap() = true; }).ok();
    let stop = AtomicStop(stop_flag);
    let mut state: State = match fs::read(&state_file).ok().and_then(|d| serde_json::from_slice(&d).ok()) {
        Some(s) if s.magic == MAGIC && s.url == url => s,
        _ => { let (size, ranges) = probe(&client, &url)?; let count = if ranges { connections } else { 1 }; let chunk = (size + count as u64 - 1) / count as u64; let segments = (0..count).map(|i| { let first = i as u64 * chunk; let last = (first + chunk).min(size) - 1; Segment { first, last, completed: 0 } }).collect(); State { magic: MAGIC.into(), url: url.clone(), size, range_supported: ranges, segments } }
    };
    if state.size == 0 { fs::write(&part, [])?; fs::rename(&part, &output)?; fs::remove_file(&state_file).ok(); return Ok(()); }
    let mut file = OpenOptions::new().create(true).write(true).open(&part)?; file.set_len(state.size)?; drop(file);
    save_state(&state_file, &state)?;
    let shared = Arc::new(Mutex::new(state.segments.clone()));
    let mut handles = Vec::new();
    for index in 0..state.segments.len() {
        let client = client.clone(); let url = url.clone(); let part = part.clone(); let shared = shared.clone(); let stop_ref = AtomicStop(stop.0.clone());
        handles.push(thread::spawn(move || {
            let mut segment = { shared.lock().unwrap()[index].clone() };
            let result = fetch_segment(&client, &url, &part, &mut segment, retries, limit, &stop_ref);
            shared.lock().unwrap()[index] = segment;
            result
        }));
    }
    for handle in handles { handle.join().map_err(|_| "worker thread panicked")??; }
    state.segments = Arc::try_unwrap(shared).map_err(|_| "state still referenced")?.into_inner().map_err(|_| "state lock poisoned")?;
    save_state(&state_file, &state)?;
    if stop.load() { println!("Paused. Run the same command to resume."); return Ok(()); }
    if state.segments.iter().any(|s| s.completed < s.last - s.first + 1) { return Err("download incomplete".into()); }
    fs::rename(&part, &output)?; fs::remove_file(state_file).ok(); println!("Completed: {}", output.display()); Ok(())
}