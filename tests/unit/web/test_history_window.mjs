import assert from "node:assert/strict";
import test from "node:test";
import { historyLineLimit, historyWindow } from "../../../src/web/js/history-window-v1.js";

test("history starts with a viewport and expands only within its line cap", () => {
  const entries = Array.from({ length: 10000 }, (_, i) => ({ content: `row ${i}`, ordinal: i }));
  const initial = historyWindow(entries, 24, 80);
  const expanded = historyWindow(entries, 500, 80);
  assert.equal(initial.visible.length, 12);
  assert.equal(initial.visible.at(-1).entry.ordinal, 9999);
  assert.equal(initial.hasOlder, true);
  assert.equal(expanded.visible.length, 250);
  assert.equal(entries.length, 10000);
  assert.equal(historyLineLimit(undefined), 500);
  for (const invalid of [0, -1, 10001, "bad", 1.5]) assert.equal(historyLineLimit(invalid), 500);
  assert.equal(historyLineLimit("750"), 750);
});

test("a huge individual message is clipped before Markdown work, with a valid Unicode tail", () => {
  const content = "x".repeat(4000000) + "終わり😀";
  const entry = { content };
  const projection = historyWindow([entry], 24, 80);
  assert.equal(projection.visible.length, 1);
  assert.equal(projection.visible[0].truncated, true);
  assert.ok(projection.visible[0].text.length < 24 * 80 * 2);
  assert.ok(projection.visible[0].text.endsWith("終わり😀"));
  assert.equal(entry.content, content);
});

test("mixed history and live events retain chronology without changing action identities", () => {
  const entries = [{ content: "old", ordinal: 4 }, { content: "one\ntwo\nthree", ordinal: 5 },
    { content: "live", stream: true }];
  const projected = historyWindow(entries, 5, 80);
  assert.equal(projected.visible.at(-1).entry.stream, true);
  assert.equal(projected.visible[0].entry.ordinal, 5);
  assert.ok(projected.visible[0].text.endsWith("three"));
  assert.equal(entries[1].content, "one\ntwo\nthree");
});
