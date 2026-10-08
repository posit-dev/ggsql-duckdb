// Entry point for the vendored hep viewer bundle (rust/assets/hep-viewer.js,
// IIFE). The bundle consumes globalThis.ggsqlHepAssets — set by
// rust/assets/hep-assets.js (served mode) or inlined into the standalone HTML
// document — and exposes window.ggsqlRenderHep(el, bytes). Assets travel as
// gzip+base64 so the same viewer works identically from the extension's HTTP
// server and from a file:// document with no URL resolution at all.

import init, {
  registerFont,
  setGenericFamily,
  PlotView,
} from "hephaestus-svg-wasm";

// CSS weight/style per bundled Roboto face, for the @font-face blocks that
// let the browser draw the family wasm shaped with.
const FACES = {
  regular: [400, "normal"],
  bold: [700, "normal"],
  italic: [400, "italic"],
  bolditalic: [700, "italic"],
};

function decodeBase64(base64) {
  const binary = atob(base64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) {
    bytes[i] = binary.charCodeAt(i);
  }
  return bytes;
}

async function gunzip(bytes) {
  const stream = new Blob([bytes])
    .stream()
    .pipeThrough(new DecompressionStream("gzip"));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

let modulePromise = null;

function loadHep() {
  if (!modulePromise) {
    modulePromise = (async () => {
      const assets = globalThis.ggsqlHepAssets;
      if (!assets) {
        throw new Error(
          "ggsql: hephaestus assets are missing (globalThis.ggsqlHepAssets is unset)",
        );
      }
      await init({ module_or_path: await gunzip(decodeBase64(assets.wasm)) });
      // Fonts register twice over: with the shaper, which measures, and with
      // the page as @font-face, which draws. Without the shaper half the
      // layout collapses (a browser enumerates no system fonts); without the
      // page half the browser substitutes another family for the runs wasm
      // measured as Roboto.
      const faceCss = [];
      for (const [face, [weight, style]] of Object.entries(FACES)) {
        const bytes = await gunzip(decodeBase64(assets.fonts[face]));
        registerFont(bytes);
        const url = URL.createObjectURL(
          new Blob([bytes], { type: "font/ttf" }),
        );
        faceCss.push(
          `@font-face{font-family:'Roboto';font-style:${style};` +
            `font-weight:${weight};font-display:block;` +
            `src:url('${url}') format('truetype');}`,
        );
      }
      const styleEl = document.createElement("style");
      styleEl.textContent = faceCss.join("\n");
      document.head.appendChild(styleEl);
      setGenericFamily("sans-serif", ["Roboto"]);
    })();
  }
  return modulePromise;
}

// Render a .hep document into `el`. Returns the PlotView (callers may keep it
// to free/redraw; the app shell replaces the element content per plot).
window.ggsqlRenderHep = async function (el, bytes) {
  await loadHep();
  // defaultFont is false: fonts were registered explicitly from the embedded
  // assets, not fetched from a URL.
  const view = await PlotView.create(el, bytes, { defaultFont: false });
  for (const warning of view.warnings) {
    console.warn("ggsql:", warning);
  }
  return view;
};
