//! Start splash drawn natively (no webview, no CSS) inside the main window. It is black at
//! once, the mark fades in, and the cover fades out when the page emits
//! `pav-app-ready` (D-UI.md Native splash). Linux draws it with GTK, which
//! Tauri already links. Other desktops compile to no-ops for now.

/// Mark width in logical px. The bitmap is the 1024 px source scaled
/// (bilinear) to this times the monitor scale. Change here to resize.
pub const MARK_LOGICAL_PX: f64 = 128.0;
/// Solid black after the splash opens, before the mark starts to fade in.
pub const BLACK_HOLD_MS: u64 = 400;
/// Fade-in of the mark and fade-out of the window.
pub const FADE_MS: u64 = 400;
/// Least time the mark stays fully shown before the fade-out may start.
/// The fade-out also waits for `pav-app-ready` (or the 8 s fallback).
pub const MIN_SHOW_MS: u64 = 2000;

#[cfg(target_os = "linux")]
mod imp {
    use super::{BLACK_HOLD_MS, FADE_MS, MARK_LOGICAL_PX, MIN_SHOW_MS};
    use gtk::gdk::prelude::GdkContextExt;
    use gtk::prelude::*;
    use gtk::{gdk, gdk_pixbuf, glib};
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

#[cfg(not(target_os = "linux"))]
mod imp {
    pub fn show(_main: &tauri::WebviewWindow) {}
    pub fn close(_app: &tauri::AppHandle) {}
}

pub use imp::{close, show};
