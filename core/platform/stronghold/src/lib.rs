use std::ffi::CStr;
#[cfg(test)]
use std::ffi::CString;
use std::os::raw::{c_char, c_int};
use std::path::Path;
use std::ptr;

use iota_stronghold::{Client, KeyProvider, SnapshotPath, Stronghold};
use std::sync::atomic::{AtomicBool, Ordering};
use zeroize::Zeroizing;

/* K Lock (2026-10-19): the key is 32 random bytes from the keyring, used as
   is. age's scrypt stretching (default work factor 19, about 1 s per open)
   protects weak passwords only, so snapshots are written at work factor 0.
   age reads the factor from each file's header, so older snapshots still open
   with the same key; a read that finds one above 0 saves it again at 0. */
static RESAVED: AtomicBool = AtomicBool::new(false);

/* Set before every commit (an atomic store), so nothing else can leave the
   library writing at the slow default. */
fn fast_writes() {
    let _ = iota_stronghold::engine::snapshot::try_set_encrypt_work_factor(0);
}

/* The scrypt work factor in a snapshot's age header ("-> scrypt <salt> <n>"),
   or None when the file cannot be read or has no such stanza. */
fn header_work_factor(path: &str) -> Option<u8> {
    use std::io::Read;
    let mut head = vec![0u8; 512];
    let mut f = std::fs::File::open(path).ok()?;
    let n = f.read(&mut head).ok()?;
    head.truncate(n);
    let tag = b"-> scrypt ";
    let at = head.windows(tag.len()).position(|w| w == tag)? + tag.len();
    let line_end = head[at..].iter().position(|&b| b == b'\n')? + at;
    let line = std::str::from_utf8(&head[at..line_end]).ok()?;
    line.split(' ').nth(1)?.trim().parse::<u8>().ok()
}

/* 1 once after a read re-saved an old snapshot at work factor 0, then 0. The
   core logs it (the platform layer does not log). */
#[no_mangle]
pub extern "C" fn pav_stronghold_take_resaved() -> c_int {
    RESAVED.swap(false, Ordering::Relaxed) as c_int
}

const CLIENT: &[u8] = b"pavillion";
const OK: c_int = 0;
const MISSING: c_int = 1;
const FAILED: c_int = 3;

fn c_str(p: *const c_char) -> Option<String> {
    if p.is_null() {
        return None;
    }
    unsafe { CStr::from_ptr(p) }.to_str().ok().map(|s| s.to_string())
}

/* A C string that may be a secret: wiped when dropped (K Lock). */
fn c_secret(p: *const c_char) -> Option<Zeroizing<Vec<u8>>> {
    if p.is_null() {
        return None;
    }
    Some(Zeroizing::new(unsafe { CStr::from_ptr(p) }.to_bytes().to_vec()))
}

fn key_provider(password: &[u8]) -> Result<KeyProvider, ()> {
    let raw = Zeroizing::new(password.to_vec());
    KeyProvider::try_from(raw).map_err(|_| ())
}

fn open(path: &str, password: &[u8]) -> Result<(Stronghold, Client, SnapshotPath), ()> {
    let stronghold = Stronghold::default();
    let key = key_provider(password)?;
    let snapshot = SnapshotPath::from_path(path);
    if Path::new(path).is_file() {
        stronghold
            .load_snapshot(&key, &snapshot)
            .map_err(|_| ())?;
        let client = match stronghold.load_client(CLIENT) {
            Ok(client) => client,
            Err(_) => stronghold.create_client(CLIENT).map_err(|_| ())?,
        };
        Ok((stronghold, client, snapshot))
    } else {
        let client = stronghold.create_client(CLIENT).map_err(|_| ())?;
        Ok((stronghold, client, snapshot))
    }
}

fn commit(stronghold: &Stronghold, snapshot: &SnapshotPath, password: &[u8]) -> Result<(), ()> {
    fast_writes();
    let key = key_provider(password)?;
    stronghold
        .commit_with_keyprovider(snapshot, &key)
        .map_err(|_| ())
}

fn label_key(user: &str) -> Vec<u8> {
    let mut key = user.as_bytes().to_vec();
    key.extend_from_slice(b"/label");
    key
}

#[no_mangle]
pub extern "C" fn pav_stronghold_set(
    path: *const c_char,
    password: *const u8,
    password_len: usize,
    user: *const c_char,
    label: *const c_char,
    value: *const c_char,
) -> c_int {
    let Some(path) = c_str(path) else {
        return FAILED;
    };
    let Some(user) = c_str(user) else {
        return FAILED;
    };
    let Some(label) = c_str(label) else {
        return FAILED;
    };
    let Some(value) = c_secret(value) else {
        return FAILED;
    };
    if password.is_null() || password_len == 0 {
        return FAILED;
    }
    let password = unsafe { std::slice::from_raw_parts(password, password_len) };
    let Ok((stronghold, client, snapshot)) = open(&path, password) else {
        return FAILED;
    };
    let store = client.store();
    if store
        .insert(user.as_bytes().to_vec(), value.to_vec(), None)
        .is_err()
    {
        return FAILED;
    }
    if store
        .insert(label_key(&user), label.as_bytes().to_vec(), None)
        .is_err()
    {
        return FAILED;
    }
    if commit(&stronghold, &snapshot, password).is_err() {
        return FAILED;
    }
    OK
}

