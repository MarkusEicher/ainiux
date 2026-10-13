// Display projection only. Stored messages and model context are never trimmed.
export function historyLineLimit(value) {
  const number = Number(value);
  return Number.isInteger(number) && number >= 1 && number <= 10000 ? number : 500;
}

// Bound source work before Markdown parsing, including a single enormous line.
// Browser rows use a conservative text-cell estimate; card chrome counts too.
export function historyWindow(entries, budget, columns = 80, content = (entry) => entry.content) {
  const limit = Math.max(1, Math.floor(budget));
  const width = Math.max(8, Math.floor(columns));
  const visible = [];
  let remaining = limit;
  let clipped = false;
  for (let index = entries.length - 1; index >= 0 && remaining > 0; --index) {
    const entry = entries[index];
    const source = String(content(entry) || "");
    const maxChars = remaining * width * 16;
    let start = Math.max(0, source.length - maxChars);
    if (start && /[\uDC00-\uDFFF]/.test(source[start])) ++start;
    const tail = source.slice(start);
    const chars = Array.from(tail);
    let rows = 1;
    let cells = 0;
    let from = chars.length;
    // Leave one row for the label/metrics. Never build a full-history DOM.
    const textRows = Math.max(1, remaining - 1);
    for (let i = chars.length - 1; i >= 0; --i) {
      const ch = chars[i];
      if (ch === "\n") { ++rows; cells = 0; }
      else {
        const code = ch.codePointAt(0);
        cells += code >= 0x1100 ? 2 : 1;
        if (cells > width) { ++rows; cells = code >= 0x1100 ? 2 : 1; }
      }
      if (rows > textRows) break;
      from = i;
    }
    const text = chars.slice(from).join("");
    const truncated = start > 0 || from > 0;
    visible.push({ entry, text, truncated });
    remaining -= Math.min(remaining, rows + 1);
    clipped ||= truncated || index > 0 && remaining <= 0;
  }
  return { visible: visible.reverse(), hasOlder: clipped || visible.length < entries.length };
}
