/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { createUpdatePie } from "./update-pie.js";

function clock() {
  let time = 0;
  const queue = [];
  let nextId = 1;
  return {
    now() {
      return time;
    },
    schedule(fn, ms) {
      const id = nextId;
      nextId += 1;
      queue.push({ id, fn, at: time + ms });
      queue.sort((a, b) => a.at - b.at || a.id - b.id);
      return id;
    },
    cancel(id) {
      const index = queue.findIndex((item) => item.id === id);
      if (index >= 0) {
        queue.splice(index, 1);
      }
    },
    advance(ms) {
      const end = time + ms;
      while (queue.length > 0 && queue[0].at <= end) {
        const item = queue.shift();
        time = item.at;
        item.fn();
      }
      time = end;
    },
  };
}

function mountPie(options = {}) {
  const el = document.createElement("span");
  el.hidden = true;
  const timers = clock();
  const pie = createUpdatePie(el, {
    now: timers.now,
    schedule: timers.schedule,
    cancel: timers.cancel,
    ...options,
  });
  return { el, pie, timers };
}

test("an idle click fills the pie over half a second and then hides it", () => {
  const { el, pie, timers } = mountPie();
  pie.start();
  expect(pie.shown()).toBe(true);
  expect(pie.turn()).toBe(0);
  expect(el.hidden).toBe(false);
  timers.advance(250);
  expect(pie.turn()).toBeCloseTo(50);
  expect(pie.shown()).toBe(true);
  timers.advance(250);
  expect(pie.shown()).toBe(false);
  expect(el.hidden).toBe(true);
});

test("a real step snaps the idle sweep back and then holds", () => {
  const { pie, timers } = mountPie();
  pie.start();
  timers.advance(200);
  expect(pie.turn()).toBeCloseTo(40);
  pie.follow(25);
  expect(pie.turn()).toBe(0);
  timers.advance(150);
  expect(pie.turn()).toBeCloseTo(25);
  expect(pie.shown()).toBe(true);
  timers.advance(1000);
  expect(pie.turn()).toBeCloseTo(25);
  expect(pie.shown()).toBe(true);
});

test("a later step follows the new percent, and an unknown total holds", () => {
  const { pie, timers } = mountPie();
  pie.follow(80);
  expect(pie.turn()).toBe(0);
  timers.advance(400);
  expect(pie.turn()).toBeCloseTo(80);
  pie.follow(10);
  expect(pie.turn()).toBeCloseTo(10);
  pie.follow(null);
  timers.advance(500);
  expect(pie.turn()).toBeCloseTo(10);
  expect(pie.shown()).toBe(true);
});

test("finishing runs the remainder up to 100 and hide is immediate", () => {
  const { pie, timers } = mountPie();
  pie.follow(50);
  timers.advance(250);
  expect(pie.turn()).toBeCloseTo(50);
  pie.finish();
  expect(pie.shown()).toBe(true);
  timers.advance(250);
  expect(pie.shown()).toBe(false);

  pie.start();
  timers.advance(100);
  expect(pie.shown()).toBe(true);
  pie.hide();
  expect(pie.shown()).toBe(false);
  expect(pie.turn()).toBe(0);
  timers.advance(1000);
  expect(pie.shown()).toBe(false);
});

test("reduced motion jumps and still keeps an idle pie up for half a second", () => {
  const { pie, timers } = mountPie({ reducedMotion: () => true });
  pie.start();
  expect(pie.turn()).toBe(100);
  expect(pie.shown()).toBe(true);
  timers.advance(499);
  expect(pie.shown()).toBe(true);
  timers.advance(1);
  expect(pie.shown()).toBe(false);

  pie.start();
  pie.follow(40);
  expect(pie.turn()).toBe(40);
  timers.advance(1000);
  expect(pie.turn()).toBe(40);
  expect(pie.shown()).toBe(true);
});
