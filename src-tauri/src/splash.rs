//! Start splash drawn natively (no webview, no CSS) inside the main window. It is black at
//! once, the mark fades in, and the cover fades out when the page emits
//! `pav-app-ready` (D-UI.md Native splash). Linux draws it with GTK, which
//! Tauri already links. Windows (layered child HWND) and macOS (NSView overlay) share the
//! timing driver below; other targets compile to no-ops.

/// Mark width in logical px. The bitmap is the 1024 px source scaled
/// (bilinear) to this times the monitor scale. Change here to resize.
pub const MARK_LOGICAL_PX: f64 = 128.0;
/// Solid black after the splash opens, before the mark starts to fade in.
pub const BLACK_HOLD_MS: u64 = 400;
/// Fade-in of the mark and fade-out of the window.
pub const FADE_MS: u64 = 400;
/// Least time the mark stays fully shown before the fade-out may start.
/// The fade-out also waits for `pav-app-ready` (or the 5 s fallback).
pub const MIN_SHOW_MS: u64 = 2000;

#[cfg(target_os = "linux")]
mod imp {
    use super::{BLACK_HOLD_MS, FADE_MS, MARK_LOGICAL_PX, MIN_SHOW_MS};
    use gtk::gdk::prelude::GdkContextExt;
    use gtk::prelude::*;
    use gtk::{gdk_pixbuf, glib};
    use std::cell::{Cell, RefCell};
    use std::rc::Rc;
    use std::time::{Duration, Instant};

    static MARK: &[u8] = include_bytes!("../icons/source.png");
    thread_local! {
        static SPLASH: RefCell<Option<gtk::DrawingArea>> = const { RefCell::new(None) };
        static SHOWN_AT: Cell<Option<Instant>> = const { Cell::new(None) };
    }

    fn mark(px: i32) -> Option<gdk_pixbuf::Pixbuf> {
        let loader = gdk_pixbuf::PixbufLoader::new();
        loader.write(MARK).ok()?;
        loader.close().ok()?;
        loader.pixbuf()?.scale_simple(px, px, gdk_pixbuf::InterpType::Bilinear)
    }

    fn fade(from: f64, to: f64, step: impl Fn(f64) + 'static, done: impl FnOnce() + 'static) {
        let start = Instant::now();
        let mut done = Some(done);
        step(from);
        glib::timeout_add_local(Duration::from_millis(16), move || {
            let t = (start.elapsed().as_millis() as f64 / FADE_MS as f64).min(1.0);
            step(from + (to - from) * t);
            if t < 1.0 { return glib::ControlFlow::Continue; }
            if let Some(done) = done.take() { done(); }
            glib::ControlFlow::Break
        });
    }

    /// Main thread only (Tauri `setup`). Draws inside the main window: the
    /// webview is moved into a `gtk::Overlay` in the same window and a black
    /// drawing area sits over it. The window is shown now, black; the page
    /// keeps loading underneath.
    pub fn show(main: &tauri::WebviewWindow) {
        let Ok(vbox) = main.default_vbox() else { return };
        let children = vbox.children();
        let [web] = children.as_slice() else { return };
        let scale = main.scale_factor().ok().filter(|s| *s > 0.0).unwrap_or(1.0);
        let px = (MARK_LOGICAL_PX * scale).round().max(16.0) as i32;
        let pixbuf = mark(px);
        let alpha = Rc::new(Cell::new(0.0));
        let area = gtk::DrawingArea::new();
        let seen = alpha.clone();
        area.connect_draw(move |area, cr| {
            cr.set_source_rgb(0.0, 0.0, 0.0);
            let _ = cr.paint();
            if let Some(pb) = &pixbuf {
                let gs = area.scale_factor().max(1) as f64;
                let (aw, ah) = (area.allocated_width() as f64 * gs, area.allocated_height() as f64 * gs);
                let _ = cr.save();
                cr.scale(1.0 / gs, 1.0 / gs);
                cr.set_source_pixbuf(pb, ((aw - px as f64) / 2.0).round(), ((ah - px as f64) / 2.0).round());
                let _ = cr.paint_with_alpha(seen.get());
                let _ = cr.restore();
            }
            glib::Propagation::Stop
        });
        let overlay = gtk::Overlay::new();
        let web = web.clone();
        vbox.remove(&web);
        overlay.add(&web);
        overlay.add_overlay(&area);
        vbox.pack_start(&overlay, true, true, 0);
        overlay.show_all();
        let _ = main.show();
        let redraw = area.clone();
        SHOWN_AT.with(|t| t.set(Some(Instant::now())));
        glib::timeout_add_local_once(Duration::from_millis(BLACK_HOLD_MS), move || {
            fade(0.0, 1.0, move |a| { alpha.set(a); redraw.queue_draw(); }, || {});
        });
        SPLASH.with(|s| *s.borrow_mut() = Some(area));
    }