#[no_mangle]
pub extern "C" fn pav_stronghold_get(
    path: *const c_char,
    password: *const u8,
    password_len: usize,
    user: *const c_char,
    out: *mut *mut c_char,
) -> c_int {
    if out.is_null() {
        return FAILED;
    }
    unsafe { *out = ptr::null_mut() };
    let Some(path) = c_str(path) else {
        return FAILED;
    };
    let Some(user) = c_str(user) else {
        return FAILED;
    };
    if password.is_null() || password_len == 0 {
        return FAILED;
    }
    if !Path::new(&path).is_file() {
        return MISSING;
    }
    let password = unsafe { std::slice::from_raw_parts(password, password_len) };
    let Ok((stronghold, client, snapshot)) = open(&path, password) else {
        return FAILED;
    };
    if header_work_factor(&path).is_some_and(|wf| wf > 0) && commit(&stronghold, &snapshot, password).is_ok() {
        RESAVED.store(true, Ordering::Relaxed);
    }
    let Ok(found) = client.store().get(user.as_bytes()) else {
        return FAILED;
    };
    let Some(bytes) = found else {
        return MISSING;
    };
    let bytes = Zeroizing::new(bytes);
    if std::str::from_utf8(&bytes).is_err() || bytes.contains(&0) {
        return FAILED;
    }
    /* Copied once into C memory; the caller frees it with pav_secret_free,
       which wipes it. No Rust copy outlives this call. */
    let n = bytes.len() + 1;
    let buf = unsafe { libc::malloc(n) as *mut c_char };
    if buf.is_null() {
        return FAILED;
    }
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr() as *const c_char, buf, bytes.len());
        *buf.add(bytes.len()) = 0;
        *out = buf;
    }
    OK
}

#[no_mangle]
pub extern "C" fn pav_stronghold_delete(
    path: *const c_char,
    password: *const u8,
    password_len: usize,
    user: *const c_char,
) -> c_int {
    let Some(path) = c_str(path) else {
        return FAILED;
    };
    let Some(user) = c_str(user) else {
        return FAILED;
    };
    if password.is_null() || password_len == 0 {
        return FAILED;
    }
    if !Path::new(&path).is_file() {
        return MISSING;
    }
    let password = unsafe { std::slice::from_raw_parts(password, password_len) };
    let Ok((stronghold, client, snapshot)) = open(&path, password) else {
        return FAILED;
    };
    let store = client.store();
    let deleted = store.delete(user.as_bytes());
    let _ = store.delete(&label_key(&user));
    if deleted.is_err() {
        return FAILED;
    }
    if deleted.ok().flatten().is_none() {
        return MISSING;
    }
    if commit(&stronghold, &snapshot, password).is_err() {
        return FAILED;
    }
    OK
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Instant;

    fn tmp_path(name: &str) -> String {
        let dir = std::env::temp_dir().join(format!("pav-sh-{}-{}", std::process::id(), name));
        let _ = std::fs::create_dir_all(&dir);
        let path = dir.join("secrets.stronghold");
        let _ = std::fs::remove_file(&path);
        path.to_string_lossy().into_owned()
    }

    fn get(path: &str, key: &[u8; 32], user: &str) -> (c_int, Option<String>) {
        let p = CString::new(path).unwrap();
        let u = CString::new(user).unwrap();
        let mut out: *mut c_char = ptr::null_mut();
        let rc = pav_stronghold_get(p.as_ptr(), key.as_ptr(), key.len(), u.as_ptr(), &mut out);
        let text = if out.is_null() {
            None
        } else {
            let s = unsafe { CStr::from_ptr(out) }.to_string_lossy().into_owned();
            unsafe { libc::free(out.cast()) };
            Some(s)
        };
        (rc, text)
    }

    /* K Lock migration: a snapshot written at the old default work factor (19)
       opens with the same key, is re-saved at 0 once, and reads fast after. */
    #[test]
    fn old_snapshot_opens_and_is_resaved_fast() {
        let path = tmp_path("migrate");
        let key = [7u8; 32];
        {
            iota_stronghold::engine::snapshot::try_set_encrypt_work_factor(19).unwrap();
            let (stronghold, client, snapshot) = open(&path, &key).unwrap();
            client.store().insert(b"p/a".to_vec(), b"hunter2".to_vec(), None).unwrap();
            stronghold.commit_with_keyprovider(&snapshot, &key_provider(&key).unwrap()).unwrap();
        }
        assert_eq!(header_work_factor(&path), Some(19));
        let _ = pav_stronghold_take_resaved();

        let slow = Instant::now();
        assert_eq!(get(&path, &key, "p/a"), (OK, Some("hunter2".to_string())));
        let slow = slow.elapsed();
        assert_eq!(pav_stronghold_take_resaved(), 1);
        assert_eq!(header_work_factor(&path), Some(0));

        let fast = Instant::now();
        assert_eq!(get(&path, &key, "p/a"), (OK, Some("hunter2".to_string())));
        let fast = fast.elapsed();
        assert_eq!(pav_stronghold_take_resaved(), 0);
        eprintln!("stronghold get: old snapshot {:?}, after re-save {:?}", slow, fast);
        assert!(fast < slow);

        let wrong = [8u8; 32];
        assert_eq!(get(&path, &wrong, "p/a").0, FAILED);
    }

    #[test]
    fn new_snapshots_are_written_at_work_factor_zero() {
        let path = tmp_path("new");
        let key = [9u8; 32];
        let p = CString::new(path.as_str()).unwrap();
        let u = CString::new("p/b").unwrap();
        let l = CString::new("label").unwrap();
        let v = CString::new("pw").unwrap();
        assert_eq!(pav_stronghold_set(p.as_ptr(), key.as_ptr(), key.len(), u.as_ptr(), l.as_ptr(), v.as_ptr()), OK);
        assert_eq!(header_work_factor(&path), Some(0));
        assert_eq!(get(&path, &key, "p/b"), (OK, Some("pw".to_string())));
    }
}
