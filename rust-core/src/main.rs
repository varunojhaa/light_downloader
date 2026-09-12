use reqwest::blocking::{Client, Response};
use reqwest::header::{CONTENT_LENGTH, CONTENT_RANGE, RANGE};
use serde::{Deserialize, Serialize};
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

const MAGIC: &str = "LDR2";
const BUFFER_SIZE: usize = 64 * 1024;
const MAX_CONNECTIONS: usize = 16;

#[derive(Clone, Serialize, Deserialize)]
struct Segment { first: u64, last: u64, completed: u64 }

#[derive(Clone, Serialize, Deserialize)]
struct State { magic: String, url: String, size: u64, range_supported: bool, segments: Vec<Segment> }

struct Stop(Arc<Mutex<bool>>);
impl Stop { fn load(&self) -> bool { *self.0.lock().unwrap_or_else(|e| e.into_inner()) } }

fn usage() { println!("Light Downloader CLI\nUsage: light-downloader URL [OUTPUT] [--connections N] [--retries N] [--limit BYTES_PER_SECOND]"); }
fn arg_value(args: &[String], name: &str, default: u64) -> u64 { args.windows(2).find(|p| p[0] == name).and_then(|p| p[1].parse().ok()).unwrap_or(default) }
fn state_path(output: &Path) -> PathBuf { PathBuf::from(format!("{}.ld", output.display())) }
fn part_path(output: &Path) -> PathBuf { PathBuf::from(format!("{}.part", output.display())) }

fn replace_file(from: &Path, to: &Path) -> io::Result<()> {
    if to.exists() { fs::remove_file(to)?; }
    fs::rename(from, to)
}

fn save_state(path: &Path, state: &State) -> io::Result<()> {
    let tmp = PathBuf::from(format!("{}.tmp", path.display()));
    let data = serde_json::to_vec_pretty(state).map_err(io::Error::other)?;
    let mut file = File::create(&tmp)?;
    file.write_all(&data)?;
    file.sync_all()?;
    replace_file(&tmp, path)
}

fn response_size(response: &Response) -> Option<u64> { response.headers().get(CONTENT_LENGTH)?.to_str().ok()?.parse().ok() }
fn total_from_range(response: &Response) -> Option<u64> { response.headers().get(CONTENT_RANGE)?.to_str().ok()?.rsplit('/').next()?.parse().ok() }

fn probe(client: &Client, url: &str) -> Result<(u64, bool), Box<dyn std::error::Error>> {
    let head = client.head(url).send()?.error_for_status()?;
    if let Some(size) = response_size(&head).or_else(|| total_from_range(&head)) {
        let ranges = head.headers().get("accept-ranges").and_then(|v| v.to_str().ok()).map(|v| v.eq_ignore_ascii_case("bytes")).unwrap_or(false);
        return Ok((size, ranges));
    }
    let response = client.get(url).header(RANGE, "bytes=0-0").send()?.error_for_status()?;
    let size = total_from_range(&response).or_else(|| response_size(&response))
        .ok_or_else(|| io::Error::other("server did not provide a file size"))?;
    Ok((size, response.status().as_u16() == 206))
}