    /// Any thread. Fades the cover out and removes it. Safe to call twice.
    pub fn close(app: &tauri::AppHandle) {
        let _ = app.run_on_main_thread(|| {
            let Some(area) = SPLASH.with(|s| s.borrow_mut().take()) else { return };
            let envelope = Duration::from_millis(BLACK_HOLD_MS + FADE_MS + MIN_SHOW_MS);
            let waited = SHOWN_AT.with(|t| t.get()).map(|t| t.elapsed()).unwrap_or(envelope);
            glib::timeout_add_local_once(envelope.saturating_sub(waited), move || {
                let fading = area.clone();
                fade(1.0, 0.0, move |a| fading.set_opacity(a), move || {
                    if let Some(parent) = area.parent().and_then(|p| p.downcast::<gtk::Container>().ok()) { parent.remove(&area); }
                });
            });
        });
    }
}

/// Windows and macOS share the timing here. A worker thread sleeps through
/// the envelope and posts each fade step to the main thread, where `native`
/// keeps the views (they are not `Send`).
/// The mark source (1024 px RGBA PNG).
#[cfg(any(windows, target_os = "macos"))]
const MARK: &[u8] = include_bytes!("../icons/source.png");

#[cfg(any(windows, target_os = "macos"))]
mod imp {
    use super::{native, BLACK_HOLD_MS, FADE_MS, MIN_SHOW_MS};
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Mutex;
    use std::thread;
    use std::time::{Duration, Instant};
    use tauri::Manager;

    static SHOWN_AT: Mutex<Option<Instant>> = Mutex::new(None);
    static CLOSING: AtomicBool = AtomicBool::new(false);

    /// Steps 0..=1 over FADE_MS at about 60 Hz, posting `apply(t)`.
    fn fade(app: &tauri::AppHandle, apply: fn(f64)) {
        let steps = (FADE_MS / 16).max(1);
        for i in 1..=steps {
            thread::sleep(Duration::from_millis(16));
            let t = i as f64 / steps as f64;
            let _ = app.run_on_main_thread(move || apply(t));
        }
    }

    pub fn show(main: &tauri::WebviewWindow) {
        if !native::create(main) {
            return;
        }
        let _ = main.show();
        *SHOWN_AT.lock().unwrap() = Some(Instant::now());
        let app = main.app_handle().clone();
        thread::spawn(move || {
            thread::sleep(Duration::from_millis(BLACK_HOLD_MS));
            fade(&app, native::set_mark);
        });
    }

    pub fn close(app: &tauri::AppHandle) {
        let Some(shown) = *SHOWN_AT.lock().unwrap() else { return };
        if CLOSING.swap(true, Ordering::SeqCst) {
            return;
        }
        let app = app.clone();
        thread::spawn(move || {
            let envelope = Duration::from_millis(BLACK_HOLD_MS + FADE_MS + MIN_SHOW_MS);
            thread::sleep(envelope.saturating_sub(shown.elapsed()));
            fade(&app, |t| native::set_cover(1.0 - t));
            let _ = app.run_on_main_thread(native::destroy);
        });
    }
}

