(() => {
  // node_modules/hephaestus-svg-wasm/hephaestus_svg_wasm.js
  var PlotDocument = class _PlotDocument {
    static __wrap(ptr) {
      const obj = Object.create(_PlotDocument.prototype);
      obj.__wbg_ptr = ptr;
      PlotDocumentFinalization.register(obj, obj.__wbg_ptr, obj);
      return obj;
    }
    __destroy_into_raw() {
      const ptr = this.__wbg_ptr;
      this.__wbg_ptr = 0;
      PlotDocumentFinalization.unregister(this);
      return ptr;
    }
    free() {
      const ptr = this.__destroy_into_raw();
      wasm.__wbg_plotdocument_free(ptr, 0);
    }
    /**
     * Dots per inch the document's writer rendered at, if it recorded one.
     *
     * Advisory here in a way it is not on the canvas client, which sizes a
     * backing store from it. This one always draws at 96.
     * @returns {number | undefined}
     */
    hintDpi() {
      try {
        const retptr = wasm.__wbindgen_add_to_stack_pointer(-16);
        wasm.plotdocument_hintDpi(retptr, this.__wbg_ptr);
        var r0 = getDataViewMemory0().getInt32(retptr + 4 * 0, true);
        var r2 = getDataViewMemory0().getFloat64(retptr + 8 * 1, true);
        return r0 === 0 ? void 0 : r2;
      } finally {
        wasm.__wbindgen_add_to_stack_pointer(16);
      }
    }
    /**
     * Height, in points, the document's writer rendered at, if it recorded one.
     * @returns {number | undefined}
     */
    hintHeight() {
      try {
        const retptr = wasm.__wbindgen_add_to_stack_pointer(-16);
        wasm.plotdocument_hintHeight(retptr, this.__wbg_ptr);
        var r0 = getDataViewMemory0().getInt32(retptr + 4 * 0, true);
        var r2 = getDataViewMemory0().getFloat64(retptr + 8 * 1, true);
        return r0 === 0 ? void 0 : r2;
      } finally {
        wasm.__wbindgen_add_to_stack_pointer(16);
      }
    }
    /**
     * Width, in points, the document's writer rendered at, if it recorded one.
     *
     * Advisory — a document can be drawn at any size. Useful as an aspect
     * ratio for a container that has to be sized before anything is laid out.
     * @returns {number | undefined}
     */
    hintWidth() {
      try {
        const retptr = wasm.__wbindgen_add_to_stack_pointer(-16);
        wasm.plotdocument_hintWidth(retptr, this.__wbg_ptr);
        var r0 = getDataViewMemory0().getInt32(retptr + 4 * 0, true);
        var r2 = getDataViewMemory0().getFloat64(retptr + 8 * 1, true);
        return r0 === 0 ? void 0 : r2;
      } finally {
        wasm.__wbindgen_add_to_stack_pointer(16);
      }
    }
    /**
     * Whether the inverted theme is in use.
     * @returns {boolean}
     */
    isDark() {
      const ret = wasm.plotdocument_isDark(this.__wbg_ptr);
      return ret !== 0;
    }
    /**
     * Whether the background rect is suppressed.
     * @returns {boolean}
     */
    isTransparent() {
      const ret = wasm.plotdocument_isTransparent(this.__wbg_ptr);
      return ret !== 0;
    }
    /**
     * Read `doc`.
     *
     * Synchronous, unlike the canvas client's `create`: there is no adapter
     * or device to acquire. Fails only if the document is unreadable — a
     * truncated file, or one written at a different format major.
     * @param {Uint8Array} doc
     * @returns {PlotDocument}
     */
    static load(doc) {
      try {
        const retptr = wasm.__wbindgen_add_to_stack_pointer(-16);
        const ptr0 = passArray8ToWasm0(doc, wasm.__wbindgen_export);
        const len0 = WASM_VECTOR_LEN;
        wasm.plotdocument_load(retptr, ptr0, len0);
        var r0 = getDataViewMemory0().getInt32(retptr + 4 * 0, true);
        var r1 = getDataViewMemory0().getInt32(retptr + 4 * 1, true);
        var r2 = getDataViewMemory0().getInt32(retptr + 4 * 2, true);
        if (r2) {
          throw takeObject(r1);
        }
        return _PlotDocument.__wrap(r0);
      } finally {
        wasm.__wbindgen_add_to_stack_pointer(16);
      }
    }
    /**
     * Whether picking attributes are emitted.
     * @returns {boolean}
     */
    pickIds() {
      const ret = wasm.plotdocument_pickIds(this.__wbg_ptr);
      return ret !== 0;
    }
    /**
     * Switch between the document's theme and its inverted form.
     *
     * Inversion swaps the palette's paper and ink anchors, which every
     * chrome element references, so gridlines, axis text and titles all
     * follow. A geom given an explicit color keeps it — marks adapt only
     * when the plot expressed them as palette references.
     *
     * Does not draw; call [`Self::to_svg`] after.
     * @param {boolean} dark
     */
    setDark(dark) {
      wasm.plotdocument_setDark(this.__wbg_ptr, dark);
    }
    /**
     * Emit `data-pick-id` on primitives and `data-pick-kind` on scope groups.
     *
     * On by default, because hit testing an inline SVG is what the attributes
     * are for and a page gets it with no wasm involved. Turn it off for a
     * dense plot that is never hit-tested: the attributes are pure weight
     * there, and `pointer-events="none"` lands on every unpicked primitive.
     *
     * Does not draw; call [`Self::to_svg`] after.
     * @param {boolean} on
     */
    setPickIds(on) {
      wasm.plotdocument_setPickIds(this.__wbg_ptr, on);
    }
    /**
     * Emit no background rect, so whatever is behind the SVG shows through.
     *
     * Off by default, which matches the canvas client: a document names a
     * paper color and honouring it is what makes the two clients agree. A
     * page that owns its own light and dark wants this on.
     * @param {boolean} transparent
     */
    setTransparent(transparent) {
      wasm.plotdocument_setTransparent(this.__wbg_ptr, transparent);
    }
    /**
     * Draw at `width` x `height` CSS pixels and return the markup.
     *
     * `id_prefix` prefixes every generated id. It is not a nicety: two SVGs
     * inlined into one page that both define `#lg0` will have the second's
     * `url(#lg0)` resolve to the first's definition, in every browser. Pass
     * something unique per view.
     * @param {number} width
     * @param {number} height
     * @param {string} id_prefix
     * @returns {SvgRender}
     */
    toSvg(width, height, id_prefix) {
      const ptr0 = passStringToWasm0(id_prefix, wasm.__wbindgen_export, wasm.__wbindgen_export2);
      const len0 = WASM_VECTOR_LEN;
      const ret = wasm.plotdocument_toSvg(this.__wbg_ptr, width, height, ptr0, len0);
      return SvgRender.__wrap(ret);
    }
  };
  if (Symbol.dispose) PlotDocument.prototype[Symbol.dispose] = PlotDocument.prototype.free;
  var SvgRender = class _SvgRender {
    static __wrap(ptr) {
      const obj = Object.create(_SvgRender.prototype);
      obj.__wbg_ptr = ptr;
      SvgRenderFinalization.register(obj, obj.__wbg_ptr, obj);
      return obj;
    }
    __destroy_into_raw() {
      const ptr = this.__wbg_ptr;
      this.__wbg_ptr = 0;
      SvgRenderFinalization.unregister(this);
      return ptr;
    }
    free() {
      const ptr = this.__destroy_into_raw();
      wasm.__wbg_svgrender_free(ptr, 0);
    }
    /**
     * The document, as markup ready to place in the page.
     * @returns {string}
     */
    get svg() {
      let deferred1_0;
      let deferred1_1;
      try {
        const retptr = wasm.__wbindgen_add_to_stack_pointer(-16);
        wasm.svgrender_svg(retptr, this.__wbg_ptr);
        var r0 = getDataViewMemory0().getInt32(retptr + 4 * 0, true);
        var r1 = getDataViewMemory0().getInt32(retptr + 4 * 1, true);
        deferred1_0 = r0;
        deferred1_1 = r1;
        return getStringFromWasm0(r0, r1);
      } finally {
        wasm.__wbindgen_add_to_stack_pointer(16);
        wasm.__wbindgen_export3(deferred1_0, deferred1_1, 1);
      }
    }
    /**
     * Everything the scene expressed that SVG could not, deduplicated.
     * @returns {string[]}
     */
    get warnings() {
      try {
        const retptr = wasm.__wbindgen_add_to_stack_pointer(-16);
        wasm.svgrender_warnings(retptr, this.__wbg_ptr);
        var r0 = getDataViewMemory0().getInt32(retptr + 4 * 0, true);
        var r1 = getDataViewMemory0().getInt32(retptr + 4 * 1, true);
        var v1 = getArrayJsValueFromWasm0(r0, r1);
        wasm.__wbindgen_export3(r0, r1 * 4, 4);
        return v1;
      } finally {
        wasm.__wbindgen_add_to_stack_pointer(16);
      }
    }
  };
  if (Symbol.dispose) SvgRender.prototype[Symbol.dispose] = SvgRender.prototype.free;
  function hasFonts() {
    const ret = wasm.hasFonts();
    return ret !== 0;
  }
  function registerFont(bytes) {
    try {
      const retptr = wasm.__wbindgen_add_to_stack_pointer(-16);
      const ptr0 = passArray8ToWasm0(bytes, wasm.__wbindgen_export);
      const len0 = WASM_VECTOR_LEN;
      wasm.registerFont(retptr, ptr0, len0);
      var r0 = getDataViewMemory0().getInt32(retptr + 4 * 0, true);
      var r1 = getDataViewMemory0().getInt32(retptr + 4 * 1, true);
      var r2 = getDataViewMemory0().getInt32(retptr + 4 * 2, true);
      var r3 = getDataViewMemory0().getInt32(retptr + 4 * 3, true);
      if (r3) {
        throw takeObject(r2);
      }
      var v2 = getArrayJsValueFromWasm0(r0, r1);
      wasm.__wbindgen_export3(r0, r1 * 4, 4);
      return v2;
    } finally {
      wasm.__wbindgen_add_to_stack_pointer(16);
    }
  }
  function setGenericFamily(kind, families) {
    try {
      const retptr = wasm.__wbindgen_add_to_stack_pointer(-16);
      const ptr0 = passStringToWasm0(kind, wasm.__wbindgen_export, wasm.__wbindgen_export2);
      const len0 = WASM_VECTOR_LEN;
      const ptr1 = passArrayJsValueToWasm0(families, wasm.__wbindgen_export);
      const len1 = WASM_VECTOR_LEN;
      wasm.setGenericFamily(retptr, ptr0, len0, ptr1, len1);
      var r0 = getDataViewMemory0().getInt32(retptr + 4 * 0, true);
      var r1 = getDataViewMemory0().getInt32(retptr + 4 * 1, true);
      if (r1) {
        throw takeObject(r0);
      }
    } finally {
      wasm.__wbindgen_add_to_stack_pointer(16);
    }
  }
  function __wbg_get_imports() {
    const import0 = {
      __proto__: null,
      __wbg_Error_67e7344beaa85059: function(arg0, arg1) {
        const ret = Error(getStringFromWasm0(arg0, arg1));
        return addHeapObject(ret);
      },
      __wbg___wbindgen_string_get_92ab86bb19cbc12f: function(arg0, arg1) {
        const obj = getObject(arg1);
        const ret = typeof obj === "string" ? obj : void 0;
        var ptr1 = isLikeNone(ret) ? 0 : passStringToWasm0(ret, wasm.__wbindgen_export, wasm.__wbindgen_export2);
        var len1 = WASM_VECTOR_LEN;
        getDataViewMemory0().setInt32(arg0 + 4 * 1, len1, true);
        getDataViewMemory0().setInt32(arg0 + 4 * 0, ptr1, true);
      },
      __wbg___wbindgen_throw_5d9e815e6fdf150f: function(arg0, arg1) {
        throw new Error(getStringFromWasm0(arg0, arg1));
      },
      __wbindgen_generic_0000000000000001: function(arg0, arg1) {
        const ret = getStringFromWasm0(arg0, arg1);
        return addHeapObject(ret);
      },
      __wbindgen_object_drop_ref: function(arg0) {
        takeObject(arg0);
      }
    };
    return {
      __proto__: null,
      "./hephaestus_svg_wasm_bg.js": import0
    };
  }
  var PlotDocumentFinalization = typeof FinalizationRegistry === "undefined" ? { register: () => {
  }, unregister: () => {
  } } : new FinalizationRegistry((ptr) => wasm.__wbg_plotdocument_free(ptr, 1));
  var SvgRenderFinalization = typeof FinalizationRegistry === "undefined" ? { register: () => {
  }, unregister: () => {
  } } : new FinalizationRegistry((ptr) => wasm.__wbg_svgrender_free(ptr, 1));
  function addHeapObject(obj) {
    if (heap_next === heap.length) heap.push(heap.length + 1);
    const idx = heap_next;
    heap_next = heap[idx];
    heap[idx] = obj;
    return idx;
  }
  function dropObject(idx) {
    if (idx < 1028) return;
    heap[idx] = heap_next;
    heap_next = idx;
  }
  function getArrayJsValueFromWasm0(ptr, len) {
    ptr = ptr >>> 0;
    const mem = getDataViewMemory0();
    const result = [];
    for (let i = ptr; i < ptr + 4 * len; i += 4) {
      result.push(takeObject(mem.getUint32(i, true)));
    }
    return result;
  }
  var cachedDataViewMemory0 = null;
  function getDataViewMemory0() {
    if (cachedDataViewMemory0 === null || cachedDataViewMemory0.buffer.detached === true || cachedDataViewMemory0.buffer.detached === void 0 && cachedDataViewMemory0.buffer !== wasm.memory.buffer) {
      cachedDataViewMemory0 = new DataView(wasm.memory.buffer);
    }
    return cachedDataViewMemory0;
  }
  function getStringFromWasm0(ptr, len) {
    return decodeText(ptr >>> 0, len);
  }
  var cachedUint8ArrayMemory0 = null;
  function getUint8ArrayMemory0() {
    if (cachedUint8ArrayMemory0 === null || cachedUint8ArrayMemory0.byteLength === 0) {
      cachedUint8ArrayMemory0 = new Uint8Array(wasm.memory.buffer);
    }
    return cachedUint8ArrayMemory0;
  }
  function getObject(idx) {
    return heap[idx];
  }
  var heap = new Array(1024).fill(void 0);
  heap.push(void 0, null, true, false);
  var heap_next = heap.length;
  function isLikeNone(x) {
    return x === void 0 || x === null;
  }
  function passArray8ToWasm0(arg, malloc) {
    const ptr = malloc(arg.length * 1, 1) >>> 0;
    getUint8ArrayMemory0().set(arg, ptr / 1);
    WASM_VECTOR_LEN = arg.length;
    return ptr;
  }
  function passArrayJsValueToWasm0(array, malloc) {
    const ptr = malloc(array.length * 4, 4) >>> 0;
    const mem = getDataViewMemory0();
    for (let i = 0; i < array.length; i++) {
      mem.setUint32(ptr + 4 * i, addHeapObject(array[i]), true);
    }
    WASM_VECTOR_LEN = array.length;
    return ptr;
  }
  function passStringToWasm0(arg, malloc, realloc) {
    if (realloc === void 0) {
      const buf = cachedTextEncoder.encode(arg);
      const ptr2 = malloc(buf.length, 1) >>> 0;
      getUint8ArrayMemory0().subarray(ptr2, ptr2 + buf.length).set(buf);
      WASM_VECTOR_LEN = buf.length;
      return ptr2;
    }
    let len = arg.length;
    let ptr = malloc(len, 1) >>> 0;
    const mem = getUint8ArrayMemory0();
    let offset = 0;
    for (; offset < len; offset++) {
      const code = arg.charCodeAt(offset);
      if (code > 127) break;
      mem[ptr + offset] = code;
    }
    if (offset !== len) {
      if (offset !== 0) {
        arg = arg.slice(offset);
      }
      ptr = realloc(ptr, len, len = offset + arg.length * 3, 1) >>> 0;
      const view = getUint8ArrayMemory0().subarray(ptr + offset, ptr + len);
      const ret = cachedTextEncoder.encodeInto(arg, view);
      offset += ret.written;
      ptr = realloc(ptr, len, offset, 1) >>> 0;
    }
    WASM_VECTOR_LEN = offset;
    return ptr;
  }
  function takeObject(idx) {
    const ret = getObject(idx);
    dropObject(idx);
    return ret;
  }
  var cachedTextDecoder = new TextDecoder("utf-8", { ignoreBOM: true, fatal: true });
  cachedTextDecoder.decode();
  var MAX_SAFARI_DECODE_BYTES = 2146435072;
  var numBytesDecoded = 0;
  function decodeText(ptr, len) {
    numBytesDecoded += len;
    if (numBytesDecoded >= MAX_SAFARI_DECODE_BYTES) {
      cachedTextDecoder = new TextDecoder("utf-8", { ignoreBOM: true, fatal: true });
      cachedTextDecoder.decode();
      numBytesDecoded = len;
    }
    return cachedTextDecoder.decode(getUint8ArrayMemory0().subarray(ptr, ptr + len));
  }
  var cachedTextEncoder = new TextEncoder();
  if (!("encodeInto" in cachedTextEncoder)) {
    cachedTextEncoder.encodeInto = function(arg, view) {
      const buf = cachedTextEncoder.encode(arg);
      view.set(buf);
      return {
        read: arg.length,
        written: buf.length
      };
    };
  }
  var WASM_VECTOR_LEN = 0;
  var wasmModule;
  var wasmInstance;
  var wasm;
  function __wbg_finalize_init(instance, module) {
    wasmInstance = instance;
    wasm = instance.exports;
    wasmModule = module;
    cachedDataViewMemory0 = null;
    cachedUint8ArrayMemory0 = null;
    return wasm;
  }
  async function __wbg_load(module, imports) {
    if (typeof Response === "function" && module instanceof Response) {
      if (!module.ok) {
        throw new Error(`failed to fetch Wasm: ${module.status} ${module.statusText} fetching '${module.url}'`);
      }
      if (typeof WebAssembly.instantiateStreaming === "function") {
        try {
          return await WebAssembly.instantiateStreaming(module, imports);
        } catch (e) {
          const validResponse = expectedResponseType(module.type);
          if (validResponse && module.headers.get("Content-Type") !== "application/wasm") {
            console.warn("`WebAssembly.instantiateStreaming` failed because your server does not serve Wasm with `application/wasm` MIME type. Falling back to `WebAssembly.instantiate` which is slower. Original error:\n", e);
          } else {
            throw e;
          }
        }
      }
      const bytes = await module.arrayBuffer();
      return await WebAssembly.instantiate(bytes, imports);
    } else {
      const instance = await WebAssembly.instantiate(module, imports);
      if (instance instanceof WebAssembly.Instance) {
        return { instance, module };
      } else {
        return instance;
      }
    }
    function expectedResponseType(type) {
      switch (type) {
        case "basic":
        case "cors":
        case "default":
          return true;
      }
      return false;
    }
  }
  async function __wbg_init(module_or_path) {
    if (wasm !== void 0) return wasm;
    if (module_or_path !== void 0) {
      if (Object.getPrototypeOf(module_or_path) === Object.prototype) {
        ({ module_or_path } = module_or_path);
      } else {
        console.warn("using deprecated parameters for the initialization function; pass a single object instead");
      }
    }
    if (module_or_path === void 0) {
      module_or_path = new URL("hephaestus_svg_wasm_bg.wasm", document.baseURI);
    }
    const imports = __wbg_get_imports();
    if (typeof module_or_path === "string" || typeof Request === "function" && module_or_path instanceof Request || typeof URL === "function" && module_or_path instanceof URL) {
      module_or_path = fetch(module_or_path);
    }
    const { instance, module } = await __wbg_load(await module_or_path, imports);
    return __wbg_finalize_init(instance, module);
  }

  // node_modules/hephaestus-svg-wasm/hephaestus-svg.js
  var registered = /* @__PURE__ */ new Set();
  async function registerFontFromUrl(url, opts = {}) {
    const key = opts.key ?? url;
    if (registered.has(key)) return [];
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error(`font fetch failed: ${response.status} ${response.statusText} for ${url}`);
    }
    const families = registerFont(new Uint8Array(await response.arrayBuffer()));
    registered.add(key);
    if (opts.genericFor) setGenericFamily(opts.genericFor, families);
    return families;
  }
  var DEFAULT_FACES = ["regular", "bold", "italic", "bolditalic"].map(
    (v) => new URL(`./fonts/roboto-${v}.ttf`, document.baseURI).href
  );
  var DEFAULT_FAMILY = "Roboto";
  var DEFAULT_FACE_CSS = [
    ["regular", 400, "normal"],
    ["bold", 700, "normal"],
    ["italic", 400, "italic"],
    ["bolditalic", 700, "italic"]
  ].map(
    ([v, weight, style]) => `@font-face{font-family:'${DEFAULT_FAMILY}';font-style:${style};font-weight:${weight};font-display:block;src:url('${new URL(`./fonts/roboto-${v}.ttf`, document.baseURI).href}') format('truetype');}`
  ).join("\n");
  var DEFAULT_FACE_STYLE_ID = "hephaestus-svg-default-faces";
  function installDefaultFaceCss() {
    if (typeof document === "undefined") return;
    if (document.getElementById(DEFAULT_FACE_STYLE_ID)) return;
    const style = document.createElement("style");
    style.id = DEFAULT_FACE_STYLE_ID;
    style.textContent = DEFAULT_FACE_CSS;
    document.head.appendChild(style);
  }
  async function registerDefaultFonts() {
    const families = await Promise.all(
      DEFAULT_FACES.map((url) => registerFontFromUrl(url, { key: url }))
    );
    setGenericFamily("sans-serif", [DEFAULT_FAMILY]);
    installDefaultFaceCss();
    return [...new Set(families.flat())];
  }
  var nextViewId = 0;
  var PlotView = class _PlotView {
    /**
     * @param {HTMLElement} container the element the SVG is placed inside. Its
     *   content box is what the plot is laid out to, and its existing content
     *   is left alone until there is markup to replace it with — so a producer
     *   that put a natively-emitted SVG in the served HTML gets an exact swap
     *   for free, and a client that never boots leaves that picture on screen.
     * @param {Uint8Array|ArrayBuffer} doc bytes of a `.hep` document.
     * @param {PlotViewOptions} [opts]
     */
    static async create(container, doc, opts = {}) {
      const bytes = doc instanceof Uint8Array ? doc : new Uint8Array(doc);
      if (opts.defaultFont !== false && !hasFonts()) {
        await registerDefaultFonts();
      }
      return new _PlotView(container, PlotDocument.load(bytes), opts);
    }
    /** @private — use {@link PlotView.create}. */
    constructor(container, plot, opts) {
      this.container = container;
      this.plot = plot;
      this.warnings = [];
      this.idPrefix = opts.idPrefix ?? `hep${nextViewId++}-`;
      this._aspect = opts.aspect ?? null;
      this._frame = null;
      this._observer = null;
      this._media = null;
      this._onMedia = null;
      this._lastSize = null;
      this._freed = false;
      if (opts.picking === false) this.plot.setPickIds(false);
      if (opts.transparent === true) this.plot.setTransparent(true);
      this.setColorScheme(opts.colorScheme ?? "light");
      if (opts.autoResize !== false) {
        this._observer = new ResizeObserver((entries) => this._onObserved(entries));
        this._observer.observe(this.container);
      }
      this._renderNow();
    }
    /** Draw immediately, cancelling any frame already scheduled. */
    redraw() {
      this._renderNow();
    }
    /**
     * Draw at an explicit size, in CSS pixels.
     *
     * Only needed when `autoResize` is off, or to drive the size from something
     * the observer cannot see.
     */
    resize(width, height) {
      if (this._freed) return;
      this._draw(Math.max(1, Math.round(width)), Math.max(1, Math.round(height)));
    }
    /**
     * Choose the theme: as authored, inverted, or following the OS.
     *
     * `'auto'` attaches a `prefers-color-scheme` listener and re-renders when
     * it changes. Inversion swaps the palette's paper and ink anchors, so
     * chrome follows; a geom given an explicit color keeps it.
     *
     * @param {'light'|'dark'|'auto'} scheme
     */
    setColorScheme(scheme) {
      if (this._freed) return;
      if (!["light", "dark", "auto"].includes(scheme)) {
        throw new Error(`unknown color scheme ${JSON.stringify(scheme)}`);
      }
      this._detachMedia();
      this._scheme = scheme;
      if (scheme === "auto") {
        this._media = window.matchMedia("(prefers-color-scheme: dark)");
        this._onMedia = (e) => {
          this.plot.setDark(e.matches);
          this._schedule();
        };
        this._media.addEventListener("change", this._onMedia);
        this.plot.setDark(this._media.matches);
      } else {
        this.plot.setDark(scheme === "dark");
      }
      this._schedule();
    }
    /** The scheme last asked for — `'auto'` if it is following the OS. */
    colorScheme() {
      return this._scheme;
    }
    /** Whether the inverted theme is currently drawn. Resolves `'auto'`. */
    isDark() {
      return this.plot.isDark();
    }
    /**
     * The markup currently in the page, for saving or copying.
     *
     * This client's answer to the canvas one's PNG export, and a better one:
     * the bytes are the plot rather than a picture of it at one size. There is
     * no rasteriser here, so a PNG would need the page to draw the SVG into a
     * canvas itself.
     */
    toSvgString() {
      return this.container.firstElementChild?.outerHTML ?? "";
    }
    /**
     * The row id under a point, or `undefined` where there is none.
     *
     * Takes CSS pixels relative to the container's top-left — `offsetX` /
     * `offsetY` of an event on the container. Answered from the DOM rather
     * than from a hit index in wasm, which is what the `data-pick-id`
     * attributes are for: the markup on screen *is* the index, so the answer
     * cannot lag the frame.
     *
     * `undefined` covers three different things, which {@link PlotView#pick}
     * separates: empty space, an occluding primitive, and chrome — which is
     * hittable but carries no id, reporting through its scope chain instead.
     */
    pickAt(x, y) {
      return this.pick(x, y)?.id;
    }
    /**
     * The scope chain under a point, outermost first.
     *
     * The `composition → plot → region → axis|legend|geom → part → item`
     * nesting the SVG backend emits as `<g data-pick-kind>` groups, which is
     * what lets a hit report *what* it hit rather than only which row.
     * `undefined` for empty space or an occluding primitive.
     *
     * @returns {PickScope[]|undefined}
     */
    pickPathAt(x, y) {
      return this.pick(x, y)?.path;
    }
    /**
     * Everything the markup knows about the point: the row id if the primitive
     * carries one, and the scope chain it sits in.
     *
     * The two halves are independent, which is the whole picking model rather
     * than a quirk here. A geom mark carries an id and no meaningful scope
     * below the geom; a tick label carries no id at all — chrome has none to
     * carry — and is identified entirely by its `part` / `item` scopes.
     *
     * `undefined` for empty space, and for a point covered by a primitive
     * drawn with `PickId::Block`: an opaque panel occludes what is under it
     * without being interactive.
     *
     * @returns {{ id?: number, path: PickScope[], element: Element }|undefined}
     */
    pick(x, y) {
      const el = this._elementAt(x, y);
      if (!el) return void 0;
      if (el.closest("[data-pick-block]")) return void 0;
      const idEl = el.closest("[data-pick-id]");
      const path = this._scopesOf(el);
      if (!idEl && path.length === 0) return void 0;
      const id = idEl ? Number(idEl.getAttribute("data-pick-id")) : NaN;
      return {
        id: Number.isFinite(id) ? id : void 0,
        path,
        element: el
      };
    }
    /** The `data-pick-kind` groups enclosing an element, outermost first. */
    _scopesOf(el) {
      const scopes = [];
      for (let n = el; n && n !== this.container; n = n.parentElement) {
        const kind = n.getAttribute?.("data-pick-kind");
        if (kind == null) continue;
        const index = n.getAttribute("data-pick-index");
        scopes.push({
          kind,
          name: n.getAttribute("data-pick-name") ?? void 0,
          index: index === null ? void 0 : Number(index)
        });
      }
      return scopes.reverse();
    }
    /**
     * The size and dpi the document's writer recorded, if any.
     *
     * Advisory. Useful as an aspect ratio for a container that must be sized
     * before the plot is laid out — which is what the `aspect` option is for.
     */
    hints() {
      return {
        width: this.plot.hintWidth(),
        height: this.plot.hintHeight(),
        dpi: this.plot.hintDpi()
      };
    }
    /** Detach observers and release the wasm-side document. */
    free() {
      if (this._freed) return;
      this._freed = true;
      if (this._frame !== null) cancelAnimationFrame(this._frame);
      this._observer?.disconnect();
      this._detachMedia();
      this.plot.free();
    }
    /**
     * The topmost element of this view's markup at a container-relative point.
     *
     * `elementFromPoint` takes viewport coordinates and answers about the whole
     * page, so the result is checked to be inside this container — an overlay
     * or a neighbouring view would otherwise be reported as a hit.
     */
    _elementAt(x, y) {
      if (this._freed) return null;
      const rect = this.container.getBoundingClientRect();
      const el = document.elementFromPoint(rect.left + x, rect.top + y);
      return el && this.container.contains(el) ? el : null;
    }
    _onObserved(entries) {
      if (this._freed) return;
      const box = entries[entries.length - 1].contentBoxSize?.[0];
      const width = box ? box.inlineSize : this.container.clientWidth;
      const height = box ? box.blockSize : this.container.clientHeight;
      this._draw(Math.max(1, Math.round(width)), Math.max(1, Math.round(height)));
    }
    /**
     * Emit at a size and put the markup in the page.
     *
     * Skips the work when the size has not changed, which is what keeps an
     * observer callback that fires on an unrelated reflow from re-solving the
     * layout and re-parsing the document's worth of SVG.
     */
    _draw(width, height) {
      const h = this._aspect ? Math.max(1, Math.round(width / this._aspect)) : height;
      if (this._lastSize && this._lastSize[0] === width && this._lastSize[1] === h) return;
      this._emit(width, h);
    }
    _emit(width, height) {
      const render = this.plot.toSvg(width, height, this.idPrefix);
      try {
        this.warnings = render.warnings;
        this.container.innerHTML = render.svg;
        this._lastSize = [width, height];
      } finally {
        render.free();
      }
    }
    /** Draw now, dropping any frame already scheduled. */
    _renderNow() {
      if (this._freed) return;
      if (this._frame !== null) {
        cancelAnimationFrame(this._frame);
        this._frame = null;
      }
      this._sizeAndEmit();
    }
    /**
     * Coalesce a draw into the next frame.
     *
     * Safe for everything here, unlike on the canvas client: replacing markup
     * does not clear anything, so the previous plot stays on screen until the
     * new one is ready.
     */
    _schedule() {
      if (this._freed || this._frame !== null) return;
      this._frame = requestAnimationFrame(() => {
        this._frame = null;
        if (!this._freed) this._sizeAndEmit();
      });
    }
    /** Measure the container and emit unconditionally. */
    _sizeAndEmit() {
      const width = Math.max(1, Math.round(this.container.clientWidth || this.plot.hintWidth() || 1));
      const height = this._aspect ? Math.max(1, Math.round(width / this._aspect)) : Math.max(1, Math.round(this.container.clientHeight || this.plot.hintHeight() || 1));
      this._emit(width, height);
    }
    _detachMedia() {
      if (this._media && this._onMedia) {
        this._media.removeEventListener("change", this._onMedia);
      }
      this._media = null;
      this._onMedia = null;
    }
  };

  // entry.js
  var FACES = {
    regular: [400, "normal"],
    bold: [700, "normal"],
    italic: [400, "italic"],
    bolditalic: [700, "italic"]
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
    const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("gzip"));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }
  var modulePromise = null;
  function loadHep() {
    if (!modulePromise) {
      modulePromise = (async () => {
        const assets = globalThis.ggsqlHepAssets;
        if (!assets) {
          throw new Error(
            "ggsql: hephaestus assets are missing (globalThis.ggsqlHepAssets is unset)"
          );
        }
        await __wbg_init({ module_or_path: await gunzip(decodeBase64(assets.wasm)) });
        const faceCss = [];
        for (const [face, [weight, style]] of Object.entries(FACES)) {
          const bytes = await gunzip(decodeBase64(assets.fonts[face]));
          registerFont(bytes);
          const url = URL.createObjectURL(
            new Blob([bytes], { type: "font/ttf" })
          );
          faceCss.push(
            `@font-face{font-family:'Roboto';font-style:${style};font-weight:${weight};font-display:block;src:url('${url}') format('truetype');}`
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
  window.ggsqlRenderHep = async function(el, bytes) {
    await loadHep();
    const view = await PlotView.create(el, bytes, { defaultFont: false });
    for (const warning of view.warnings) {
      console.warn("ggsql:", warning);
    }
    return view;
  };
})();