fn fetch_segment(client: &Client, url: &str, part: &Path, segment: &mut Segment, ranges: bool, retries: u64, limit: u64, stop: &Stop) -> Result<(), String> {
    let length = segment.last - segment.first + 1;
    let mut attempt = 0;
    while segment.completed < length {
        if stop.load() { return Ok(()); }
        let start = segment.first + segment.completed;
        let range = format!("bytes={}-{}", start, segment.last);
        let request = if ranges { client.get(url).header(RANGE, range) } else { client.get(url) };
        match request.send() {
            Ok(mut response) if (ranges && response.status().as_u16() == 206) || (!ranges && response.status().is_success()) => {
                let mut file = OpenOptions::new().write(true).open(part).map_err(|e| e.to_string())?;
                file.seek(SeekFrom::Start(start)).map_err(|e| e.to_string())?;
                let mut buffer = [0u8; BUFFER_SIZE];
                let began = Instant::now(); let mut bytes = 0u64;
                loop {
                    if stop.load() { return Ok(()); }
                    let n = response.read(&mut buffer).map_err(|e| e.to_string())?;
                    if n == 0 { break; }
                    file.write_all(&buffer[..n]).map_err(|e| e.to_string())?;
                    segment.completed += n as u64; bytes += n as u64;
                    if limit > 0 { let wanted = Duration::from_secs_f64(bytes as f64 / limit as f64); if let Some(wait) = wanted.checked_sub(began.elapsed()) { thread::sleep(wait); } }
                }
                if segment.completed == length { return Ok(()); }
                attempt += 1;
            }
            Ok(_) if attempt < retries => { attempt += 1; }
            Ok(_) => return Err(if ranges { "server ignored byte range request" } else { "server returned an unsuccessful response" }.into()),
            Err(error) if attempt < retries => { attempt += 1; eprintln!("retry {}: {}", attempt, error); }
            Err(error) => return Err(error.to_string()),
        }
        thread::sleep(Duration::from_millis(250 * attempt.min(8)));
    }
    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 || args[1] == "--help" { usage(); return Ok(()); }
    let url = args[1].clone();
    let output = PathBuf::from(args.get(2).filter(|v| !v.starts_with('-')).cloned().unwrap_or_else(|| url.rsplit('/').next().filter(|v| !v.is_empty()).unwrap_or("download.bin").to_string()));
    let connections = (arg_value(&args, "--connections", 8) as usize).clamp(1, MAX_CONNECTIONS);
    let retries = arg_value(&args, "--retries", 3); let limit = arg_value(&args, "--limit", 0);
    let client = Client::builder().user_agent("LightDownloader/1.0").build()?;
    let part = part_path(&output); let state_file = state_path(&output);
    let flag = Arc::new(Mutex::new(false)); let handler_flag = flag.clone();
    ctrlc::set_handler(move || { *handler_flag.lock().unwrap_or_else(|e| e.into_inner()) = true; }).ok();
    let stop = Stop(flag);
    let mut state: State = match fs::read(&state_file).ok().and_then(|d| serde_json::from_slice::<State>(&d).ok()) {
        Some(s) if s.magic == MAGIC && s.url == url && s.segments.iter().all(|x| x.first <= x.last && x.completed <= x.last - x.first + 1) => s,
        _ => { let (size, ranges) = probe(&client, &url)?; let count = if ranges && size > 0 { connections.min(size as usize) } else { 1 }; let chunk = size.div_ceil(count as u64); let segments = (0..count).filter_map(|i| { let first = i as u64 * chunk; (first < size).then(|| Segment { first, last: (first + chunk).min(size) - 1, completed: 0 }) }).collect(); State { magic: MAGIC.into(), url: url.clone(), size, range_supported: ranges, segments } }
    };
    if state.size == 0 { File::create(&part)?; replace_file(&part, &output)?; fs::remove_file(&state_file).ok(); return Ok(()); }
    if !state.range_supported {
        state.segments = vec![Segment { first: 0, last: state.size - 1, completed: 0 }];
        if part.exists() { fs::remove_file(&part)?; }
    }
    let existing = fs::metadata(&part).map(|m| m.len()).unwrap_or(0);
    if existing != state.size { let file = OpenOptions::new().create(true).write(true).truncate(false).open(&part)?; file.set_len(state.size)?; }
    save_state(&state_file, &state)?;
    let shared = Arc::new(Mutex::new(state.segments.clone())); let mut handles = Vec::new();
    for index in 0..state.segments.len() {
        let (client, url, part, shared, stop_ref) = (client.clone(), url.clone(), part.clone(), shared.clone(), Stop(stop.0.clone()));
        let ranges = state.range_supported;
        handles.push(thread::spawn(move || { let mut segment = shared.lock().unwrap_or_else(|e| e.into_inner())[index].clone(); let result = fetch_segment(&client, &url, &part, &mut segment, ranges, retries, limit, &stop_ref); shared.lock().unwrap_or_else(|e| e.into_inner())[index] = segment; result }));
    }
    let mut error = None;
    for handle in handles {
        match handle.join() {
            Ok(Ok(())) => {}
            Ok(Err(e)) => error = Some(e),
            Err(_) => error = Some("worker thread panicked".into()),
        }
    }
    state.segments = shared.lock().unwrap_or_else(|e| e.into_inner()).clone(); save_state(&state_file, &state)?;
    if let Some(e) = error { return Err(io::Error::other(e).into()); }
    if stop.load() { println!("Paused. Run the same command to resume."); return Ok(()); }
    if state.segments.iter().any(|s| s.completed < s.last - s.first + 1) {
        return Err(io::Error::other("download incomplete").into());
    }
    replace_file(&part, &output)?; fs::remove_file(state_file).ok(); println!("Completed: {}", output.display()); Ok(())
}