/// Windows: a layered child HWND on top of the WebView2 child, sized to the
/// client area. Each frame is a premultiplied BGRA DIB (black plus the mark
/// at its alpha) pushed with UpdateLayeredWindow; the cover fade uses the
/// blend's constant alpha. Layered child windows need Windows 8+.
#[cfg(windows)]
mod native {
    use super::MARK_LOGICAL_PX;
    use std::cell::{Cell, RefCell};
    use std::ptr::{null, null_mut};
    use windows_sys::Win32::Foundation::{HWND, POINT, RECT, SIZE};
    use windows_sys::Win32::Graphics::Gdi::*;
    use windows_sys::Win32::System::LibraryLoader::GetModuleHandleW;
    use windows_sys::Win32::UI::WindowsAndMessaging::*;

    thread_local! {
        static HWND_: Cell<HWND> = const { Cell::new(null_mut()) };
        /// Scaled mark, straight RGBA, and its side in px.
        static MARK: RefCell<(Vec<u8>, usize)> = const { RefCell::new((Vec::new(), 0)) };
        static ALPHA: Cell<(f64, f64)> = const { Cell::new((0.0, 1.0)) };
    }

    /// Decodes the 1024 px RGBA source and scales it bilinearly to `px`.
    fn mark(px: usize) -> Option<Vec<u8>> {
        let decoder = png::Decoder::new(std::io::Cursor::new(super::MARK));
        let mut reader = decoder.read_info().ok()?;
        let mut src = vec![0; reader.output_buffer_size()?];
        let info = reader.next_frame(&mut src).ok()?;
        if info.color_type != png::ColorType::Rgba || info.bit_depth != png::BitDepth::Eight {
            return None;
        }
        let (sw, sh) = (info.width as usize, info.height as usize);
        let mut out = vec![0u8; px * px * 4];
        for y in 0..px {
            let fy = ((y as f64 + 0.5) * sh as f64 / px as f64 - 0.5).clamp(0.0, (sh - 1) as f64);
            let (y0, wy) = (fy as usize, fy.fract());
            let y1 = (y0 + 1).min(sh - 1);
            for x in 0..px {
                let fx = ((x as f64 + 0.5) * sw as f64 / px as f64 - 0.5).clamp(0.0, (sw - 1) as f64);
                let (x0, wx) = (fx as usize, fx.fract());
                let x1 = (x0 + 1).min(sw - 1);
                for c in 0..4 {
                    let p = |xx: usize, yy: usize| src[(yy * sw + xx) * 4 + c] as f64;
                    let top = p(x0, y0) * (1.0 - wx) + p(x1, y0) * wx;
                    let bot = p(x0, y1) * (1.0 - wx) + p(x1, y1) * wx;
                    out[(y * px + x) * 4 + c] = (top * (1.0 - wy) + bot * wy).round() as u8;
                }
            }
        }
        Some(out)
    }

