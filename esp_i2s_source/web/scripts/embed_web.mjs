// WEB-1z: gzip the single-file Vite build and emit the embeddable artifact into
// main/www/. Run automatically by `npm run build`. The .gz is committed so
// idf.py build / CI never need Node (SPEC §5.5).
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { gzipSync } from "node:zlib";
import { createHash } from "node:crypto";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const webRoot = resolve(here, "..");
const distHtml = resolve(webRoot, "dist", "index.html");
const outDir = resolve(webRoot, "..", "main", "www");

const html = readFileSync(distHtml);
const gz = gzipSync(html, { level: 9 });
const hash = createHash("sha256").update(html).digest("hex").slice(0, 16);

mkdirSync(outDir, { recursive: true });
writeFileSync(resolve(outDir, "index.html.gz"), gz);
// Hash of the built HTML — lets the firmware/CI warn if the committed .gz is
// stale vs a fresh `npm run build`.
writeFileSync(resolve(outDir, "index.html.gz.sha256"), hash + "\n");

const kb = (n) => (n / 1024).toFixed(1);
console.log(`embed_web: ${html.length} B html -> ${gz.length} B gz (${kb(gz.length)} KB), sha=${hash}`);
if (gz.length > 200 * 1024) {
  console.warn(`embed_web: WARNING bundle ${kb(gz.length)} KB exceeds the 200 KB target (SPEC §5.5)`);
}
