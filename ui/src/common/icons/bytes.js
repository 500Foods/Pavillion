/* Asset bytes stay out of the preferences blob. The page reads them in 1 MB slices. */
import { paintRender } from "./snippet.js";

const CHUNK = 1048576;

export function bytesToBase64(bytes) {
  let bin = "";
  const step = 8192;
  for (let i = 0; i < bytes.length; i += step) {
    const slice = bytes.subarray(i, i + step);
    bin += String.fromCharCode(...slice);
  }
  return btoa(bin);
}

export function base64ToBytes(text) {
  const bin = atob(text || "");
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i += 1) {
    out[i] = bin.charCodeAt(i);
  }
  return out;
}

export async function readAssetBytes(api, id) {
  const parts = [];
  let offset = 0;
  for (;;) {
    const slice = await api.asset.read({ id, offset, len: CHUNK });
    const bytes = base64ToBytes(slice && slice.base64);
    if (bytes.length === 0) {
      break;
    }
    parts.push(bytes);
    offset += bytes.length;
    if (bytes.length < CHUNK) {
      break;
    }
  }
  const total = parts.reduce((sum, part) => sum + part.length, 0);
  const out = new Uint8Array(total);
  let at = 0;
  for (const part of parts) {
    out.set(part, at);
    at += part.length;
  }
  return out;
}

/* Draw from the bytes, never from a pav-asset img, so the canvas stays untainted. */
export async function storeRenderedPng(api, bytes, transform) {
  const doc = globalThis.document;
  if (!doc || typeof createImageBitmap !== "function") {
    throw new Error("Canvas is not available.");
  }
  const canvas = doc.createElement("canvas");
  const ctx = canvas.getContext("2d");
  if (!ctx) {
    throw new Error("Canvas is not available.");
  }
  const bitmap = await createImageBitmap(new Blob([bytes], { type: "image/png" }));
  paintRender(ctx, bitmap, transform);
  const blob = await new Promise((resolve) => {
    canvas.toBlob((out) => resolve(out), "image/png");
  });
  if (!blob) {
    throw new Error("Could not encode the PNG.");
  }
  const raw = new Uint8Array(await blob.arrayBuffer());
  const origin = transform && typeof transform.originalId === "string" ? transform.originalId : "";
  return api.asset.put({
    mime: "image/png",
    kind: "png-render",
    base64: bytesToBase64(raw),
    ...(origin ? { origin } : {}),
  });
}