    /// Repaints the layered window: black, mark centred at `mark_a`, whole
    /// window at `cover_a`. Re-reads the parent client rect so resizes follow.
    fn paint() {
        let hwnd = HWND_.with(|h| h.get());
        if hwnd.is_null() {
            return;
        }
        let (mark_a, cover_a) = ALPHA.with(|a| a.get());
        unsafe {
            let mut rc: RECT = std::mem::zeroed();
            GetClientRect(GetParent(hwnd), &mut rc);
            let (w, h) = ((rc.right - rc.left).max(1), (rc.bottom - rc.top).max(1));
            let screen = GetDC(null_mut());
            let mem = CreateCompatibleDC(screen);
            let mut bmi: BITMAPINFO = std::mem::zeroed();
            bmi.bmiHeader.biSize = std::mem::size_of::<BITMAPINFOHEADER>() as u32;
            bmi.bmiHeader.biWidth = w;
            bmi.bmiHeader.biHeight = -h;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB as u32;
            let mut bits: *mut core::ffi::c_void = null_mut();
            let dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &mut bits, null_mut(), 0);
            if !dib.is_null() && !bits.is_null() {
                let (w, h) = (w as usize, h as usize);
                let buf = std::slice::from_raw_parts_mut(bits as *mut u8, w * h * 4);
                // Opaque black (premultiplied BGRA), then the mark over it.
                for px in buf.chunks_exact_mut(4) {
                    px.copy_from_slice(&[0, 0, 0, 255]);
                }
                MARK.with(|m| {
                    let (rgba, side) = &*m.borrow();
                    let ox = (w as isize - *side as isize) / 2;
                    let oy = (h as isize - *side as isize) / 2;
                    for y in 0..*side {
                        let dy = oy + y as isize;
                        if dy < 0 || dy >= h as isize { continue; }
                        for x in 0..*side {
                            let dx = ox + x as isize;
                            if dx < 0 || dx >= w as isize { continue; }
                            let s = &rgba[(y * side + x) * 4..][..4];
                            let a = s[3] as f64 / 255.0 * mark_a;
                            let d = &mut buf[(dy as usize * w + dx as usize) * 4..][..4];
                            d[0] = (s[2] as f64 * a).round() as u8;
                            d[1] = (s[1] as f64 * a).round() as u8;
                            d[2] = (s[0] as f64 * a).round() as u8;
                        }
                    }
                });
                let old = SelectObject(mem, dib);
                let size = SIZE { cx: w as i32, cy: h as i32 };
                let origin = POINT { x: 0, y: 0 };
                let blend = BLENDFUNCTION {
                    BlendOp: AC_SRC_OVER as u8,
                    BlendFlags: 0,
                    SourceConstantAlpha: (cover_a.clamp(0.0, 1.0) * 255.0).round() as u8,
                    AlphaFormat: AC_SRC_ALPHA as u8,
                };
                SetWindowPos(hwnd, HWND_TOP, 0, 0, w as i32, h as i32, SWP_NOACTIVATE);
                UpdateLayeredWindow(hwnd, screen, null(), &size, mem, &origin, 0, &blend, ULW_ALPHA);
                SelectObject(mem, old);
                DeleteObject(dib);
            }
            DeleteDC(mem);
            ReleaseDC(null_mut(), screen);
        }
    }

    pub fn create(main: &tauri::WebviewWindow) -> bool {
        let Ok(parent) = main.hwnd() else { return false };
        let px = (MARK_LOGICAL_PX * main.scale_factor().unwrap_or(1.0)).round() as usize;
        let Some(rgba) = mark(px) else { return false };
        MARK.with(|m| *m.borrow_mut() = (rgba, px));
        let class: Vec<u16> = "PavillionSplash\0".encode_utf16().collect();
        unsafe {
            let inst = GetModuleHandleW(null());
            let wc = WNDCLASSW {
                lpfnWndProc: Some(DefWindowProcW),
                hInstance: inst,
                lpszClassName: class.as_ptr(),
                hCursor: LoadCursorW(null_mut(), IDC_ARROW),
                ..std::mem::zeroed()
            };
            RegisterClassW(&wc); // A repeat registration fails harmlessly.
            let hwnd = CreateWindowExW(
                WS_EX_LAYERED | WS_EX_NOACTIVATE,
                class.as_ptr(),
                null(),
                WS_CHILD | WS_VISIBLE,
                0, 0, 1, 1,
                parent.0 as HWND,
                null_mut(),
                inst,
                null(),
            );
            if hwnd.is_null() {
                return false;
            }
            HWND_.with(|h| h.set(hwnd));
        }
        ALPHA.with(|a| a.set((0.0, 1.0)));
        paint();
        true
    }

    pub fn set_mark(a: f64) {
        ALPHA.with(|v| v.set((a, v.get().1)));
        paint();
    }

    pub fn set_cover(a: f64) {
        ALPHA.with(|v| v.set((v.get().0, a)));
        paint();
    }

    pub fn destroy() {
        let hwnd = HWND_.with(|h| h.replace(null_mut()));
        if !hwnd.is_null() {
            unsafe { DestroyWindow(hwnd) };
        }
        MARK.with(|m| *m.borrow_mut() = (Vec::new(), 0));
    }
}

