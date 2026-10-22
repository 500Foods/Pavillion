/* Pie behind the Update icon. A full 0–100 sweep is SWEEP_MS. Real
   progress moves at that same rate and then holds. Reduced motion jumps
   to the target and still keeps an idle sweep on screen for SWEEP_MS. */

const SWEEP_MS = 500;
const TICK_MS = 50;

function clampTurn(value) {
  if (!Number.isFinite(value)) {
    return null;
  }
  return Math.max(0, Math.min(100, value));
}

export function createUpdatePie(el, options = {}) {
  const now = typeof options.now === "function" ? options.now : () => Date.now();
  const schedule = typeof options.schedule === "function" ? options.schedule : (fn, ms) => setTimeout(fn, ms);
  const cancel = typeof options.cancel === "function" ? options.cancel : (id) => clearTimeout(id);
  const reducedMotion = typeof options.reducedMotion === "function" ? options.reducedMotion : () => false;
  const onChange = typeof options.onChange === "function" ? options.onChange : () => {};

  let displayed = 0;
  let target = 0;
  let hold = false;
  let visible = false;
  let timer = 0;
  let tickAt = 0;
  let alive = true;

  function notify() {
    onChange();
  }

  function paint() {
    if (!el) {
      return;
    }
    el.hidden = !visible;
    el.style.setProperty("--pav-sync-turn", `${displayed}%`);
  }

  function clearTimer() {
    if (timer) {
      cancel(timer);
      timer = 0;
    }
  }

  function hideNow() {
    visible = false;
    displayed = 0;
    target = 0;
    hold = false;
    paint();
    notify();
  }

  function arm() {
    clearTimer();
    if (!alive || !visible) {
      return;
    }
    if (displayed >= target - 0.001) {
      displayed = target;
      paint();
      if (!hold && target >= 100) {
        hideNow();
      } else {
        notify();
      }
      return;
    }
    if (reducedMotion()) {
      const wait = ((target - displayed) / 100) * SWEEP_MS;
      displayed = target;
      paint();
      notify();
      if (!hold && target >= 100) {
        timer = schedule(() => {
          timer = 0;
          if (!alive || hold || !visible) {
            return;
          }
          hideNow();
        }, wait);
      }
      return;
    }
    tickAt = now();
    timer = schedule(onTick, TICK_MS);
  }

  function onTick() {
    timer = 0;
    if (!alive || !visible) {
      return;
    }
    const t = now();
    const dt = Math.max(0, t - tickAt);
    tickAt = t;
    const step = (100 / SWEEP_MS) * (dt > 0 ? dt : TICK_MS);
    displayed = Math.min(target, displayed + step);
    paint();
    notify();
    arm();
  }

  return {
    shown() {
      return visible;
    },
    turn() {
      return displayed;
    },
    start() {
      if (!alive) {
        return;
      }
      clearTimer();
      hold = false;
      target = 100;
      displayed = 0;
      visible = true;
      paint();
      notify();
      arm();
    },
    /* The first real step snaps a premature idle sweep back to empty.
       Later steps follow the latest percent, including a lower one.
       A missing total holds. */
    follow(turn) {
      if (!alive) {
        return;
      }
      const entering = !hold;
      if (entering) {
        displayed = 0;
        target = 0;
      }
      hold = true;
      visible = true;
      const next = clampTurn(turn);
      if (next == null) {
        target = displayed;
      } else {
        target = next;
      }
      paint();
      notify();
      arm();
    },
    finish() {
      if (!alive || !visible) {
        return;
      }
      hold = false;
      target = 100;
      paint();
      notify();
      arm();
    },
    hide() {
      if (!alive) {
        return;
      }
      clearTimer();
      visible = false;
      displayed = 0;
      target = 0;
      hold = false;
      paint();
      notify();
    },
    destroy() {
      alive = false;
      clearTimer();
    },
  };
}
