use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::path::Path;
use std::ptr;

use iota_stronghold::{Client, KeyProvider, SnapshotPath, Stronghold};
use zeroize::Zeroizing;

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
    let Some(value) = c_str(value) else {
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
        .insert(user.as_bytes().to_vec(), value.as_bytes().to_vec(), None)
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
    let Ok((_stronghold, client, _snapshot)) = open(&path, password) else {
        return FAILED;
    };
    let Ok(found) = client.store().get(user.as_bytes()) else {
        return FAILED;
    };
    let Some(bytes) = found else {
        return MISSING;
    };
    let Ok(text) = String::from_utf8(bytes) else {
        return FAILED;
    };
    let Ok(c) = CString::new(text) else {
        return FAILED;
    };
    let n = c.as_bytes_with_nul().len();
    let buf = unsafe { libc::malloc(n) as *mut c_char };
    if buf.is_null() {
        return FAILED;
    }
    unsafe {
        ptr::copy_nonoverlapping(c.as_ptr(), buf, n);
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