/// macOS: an opaque black NSBox added above the WKWebView in the window's
/// content view (autoresizing with it), holding a centred NSImageView of the
/// PNG at MARK_LOGICAL_PX points. Fades set `alphaValue` on layer-backed views.
#[cfg(target_os = "macos")]
mod native {
    use super::MARK_LOGICAL_PX;
    use objc2::rc::Retained;
    use objc2::{AllocAnyThread, MainThreadMarker, MainThreadOnly};
    use objc2_app_kit::{
        NSAutoresizingMaskOptions, NSBox, NSBoxType, NSColor, NSImage, NSImageScaling,
        NSImageView, NSTitlePosition, NSWindow, NSWindowOrderingMode,
    };
    use objc2_foundation::{NSData, NSPoint, NSRect, NSSize};
    use std::cell::RefCell;

    thread_local! {
        static VIEWS: RefCell<Option<(Retained<NSBox>, Retained<NSImageView>)>> = const { RefCell::new(None) };
    }

    pub fn create(main: &tauri::WebviewWindow) -> bool {
        let Some(mtm) = MainThreadMarker::new() else { return false };
        let Ok(ptr) = main.ns_window() else { return false };
        let window: &NSWindow = unsafe { &*(ptr as *const NSWindow) };
        let Some(content) = window.contentView() else { return false };
        let data = NSData::with_bytes(super::MARK);
        let Some(image) = NSImage::initWithData(NSImage::alloc(), &data) else { return false };
        let side = MARK_LOGICAL_PX;
        image.setSize(NSSize::new(side, side));
        let bounds = content.bounds();
        let cover = NSBox::initWithFrame(NSBox::alloc(mtm), bounds);
        cover.setBoxType(NSBoxType::Custom);
        cover.setTitlePosition(NSTitlePosition::NoTitle);
        cover.setFillColor(&NSColor::blackColor());
        cover.setBorderWidth(0.0);
        cover.setContentViewMargins(NSSize::new(0.0, 0.0));
        cover.setWantsLayer(true);
        cover.setAutoresizingMask(
            NSAutoresizingMaskOptions::ViewWidthSizable | NSAutoresizingMaskOptions::ViewHeightSizable,
        );
        let frame = NSRect::new(
            NSPoint::new((bounds.size.width - side) / 2.0, (bounds.size.height - side) / 2.0),
            NSSize::new(side, side),
        );
        let mark = NSImageView::initWithFrame(NSImageView::alloc(mtm), frame);
        mark.setImage(Some(&image));
        mark.setImageScaling(NSImageScaling::ScaleProportionallyUpOrDown);
        mark.setWantsLayer(true);
        mark.setAlphaValue(0.0);
        mark.setAutoresizingMask(
            NSAutoresizingMaskOptions::ViewMinXMargin
                | NSAutoresizingMaskOptions::ViewMaxXMargin
                | NSAutoresizingMaskOptions::ViewMinYMargin
                | NSAutoresizingMaskOptions::ViewMaxYMargin,
        );
        content.addSubview_positioned_relativeTo(&cover, NSWindowOrderingMode::Above, None);
        content.addSubview_positioned_relativeTo(&mark, NSWindowOrderingMode::Above, Some(&cover));
        VIEWS.with(|v| *v.borrow_mut() = Some((cover, mark)));
        true
    }

    pub fn set_mark(a: f64) {
        VIEWS.with(|v| if let Some((_, m)) = &*v.borrow() { m.setAlphaValue(a) });
    }

    /// The mark sits beside the cover (not inside it), so fade both.
    pub fn set_cover(a: f64) {
        VIEWS.with(|v| {
            if let Some((c, m)) = &*v.borrow() {
                c.setAlphaValue(a);
                m.setAlphaValue(a);
            }
        });
    }

    pub fn destroy() {
        if let Some((c, m)) = VIEWS.with(|v| v.borrow_mut().take()) {
            m.removeFromSuperview();
            c.removeFromSuperview();
        }
    }
}

#[cfg(not(any(target_os = "linux", windows, target_os = "macos")))]
mod imp {
    pub fn show(_main: &tauri::WebviewWindow) {}
    pub fn close(_app: &tauri::AppHandle) {}
}

pub use imp::{close, show};
