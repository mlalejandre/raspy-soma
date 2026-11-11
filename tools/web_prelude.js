/* SOMA: prelude del navegador. Se ejecuta al crear cada pagina, tras los objetos nativos (document, window, Node...). */
(function (g) {
  'use strict';
  var defs = function (o, m) { Object.keys(m).forEach(function (k) { Object.defineProperty(o, k, { value: m[k], writable: true, configurable: true, enumerable: false }); }); };
  var hide = function (o, k, v) { Object.defineProperty(o, k, { value: v, writable: true, configurable: true, enumerable: false }); };

  /* ------------------------------------------------------------ eventos */
  function report(e) { try { console.error('Uncaught ' + (e && e.stack ? e.stack : (e && e.message ? e.name + ': ' + e.message : String(e)))); } catch (_) {} }
  function EventTarget() {}
  defs(EventTarget.prototype, {
    addEventListener: function (type, fn, opts) {
      if (!fn) return;
      var cap = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
      var once = !!(opts && typeof opts === 'object' && opts.once);
      var self = this === undefined ? g : this;
      if (!self.__l) hide(self, '__l', {});
      var l = self.__l[type] || (self.__l[type] = []);
      for (var i = 0; i < l.length; i++) if (l[i].f === fn && l[i].c === cap) return;
      l.push({ f: fn, c: cap, once: once });
      if (opts && opts.signal) opts.signal.addEventListener('abort', function () { self.removeEventListener(type, fn, opts); });
    },
    removeEventListener: function (type, fn, opts) {
      var self = this === undefined ? g : this;
      var cap = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
      var l = self.__l && self.__l[type]; if (!l) return;
      for (var i = 0; i < l.length; i++) if (l[i].f === fn && l[i].c === cap) { l.splice(i, 1); return; }
    },
    dispatchEvent: function (ev) {
      var self = this === undefined ? g : this;
      ev.target = self; ev._stop = 0;
      var p = [], n = self;
      if (self === g) p = [g];
      else if (self.nodeType) { while (n) { p.push(n); n = n.parentNode; } if (p[p.length - 1] === g.document) p.push(g); }
      else p = [self];
      hide(ev, '_path', p);
      var i;
      for (i = p.length - 1; i > 0 && !ev._stop; i--) { ev.eventPhase = 1; invoke(p[i], ev, true); }
      if (!ev._stop) { ev.eventPhase = 2; invoke(p[0], ev, null); }
      if (ev.bubbles) for (i = 1; i < p.length && !ev._stop; i++) { ev.eventPhase = 3; invoke(p[i], ev, false); }
      ev.eventPhase = 0; ev.currentTarget = null;
      return !ev.defaultPrevented;
    }
  });
  function callL(t, f, ev) {
    try { if (typeof f === 'function') f.call(t, ev); else if (f && typeof f.handleEvent === 'function') f.handleEvent(ev); } catch (e) { report(e); }
  }
  function invoke(t, ev, cap) {
    ev.currentTarget = t;
    if (cap !== true) {
      var h = t['on' + ev.type];
      if (typeof h === 'function') callL(t, h, ev);
      else if (t.nodeType === 1 && t.hasAttribute && t.hasAttribute('on' + ev.type)) { var af = g.__attrfn(t, ev.type); if (af) callL(t, af, ev); }
    }
    var l = t.__l && t.__l[ev.type];
    if (!l) return;
    l = l.slice();
    for (var i = 0; i < l.length; i++) {
      var r = l[i];
      if (cap === true && !r.c) continue;
      if (cap === false && r.c) continue;
      if (r.once) t.removeEventListener(ev.type, r.f, r.c);
      callL(t, r.f, ev);
      if (ev._stop === 2) return;
    }
  }
  function Event(type, init) {
    init = init || {};
    this.type = String(type); this.bubbles = !!init.bubbles; this.cancelable = !!init.cancelable; this.composed = !!init.composed;
    this.defaultPrevented = false; this.target = null; this.currentTarget = null; this.eventPhase = 0; this.isTrusted = false;
    this.timeStamp = g.performance.now(); hide(this, '_stop', 0);
  }
  defs(Event.prototype, {
    preventDefault: function () { if (this.cancelable) this.defaultPrevented = true; },
    stopPropagation: function () { if (!this._stop) this._stop = 1; },
    stopImmediatePropagation: function () { this._stop = 2; },
    composedPath: function () { return this._path ? this._path.slice() : []; },
    initEvent: function (t, b, c) { this.type = t; this.bubbles = !!b; this.cancelable = !!c; }
  });
  Event.NONE = 0; Event.CAPTURING_PHASE = 1; Event.AT_TARGET = 2; Event.BUBBLING_PHASE = 3;
  function sub(name, fields, parent) {
    var P = parent || Event;
    var C = function (type, init) {
      P.call(this, type, init); init = init || {};
      for (var k in fields) this[k] = init[k] !== undefined ? init[k] : fields[k];
    };
    C.prototype = Object.create(P.prototype); C.prototype.constructor = C; hide(C, 'name', name); g[name] = C; return C;
  }
  var UIEvent = sub('UIEvent', { detail: 0, view: null });
  var MouseEvent = sub('MouseEvent', { screenX: 0, screenY: 0, clientX: 0, clientY: 0, pageX: 0, pageY: 0, offsetX: 0, offsetY: 0, x: 0, y: 0, button: 0, buttons: 0, ctrlKey: false, shiftKey: false, altKey: false, metaKey: false, relatedTarget: null }, UIEvent);
  sub('PointerEvent', { pointerId: 1, pointerType: 'mouse', isPrimary: true, width: 1, height: 1, pressure: 0 }, MouseEvent);
  sub('WheelEvent', { deltaX: 0, deltaY: 0, deltaZ: 0, deltaMode: 0 }, MouseEvent);
  sub('KeyboardEvent', { key: '', code: '', keyCode: 0, which: 0, charCode: 0, repeat: false, location: 0, ctrlKey: false, shiftKey: false, altKey: false, metaKey: false, isComposing: false }, UIEvent);
  sub('FocusEvent', { relatedTarget: null }, UIEvent);
  sub('InputEvent', { data: null, inputType: '', isComposing: false }, UIEvent);
  sub('CustomEvent', { detail: null });
  sub('MessageEvent', { data: null, origin: '', lastEventId: '', source: null, ports: [] });
  sub('PopStateEvent', { state: null });
  sub('HashChangeEvent', { oldURL: '', newURL: '' });
  sub('ProgressEvent', { lengthComputable: false, loaded: 0, total: 0 });
  sub('ErrorEvent', { message: '', filename: '', lineno: 0, colno: 0, error: null });
  sub('SubmitEvent', { submitter: null });
  sub('StorageEvent', { key: null, oldValue: null, newValue: null, url: '', storageArea: null });
  sub('TouchEvent', { touches: [], targetTouches: [], changedTouches: [] }, UIEvent);
  sub('AnimationEvent', { animationName: '', elapsedTime: 0 });
  sub('TransitionEvent', { propertyName: '', elapsedTime: 0 });
  sub('ClipboardEvent', { clipboardData: null });
  sub('DragEvent', { dataTransfer: null }, MouseEvent);
  g.Event = Event; g.EventTarget = EventTarget;
  if (g.Node) Object.setPrototypeOf(g.Node.prototype, EventTarget.prototype);
  Object.setPrototypeOf(g, EventTarget.prototype);
  hide(g, 'window', g); hide(g, 'self', g); hide(g, 'top', g); hide(g, 'parent', g); hide(g, 'frames', g); hide(g, 'globalThis', g);

  /* ------------------------------------------------------------ URL */
  var encQ = function (s) { return encodeURIComponent(s).replace(/%20/g, '+').replace(/[!'()~]/g, function (c) { return '%' + c.charCodeAt(0).toString(16).toUpperCase(); }); };
  var decQ = function (s) { s = s.replace(/\+/g, ' '); try { return decodeURIComponent(s); } catch (e) { return s; } };
  function URLSearchParams(init) {
    hide(this, '_l', []); hide(this, '_u', null);
    if (typeof init === 'string') {
      if (init.charAt(0) === '?') init = init.slice(1);
      var parts = init.split('&');
      for (var i = 0; i < parts.length; i++) { var p = parts[i]; if (!p) continue; var j = p.indexOf('='); this._l.push(j < 0 ? [decQ(p), ''] : [decQ(p.slice(0, j)), decQ(p.slice(j + 1))]); }
    } else if (init && typeof init === 'object') {
      if (init instanceof URLSearchParams) this._l = init._l.map(function (x) { return x.slice(); });
      else if (Array.isArray(init)) { for (var a = 0; a < init.length; a++) this._l.push([String(init[a][0]), String(init[a][1])]); }
      else Object.keys(init).forEach(function (k) { this._l.push([k, String(init[k])]); }, this);
    }
  }
  defs(URLSearchParams.prototype, {
    _upd: function () { if (this._u) { var s = this.toString(); this._u._search = s ? '?' + s : ''; } },
    append: function (k, v) { this._l.push([String(k), String(v)]); this._upd(); },
    delete: function (k) { this._l = this._l.filter(function (x) { return x[0] !== k; }); this._upd(); },
    get: function (k) { for (var i = 0; i < this._l.length; i++) if (this._l[i][0] === k) return this._l[i][1]; return null; },
    getAll: function (k) { return this._l.filter(function (x) { return x[0] === k; }).map(function (x) { return x[1]; }); },
    has: function (k) { return this._l.some(function (x) { return x[0] === k; }); },
    set: function (k, v) { k = String(k); v = String(v); var f = false; this._l = this._l.filter(function (x) { if (x[0] !== k) return true; if (f) return false; f = true; x[1] = v; return true; }); if (!f) this._l.push([k, v]); this._upd(); },
    sort: function () { this._l.sort(function (a, b) { return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0; }); this._upd(); },
    toString: function () { return this._l.map(function (x) { return encQ(x[0]) + '=' + encQ(x[1]); }).join('&'); },
    forEach: function (fn, t) { this._l.forEach(function (x) { fn.call(t, x[1], x[0], this); }, this); },
    keys: function () { return this._l.map(function (x) { return x[0]; })[Symbol.iterator](); },
    values: function () { return this._l.map(function (x) { return x[1]; })[Symbol.iterator](); },
    entries: function () { return this._l.map(function (x) { return [x[0], x[1]]; })[Symbol.iterator](); }
  });
  URLSearchParams.prototype[Symbol.iterator] = URLSearchParams.prototype.entries;
  Object.defineProperty(URLSearchParams.prototype, 'size', { get: function () { return this._l.length; } });
  var urlRe = /^([a-zA-Z][a-zA-Z0-9+.\-]*:)(?:\/\/(?:([^:@\/?#]*)(?::([^@\/?#]*))?@)?(\[[^\]]*\]|[^:\/?#]*)(?::(\d*))?)?([^?#]*)(\?[^#]*)?(#.*)?$/;
  var defPorts = { 'http:': '80', 'https:': '443', 'ftp:': '21', 'ws:': '80', 'wss:': '443' };
  function URL(url, base) {
    url = String(url);
    if (base !== undefined && base !== null) url = g.__resolve(String(base instanceof URL ? base.href : base), url);
    var m = urlRe.exec(url);
    if (!m) throw new TypeError("Invalid URL: " + url);
    hide(this, '_p', m[1].toLowerCase()); hide(this, '_user', m[2] || ''); hide(this, '_pass', m[3] || '');
    hide(this, '_h', (m[4] || '').toLowerCase()); hide(this, '_port', m[5] || ''); hide(this, '_path', m[6] || (m[4] !== undefined && /^(https?|ftp|wss?):$/.test(m[1]) ? '/' : ''));
    hide(this, '_search', m[7] && m[7] !== '?' ? m[7] : ''); hide(this, '_hash', m[8] && m[8] !== '#' ? m[8] : '');
    if (defPorts[this._p] === this._port) this._port = '';
    var sp = new URLSearchParams(this._search); sp._u = this; hide(this, '_sp', sp);
  }
  var up = function (name, get, set) { Object.defineProperty(URL.prototype, name, { get: get, set: set, enumerable: true, configurable: true }); };
  up('protocol', function () { return this._p; }, function (v) { this._p = String(v).replace(/:?$/, ':'); });
  up('username', function () { return this._user; }, function (v) { this._user = String(v); });
  up('password', function () { return this._pass; }, function (v) { this._pass = String(v); });
  up('hostname', function () { return this._h; }, function (v) { this._h = String(v); });
  up('port', function () { return this._port; }, function (v) { this._port = String(v); });
  up('host', function () { return this._h + (this._port ? ':' + this._port : ''); }, function (v) { var i = String(v).indexOf(':'); this._h = i < 0 ? String(v) : v.slice(0, i); this._port = i < 0 ? '' : v.slice(i + 1); });
  up('pathname', function () { return this._path; }, function (v) { v = String(v); this._path = v.charAt(0) === '/' ? v : '/' + v; });
  up('search', function () { return this._search; }, function (v) { v = String(v); this._search = v && v !== '?' ? (v.charAt(0) === '?' ? v : '?' + v) : ''; this._sp._l = new URLSearchParams(this._search)._l; });
  up('hash', function () { return this._hash; }, function (v) { v = String(v); this._hash = v && v !== '#' ? (v.charAt(0) === '#' ? v : '#' + v) : ''; });
  up('origin', function () { return /^(https?|ftp|wss?):$/.test(this._p) ? this._p + '//' + this.host : 'null'; }, function () {});
  up('searchParams', function () { return this._sp; }, function () {});
  up('href', function () {
    var a = this._user ? this._user + (this._pass ? ':' + this._pass : '') + '@' : '';
    var hasAuth = this._h !== '' || /^(https?|ftp|wss?|file):$/.test(this._p);
    return this._p + (hasAuth ? '//' : '') + a + this.host + this._path + this._search + this._hash;
  }, function (v) { var u = new URL(v); this._p = u._p; this._user = u._user; this._pass = u._pass; this._h = u._h; this._port = u._port; this._path = u._path; this._search = u._search; this._hash = u._hash; });
  defs(URL.prototype, { toString: function () { return this.href; }, toJSON: function () { return this.href; } });
  defs(URL, { canParse: function (u, b) { try { new URL(u, b); return true; } catch (e) { return false; } }, createObjectURL: function (b) { return g.__blobUrl(b); }, revokeObjectURL: function () {} });
  g.URL = URL; g.URLSearchParams = URLSearchParams;

  /* ------------------------------------------------------------ Blob / TextEncoder / tipos tipados */
  function Blob(parts, opts) { var s = ''; (parts || []).forEach(function (p) { s += (p && p._s !== undefined && p instanceof Blob) ? p._s : (p && p.length !== undefined && typeof p !== 'string' && p.BYTES_PER_ELEMENT ? String.fromCharCode.apply(null, Array.prototype.slice.call(p)) : String(p)); }); hide(this, '_s', s); this.size = s.length; this.type = (opts && opts.type) || ''; }
  defs(Blob.prototype, { text: function () { return Promise.resolve(this._s); }, arrayBuffer: function () { return Promise.resolve(new g.Uint8Array(this._s.split('').map(function (c) { return c.charCodeAt(0) & 255; })).buffer); }, slice: function (a, b, t) { return new Blob([this._s.slice(a, b)], { type: t || this.type }); } });
  function File(parts, name, opts) { Blob.call(this, parts, opts); this.name = name; this.lastModified = Date.now(); }
  File.prototype = Object.create(Blob.prototype); File.prototype.constructor = File;
  function FileReader() { this.result = null; this.readyState = 0; }
  FileReader.prototype = Object.create(EventTarget.prototype);
  ['readAsText', 'readAsDataURL', 'readAsArrayBuffer', 'readAsBinaryString'].forEach(function (m) {
    FileReader.prototype[m] = function (b) { var self = this; setTimeout(function () { self.readyState = 2; self.result = m === 'readAsDataURL' ? 'data:' + (b.type || 'application/octet-stream') + ';base64,' + btoa(b._s) : b._s; var ev = new ProgressEvent('load'); self.dispatchEvent(ev); var e2 = new ProgressEvent('loadend'); self.dispatchEvent(e2); }, 0); };
  });
  g.Blob = Blob; g.File = File; g.FileReader = FileReader;
  hide(g, '__blobUrl', function (b) { var id = 'blob:soma/' + (++__bid); __blobs[id] = b; return id; }); var __bid = 0, __blobs = {};
  var utf8enc = function (s) { var o = []; s = unescape(encodeURIComponent(s)); for (var i = 0; i < s.length; i++) o.push(s.charCodeAt(i) & 255); return o; };
  var utf8dec = function (a) { var s = ''; for (var i = 0; i < a.length; i++) s += String.fromCharCode(a[i] & 255); try { return decodeURIComponent(escape(s)); } catch (e) { return s; } };
  function mkTyped(name, bytes, conv) {
    var C = function (a, b, c) {
      var arr = [];
      if (typeof a === 'number') { for (var i = 0; i < a; i++) arr.push(0); }
      else if (a && a.byteLength !== undefined && a.length === undefined) { for (var j = 0; j < a.byteLength / bytes; j++) arr.push(0); }
      else if (a && a.length !== undefined) { for (var k = 0; k < a.length; k++) arr.push(conv(a[k])); }
      for (var q = 0; q < arr.length; q++) this[q] = arr[q];
      hide(this, 'length', arr.length); hide(this, 'buffer', (a && a.byteLength !== undefined && a.length === undefined) ? a : new ArrayBuffer(arr.length * bytes));
      hide(this, 'byteLength', arr.length * bytes); hide(this, 'byteOffset', 0);
    };
    C.prototype = Object.create(Array.prototype); C.prototype.constructor = C; C.BYTES_PER_ELEMENT = bytes; hide(C.prototype, 'BYTES_PER_ELEMENT', bytes);
    defs(C.prototype, {
      set: function (src, off) { off = off || 0; for (var i = 0; i < src.length; i++) this[off + i] = conv(src[i]); },
      subarray: function (a, b) { return new C(Array.prototype.slice.call(this, a, b)); },
      slice: function (a, b) { return new C(Array.prototype.slice.call(this, a, b)); },
      fill: function (v, a, b) { a = a || 0; b = b === undefined ? this.length : b; for (var i = a; i < b; i++) this[i] = conv(v); return this; },
      map: function (f, t) { var r = new C(this.length); for (var i = 0; i < this.length; i++) r[i] = conv(f.call(t, this[i], i, this)); return r; }
    });
    Object.defineProperty(C.prototype, 'length', { get: function () { return this.__n; }, set: function (v) { hide(this, '__n', v); }, configurable: true });
    C.from = function (a, f) { var r = []; for (var i = 0; i < a.length; i++) r.push(f ? f(a[i], i) : a[i]); return new C(r); };
    C.of = function () { return new C(Array.prototype.slice.call(arguments)); };
    g[name] = C; return C;
  }
  function ArrayBuffer(n) { this.byteLength = n | 0; }
  defs(ArrayBuffer.prototype, { slice: function (a, b) { return new ArrayBuffer(Math.max(0, (b === undefined ? this.byteLength : b) - (a || 0))); } });
  ArrayBuffer.isView = function (v) { return !!(v && v.BYTES_PER_ELEMENT); };
  g.ArrayBuffer = ArrayBuffer; g.SharedArrayBuffer = ArrayBuffer;
  mkTyped('Uint8Array', 1, function (v) { return v & 255; }); mkTyped('Int8Array', 1, function (v) { return (v << 24) >> 24; });
  mkTyped('Uint8ClampedArray', 1, function (v) { v = Math.round(v); return v < 0 ? 0 : v > 255 ? 255 : v | 0; });
  mkTyped('Uint16Array', 2, function (v) { return v & 65535; }); mkTyped('Int16Array', 2, function (v) { return (v << 16) >> 16; });
  mkTyped('Uint32Array', 4, function (v) { return v >>> 0; }); mkTyped('Int32Array', 4, function (v) { return v | 0; });
  mkTyped('Float32Array', 4, function (v) { return +v; }); mkTyped('Float64Array', 8, function (v) { return +v; });
  function TextEncoder() { this.encoding = 'utf-8'; } TextEncoder.prototype.encode = function (s) { return new g.Uint8Array(utf8enc(String(s === undefined ? '' : s))); };
  function TextDecoder(l) { this.encoding = (l || 'utf-8').toLowerCase(); } TextDecoder.prototype.decode = function (a) { return a ? utf8dec(a.length !== undefined ? a : []) : ''; };
  g.TextEncoder = TextEncoder; g.TextDecoder = TextDecoder;

  /* ------------------------------------------------------------ red: Headers / Response / fetch / XHR */
  function Headers(init) {
    hide(this, '_m', {});
    if (init) { if (init instanceof Headers) init.forEach(function (v, k) { this.set(k, v); }, this); else if (Array.isArray(init)) init.forEach(function (p) { this.append(p[0], p[1]); }, this); else Object.keys(init).forEach(function (k) { this.set(k, init[k]); }, this); }
  }
  defs(Headers.prototype, {
    append: function (k, v) { k = String(k).toLowerCase(); this._m[k] = this._m[k] !== undefined ? this._m[k] + ', ' + v : String(v); },
    set: function (k, v) { this._m[String(k).toLowerCase()] = String(v); },
    get: function (k) { var v = this._m[String(k).toLowerCase()]; return v === undefined ? null : v; },
    has: function (k) { return this._m[String(k).toLowerCase()] !== undefined; },
    delete: function (k) { delete this._m[String(k).toLowerCase()]; },
    forEach: function (fn, t) { Object.keys(this._m).forEach(function (k) { fn.call(t, this._m[k], k, this); }, this); },
    keys: function () { return Object.keys(this._m)[Symbol.iterator](); },
    values: function () { var m = this._m; return Object.keys(m).map(function (k) { return m[k]; })[Symbol.iterator](); },
    entries: function () { var m = this._m; return Object.keys(m).map(function (k) { return [k, m[k]]; })[Symbol.iterator](); }
  });
  Headers.prototype[Symbol.iterator] = Headers.prototype.entries;
  function bodyOf(b) {
    if (b === undefined || b === null) return ['', null];
    if (typeof b === 'string') return [b, 'text/plain;charset=UTF-8'];
    if (b instanceof URLSearchParams) return [b.toString(), 'application/x-www-form-urlencoded;charset=UTF-8'];
    if (b instanceof Blob) return [b._s, b.type || null];
    if (g.FormData && b instanceof g.FormData) return [b._encode(), 'application/x-www-form-urlencoded;charset=UTF-8'];
    if (b.BYTES_PER_ELEMENT) return [utf8dec(b), null];
    return [String(b), 'text/plain;charset=UTF-8'];
  }
  function Body() {}
  defs(Body.prototype, {
    text: function () { if (this.bodyUsed) return Promise.reject(new TypeError('Body already used')); this.bodyUsed = true; return Promise.resolve(this._b); },
    json: function () { var self = this; return this.text().then(function (t) { return JSON.parse(t); }); },
    arrayBuffer: function () { return this.text().then(function (t) { return new g.Uint8Array(utf8enc(t)).buffer; }); },
    blob: function () { var ty = this.headers.get('content-type') || ''; return this.text().then(function (t) { return new Blob([t], { type: ty }); }); },
    formData: function () { return this.text().then(function (t) { var f = new g.FormData(); new URLSearchParams(t).forEach(function (v, k) { f.append(k, v); }); return f; }); }
  });
  function Response(body, init) {
    init = init || {}; var b = bodyOf(body);
    hide(this, '_b', b[0]); this.bodyUsed = false; this.status = init.status === undefined ? 200 : init.status; this.statusText = init.statusText || '';
    this.ok = this.status >= 200 && this.status < 300; this.headers = new Headers(init.headers); this.url = init.url || ''; this.type = 'basic'; this.redirected = false;
    if (b[1] && !this.headers.has('content-type')) this.headers.set('content-type', b[1]); this.body = null;
  }
  Response.prototype = Object.create(Body.prototype); Response.prototype.constructor = Response;
  defs(Response.prototype, { clone: function () { var r = new Response(this._b, { status: this.status, statusText: this.statusText, headers: this.headers, url: this.url }); return r; } });
  Response.error = function () { var r = new Response(null, { status: 0 }); r.type = 'error'; return r; };
  Response.json = function (d, init) { var r = new Response(JSON.stringify(d), init); r.headers.set('content-type', 'application/json'); return r; };
  Response.redirect = function (u, s) { return new Response(null, { status: s || 302, headers: { location: u } }); };
  function Request(input, init) {
    init = init || {}; var src = input instanceof Request ? input : null;
    this.url = src ? src.url : g.__resolve(g.location.href, String(input)); this.method = String(init.method || (src && src.method) || 'GET').toUpperCase();
    this.headers = new Headers(init.headers || (src && src.headers)); var b = bodyOf(init.body !== undefined ? init.body : (src ? src._b : undefined));
    hide(this, '_b', b[0]); if (b[1] && !this.headers.has('content-type')) this.headers.set('content-type', b[1]);
    this.credentials = init.credentials || 'same-origin'; this.mode = init.mode || 'cors'; this.cache = init.cache || 'default'; this.redirect = init.redirect || 'follow'; this.referrer = init.referrer || ''; this.signal = init.signal || null; this.bodyUsed = false;
  }
  Request.prototype = Object.create(Body.prototype); Request.prototype.constructor = Request;
  defs(Request.prototype, { clone: function () { return new Request(this); } });
  function doFetch(method, url, body, ctype) {
    var r = g.__fetch(method, url, body, ctype || '');
    if (!r) throw new TypeError('Failed to fetch');
    return r;
  }
  g.fetch = function (input, init) {
    return new Promise(function (resolve, reject) {
      var req;
      try { req = new Request(input, init); } catch (e) { reject(e); return; }
      if (req.signal && req.signal.aborted) { reject(new DOMException('The operation was aborted.', 'AbortError')); return; }
      var u = req.url;
      if (u.indexOf('blob:soma/') === 0) { var bl = __blobs[u]; resolve(new Response(bl ? bl._s : '', { status: bl ? 200 : 404, url: u, headers: { 'content-type': bl ? bl.type : '' } })); return; }
      if (u.indexOf('data:') === 0) { var m = /^data:([^,;]*)(;base64)?,(.*)$/.exec(u); if (!m) { reject(new TypeError('Failed to fetch')); return; } var d = m[2] ? atob(m[3]) : decodeURIComponent(m[3]); resolve(new Response(d, { url: u, headers: { 'content-type': m[1] || 'text/plain' } })); return; }
      setTimeout(function () {
        var r;
        try { r = doFetch(req.method, u, req.method === 'GET' || req.method === 'HEAD' ? '' : req._b, req.headers.get('content-type')); } catch (e) { reject(e); return; }
        var res = new Response(r.body, { status: r.status, statusText: r.status === 200 ? 'OK' : '', url: r.url || u, headers: { 'content-type': r.ctype || '' } });
        resolve(res);
      }, 0);
    });
  };
  g.Headers = Headers; g.Response = Response; g.Request = Request;
  function XHR() { this.readyState = 0; this.status = 0; this.statusText = ''; this.responseText = ''; this.response = ''; this.responseType = ''; this.responseURL = ''; this.timeout = 0; this.withCredentials = false; this.upload = new EventTarget(); hide(this, '_h', {}); hide(this, '_rh', {}); }
  XHR.prototype = Object.create(EventTarget.prototype); XHR.prototype.constructor = XHR;
  XHR.UNSENT = 0; XHR.OPENED = 1; XHR.HEADERS_RECEIVED = 2; XHR.LOADING = 3; XHR.DONE = 4;
  function xfire(x, type) { var ev = new ProgressEvent(type, { loaded: x.responseText.length, total: x.responseText.length }); ev.target = x; x.dispatchEvent(ev); }
  defs(XHR.prototype, {
    open: function (m, u, async) { hide(this, '_m', String(m).toUpperCase()); hide(this, '_u', g.__resolve(g.location.href, String(u))); hide(this, '_async', async !== false); this.readyState = 1; xfire(this, 'readystatechange'); },
    setRequestHeader: function (k, v) { this._h[String(k).toLowerCase()] = String(v); },
    getResponseHeader: function (k) { var v = this._rh[String(k).toLowerCase()]; return v === undefined ? null : v; },
    getAllResponseHeaders: function () { var o = ''; for (var k in this._rh) o += k + ': ' + this._rh[k] + '\r\n'; return o; },
    overrideMimeType: function () {}, abort: function () { hide(this, '_abort', true); },
    send: function (body) {
      var self = this, b = bodyOf(body);
      var run = function () {
        if (self._abort) return;
        var r; try { r = doFetch(self._m, self._u, self._m === 'GET' || self._m === 'HEAD' ? '' : b[0], self._h['content-type'] || b[1] || ''); } catch (e) { self.readyState = 4; self.status = 0; xfire(self, 'readystatechange'); xfire(self, 'error'); xfire(self, 'loadend'); return; }
        self.status = r.status; self.statusText = r.status === 200 ? 'OK' : ''; self.responseURL = r.url || self._u; self._rh['content-type'] = r.ctype || '';
        self.readyState = 2; xfire(self, 'readystatechange'); self.readyState = 3; xfire(self, 'readystatechange');
        self.responseText = r.body; self.response = r.body;
        if (self.responseType === 'json') { try { self.response = JSON.parse(r.body); } catch (e) { self.response = null; } }
        else if (self.responseType === 'arraybuffer') self.response = new g.Uint8Array(utf8enc(r.body)).buffer;
        else if (self.responseType === 'blob') self.response = new Blob([r.body], { type: r.ctype });
        self.readyState = 4; xfire(self, 'readystatechange'); xfire(self, 'load'); xfire(self, 'loadend');
      };
      if (this._async) setTimeout(run, 0); else run();
    }
  });
  g.XMLHttpRequest = XHR;
  function FormData(form) {
    hide(this, '_l', []);
    if (form && form.elements) for (var i = 0; i < form.elements.length; i++) { var e = form.elements[i]; if (!e.name || e.disabled) continue; var t = (e.type || '').toLowerCase(); if ((t === 'checkbox' || t === 'radio') && !e.checked) continue; if (t === 'submit' || t === 'button' || t === 'file' || t === 'reset') continue; if (e.tagName === 'SELECT' && e.multiple) { for (var j = 0; j < e.options.length; j++) if (e.options[j].selected) this._l.push([e.name, e.options[j].value]); continue; } this._l.push([e.name, e.value]); }
  }
  FormData.prototype = Object.create(URLSearchParams.prototype); FormData.prototype.constructor = FormData;
  defs(FormData.prototype, { _encode: function () { return this.toString(); }, _upd: function () {} });
  g.FormData = FormData;
  function AbortSignal() { this.aborted = false; this.reason = undefined; }
  AbortSignal.prototype = Object.create(EventTarget.prototype); AbortSignal.prototype.constructor = AbortSignal;
  AbortSignal.prototype.throwIfAborted = function () { if (this.aborted) throw this.reason; };
  AbortSignal.abort = function (r) { var s = new AbortSignal(); s.aborted = true; s.reason = r; return s; };
  AbortSignal.timeout = function (ms) { var c = new AbortController(); setTimeout(function () { c.abort(new DOMException('Timeout', 'TimeoutError')); }, ms); return c.signal; };
  function AbortController() { this.signal = new AbortSignal(); }
  AbortController.prototype.abort = function (r) { var s = this.signal; if (s.aborted) return; s.aborted = true; s.reason = r === undefined ? new DOMException('The operation was aborted.', 'AbortError') : r; var ev = new Event('abort'); s.dispatchEvent(ev); };
  g.AbortController = AbortController; g.AbortSignal = AbortSignal;
  function DOMException(msg, name) { this.message = msg || ''; this.name = name || 'Error'; this.code = 0; }
  DOMException.prototype = Object.create(Error.prototype); DOMException.prototype.constructor = DOMException; g.DOMException = DOMException;

  /* ------------------------------------------------------------ location / history / navigator / varios */
  var locAcc = function (name, get, set) { Object.defineProperty(g.location, name, { get: get, set: set, enumerable: true, configurable: true }); };
  var curUrl = function () { return new URL(g.__url()); };
  g.location = {};
  locAcc('href', function () { return g.__url(); }, function (v) { g.__nav(g.__resolve(g.__url(), String(v)), 0); });
  ['protocol', 'host', 'hostname', 'port', 'pathname', 'search', 'hash', 'origin'].forEach(function (k) {
    locAcc(k, function () { return curUrl()[k]; }, k === 'origin' ? function () {} : function (v) { var u = curUrl(); u[k] = v; g.__nav(u.href, 0); });
  });
  defs(g.location, {
    assign: function (u) { g.__nav(g.__resolve(g.__url(), String(u)), 0); }, replace: function (u) { g.__nav(g.__resolve(g.__url(), String(u)), 1); },
    reload: function () { g.__nav(g.__url(), 4); }, toString: function () { return g.__url(); }, ancestorOrigins: []
  });
  if (g.document) Object.defineProperty(g.document, 'location', { get: function () { return g.location; }, set: function (v) { g.location.href = v; }, configurable: true });
  var hstate = null, hlen = 1;
  g.history = {
    get length() { return hlen; }, get state() { return hstate; }, scrollRestoration: 'auto',
    pushState: function (s, t, u) { hstate = s === undefined ? null : s; hlen++; if (u !== undefined && u !== null) g.__nav(g.__resolve(g.__url(), String(u)), 2); },
    replaceState: function (s, t, u) { hstate = s === undefined ? null : s; if (u !== undefined && u !== null) g.__nav(g.__resolve(g.__url(), String(u)), 3); },
    back: function () { g.__hist(-1); }, forward: function () { g.__hist(1); }, go: function (d) { g.__hist(d | 0); }
  };
  g.navigator = {
    userAgent: g.__ua(), appName: 'Netscape', appCodeName: 'Mozilla', appVersion: '5.0 (SOMA)', platform: 'SOMA', product: 'Gecko', vendor: 'SOMA', language: 'es-ES', languages: ['es-ES', 'es', 'en'],
    onLine: true, cookieEnabled: true, hardwareConcurrency: 2, deviceMemory: 1, maxTouchPoints: 0, doNotTrack: null, webdriver: false, pdfViewerEnabled: false,
    sendBeacon: function (u, d) { try { doFetch('POST', g.__resolve(g.location.href, u), bodyOf(d)[0], 'text/plain'); return true; } catch (e) { return false; } },
    clipboard: { writeText: function () { return Promise.resolve(); }, readText: function () { return Promise.resolve(''); } },
    permissions: { query: function () { return Promise.resolve({ state: 'denied', addEventListener: function () {} }); } },
    vibrate: function () { return false; }, javaEnabled: function () { return false; }, getBattery: function () { return Promise.reject(new Error('no disponible')); }
  };
  g.screen = { width: g.innerWidth, height: g.innerHeight, availWidth: g.innerWidth, availHeight: g.innerHeight, colorDepth: 24, pixelDepth: 24, orientation: { type: 'landscape-primary', angle: 0, addEventListener: function () {} } };
  var t0 = g.__now();
  g.performance = { now: function () { return g.__now() - t0; }, timeOrigin: Date.now(), mark: function () {}, measure: function () {}, getEntries: function () { return []; }, getEntriesByType: function () { return []; }, getEntriesByName: function () { return []; }, clearMarks: function () {}, clearMeasures: function () {}, timing: { navigationStart: Date.now() }, navigation: { type: 0 }, toJSON: function () { return {}; } };
  g.crypto = {
    getRandomValues: function (a) { for (var i = 0; i < a.length; i++) a[i] = Math.floor(Math.random() * 4294967296); return a; },
    randomUUID: function () { return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function (c) { var r = Math.random() * 16 | 0; return (c === 'x' ? r : (r & 3 | 8)).toString(16); }); },
    subtle: {}
  };
  g.matchMedia = function (q) {
    q = String(q); var w = g.innerWidth, h = g.innerHeight, ok = true;
    var parts = q.split(/\band\b/);
    for (var i = 0; i < parts.length; i++) {
      var m = /\(\s*(min|max)-(width|height)\s*:\s*([\d.]+)(px|em|rem)?\s*\)/.exec(parts[i]);
      if (m) { var v = parseFloat(m[3]) * (m[4] === 'em' || m[4] === 'rem' ? 16 : 1), d = m[2] === 'width' ? w : h; if (m[1] === 'min' ? d < v : d > v) ok = false; continue; }
      if (/prefers-color-scheme\s*:\s*dark|prefers-reduced-motion\s*:\s*reduce|hover\s*:\s*none|pointer\s*:\s*(coarse|none)|print|orientation\s*:\s*portrait/.test(parts[i])) { if (!/not\s/.test(parts[i])) ok = false; }
    }
    var mq = { matches: ok, media: q, onchange: null, addListener: function () {}, removeListener: function () {}, addEventListener: function () {}, removeEventListener: function () {}, dispatchEvent: function () { return true; } };
    return mq;
  };
  function Obs(name, onobserve) {
    var C = function (cb) { hide(this, '_cb', cb); hide(this, '_t', []); };
    defs(C.prototype, { observe: function (t) { this._t.push(t); var self = this; if (onobserve) setTimeout(function () { try { self._cb(onobserve(t, self), self); } catch (e) { report(e); } }, 0); }, unobserve: function () {}, disconnect: function () { this._t = []; }, takeRecords: function () { return []; } });
    g[name] = C;
  }
  Obs('MutationObserver', null);
  var rect = function (t) { return t.getBoundingClientRect(); };
  Obs('IntersectionObserver', function (t) { var r = rect(t); return [{ target: t, isIntersecting: true, intersectionRatio: 1, boundingClientRect: r, intersectionRect: r, rootBounds: null, time: g.performance.now() }]; });
  Obs('ResizeObserver', function (t) { var r = rect(t); return [{ target: t, contentRect: r, borderBoxSize: [{ inlineSize: r.width, blockSize: r.height }], contentBoxSize: [{ inlineSize: r.width, blockSize: r.height }] }]; });
  Obs('PerformanceObserver', null);
  g.customElements = { _d: {}, define: function (n, c) { this._d[n] = c; }, get: function (n) { return this._d[n]; }, whenDefined: function (n) { return Promise.resolve(this._d[n]); }, upgrade: function () {} };
  g.Intl = g.Intl || {
    NumberFormat: function (l, o) { this.format = function (n) { return (+n).toLocaleString(l, o); }; this.resolvedOptions = function () { return { locale: 'en-US' }; }; },
    DateTimeFormat: function (l, o) { this.format = function (d) { d = d || new Date(); return d.getUTCFullYear() + '-' + ('0' + (d.getUTCMonth() + 1)).slice(-2) + '-' + ('0' + d.getUTCDate()).slice(-2); }; this.resolvedOptions = function () { return { locale: 'en-US', timeZone: 'UTC' }; }; },
    Collator: function () { this.compare = function (a, b) { return a < b ? -1 : a > b ? 1 : 0; }; },
    PluralRules: function () { this.select = function (n) { return n === 1 ? 'one' : 'other'; }; },
    RelativeTimeFormat: function () { this.format = function (v, u) { return v + ' ' + u; }; },
    ListFormat: function () { this.format = function (a) { return a.join(', '); }; },
    Segmenter: function () { this.segment = function (s) { return String(s).split(''); }; }
  };
  g.DOMParser = function () {};
  g.DOMParser.prototype.parseFromString = function (html) {
    var d = g.document.createElement('div'); d.innerHTML = String(html);
    return { body: d, documentElement: d, head: d, title: '', querySelector: function (s) { return d.querySelector(s); }, querySelectorAll: function (s) { return d.querySelectorAll(s); }, getElementById: function (i) { return d.querySelector('#' + i); }, getElementsByTagName: function (t) { return d.getElementsByTagName(t); }, childNodes: d.childNodes };
  };
  g.XMLSerializer = function () {}; g.XMLSerializer.prototype.serializeToString = function (n) { return n.outerHTML; };
  g.Worker = function () { throw new Error('Worker no esta soportado en este navegador'); };
  g.WebSocket = function (u) { var self = this; this.url = u; this.readyState = 3; this.protocol = ''; this.bufferedAmount = 0; setTimeout(function () { var e = new Event('error'); self.dispatchEvent(e); var c = new Event('close'); self.dispatchEvent(c); }, 0); };
  g.WebSocket.prototype = Object.create(EventTarget.prototype); g.WebSocket.prototype.send = function () { throw new DOMException('WebSocket no conectado', 'InvalidStateError'); }; g.WebSocket.prototype.close = function () {};
  g.WebSocket.CONNECTING = 0; g.WebSocket.OPEN = 1; g.WebSocket.CLOSING = 2; g.WebSocket.CLOSED = 3;
  g.EventSource = function () { var self = this; this.readyState = 2; setTimeout(function () { var e = new Event('error'); }, 0); }; g.EventSource.prototype = Object.create(EventTarget.prototype); g.EventSource.prototype.close = function () {};
  g.BroadcastChannel = function (n) { this.name = n; }; g.BroadcastChannel.prototype = Object.create(EventTarget.prototype); g.BroadcastChannel.prototype.postMessage = function () {}; g.BroadcastChannel.prototype.close = function () {};
  g.MessageChannel = function () { var a = new EventTarget(), b = new EventTarget(); a.postMessage = function (d) { setTimeout(function () { b.dispatchEvent(new MessageEvent('message', { data: d })); }, 0); }; b.postMessage = function (d) { setTimeout(function () { a.dispatchEvent(new MessageEvent('message', { data: d })); }, 0); }; this.port1 = a; this.port2 = b; };
  g.postMessage = function (d, origin) { setTimeout(function () { var e = new MessageEvent('message', { data: d, origin: g.location.origin, source: g }); g.dispatchEvent(e); }, 0); };
  g.Notification = function () {}; g.Notification.permission = 'denied'; g.Notification.requestPermission = function () { return Promise.resolve('denied'); };
  g.alert = function (m) { g.__alert(String(m === undefined ? '' : m)); };
  g.confirm = function (m) { g.__alert(String(m)); return false; };
  g.prompt = function (m, d) { g.__alert(String(m)); return null; };
  g.open = function (u) { if (u) g.__nav(g.__resolve(g.__url(), String(u)), 0); return null; };
  g.close = function () {}; g.focus = function () {}; g.blur = function () {}; g.stop = function () {}; g.print = function () {};
  g.getSelection = function () { return { rangeCount: 0, toString: function () { return ''; }, removeAllRanges: function () {}, addRange: function () {}, getRangeAt: function () { return null; }, isCollapsed: true, anchorNode: null }; };
  g.scrollTo = g.scroll = function (x, y) { if (x && typeof x === 'object') { y = x.top; x = x.left; } g.__scrollTo(x | 0, y | 0); };
  g.scrollBy = function (x, y) { if (x && typeof x === 'object') { y = x.top; x = x.left; } g.__scrollTo(g.scrollX + (x | 0), g.scrollY + (y | 0)); };
  g.getComputedStyle = function (el) { return g.__cstyle(el); };
  g.requestAnimationFrame = g.requestAnimationFrame || function (f) { return setTimeout(function () { f(g.performance.now()); }, 16); };
  g.cancelAnimationFrame = g.cancelAnimationFrame || g.clearTimeout;
  g.reportError = report; g.addEventListener = EventTarget.prototype.addEventListener; g.removeEventListener = EventTarget.prototype.removeEventListener; g.dispatchEvent = EventTarget.prototype.dispatchEvent;
  g.CSS = { supports: function () { return false; }, escape: function (s) { return String(s).replace(/[^\w-]/g, function (c) { return '\\' + c; }); } };
  g.WebAssembly = undefined;
})(globalThis);
/* ------------------------------------------------------------ extras de DOM escritos en JS */
(function (g) {
  'use strict';
  var defs = function (o, m) { Object.keys(m).forEach(function (k) { Object.defineProperty(o, k, { value: m[k], writable: true, configurable: true, enumerable: false }); }); };
  var doc = g.document, EP = g.Element.prototype, NP = g.Node.prototype, DP = g.Document.prototype;
  ['window', 'self', 'top', 'parent', 'frames'].forEach(function (k) { Object.defineProperty(g, k, { value: g, writable: true, configurable: true, enumerable: false }); });
  g.opener = null; g.name = ''; g.closed = false; g.length = 0; g.origin = g.location.origin; g.isSecureContext = false; g.frameElement = null;
  g.external = {}; g.visualViewport = { width: g.innerWidth, height: g.innerHeight, scale: 1, offsetLeft: 0, offsetTop: 0, pageLeft: 0, pageTop: 0, addEventListener: function () {}, removeEventListener: function () {} };
  g.requestIdleCallback = function (f) { return setTimeout(function () { f({ didTimeout: false, timeRemaining: function () { return 10; } }); }, 1); };
  g.cancelIdleCallback = g.clearTimeout;
  g.queueMicrotask = g.queueMicrotask || function (f) { Promise.resolve().then(f); };

  var nodeC = { ELEMENT_NODE: 1, ATTRIBUTE_NODE: 2, TEXT_NODE: 3, CDATA_SECTION_NODE: 4, PROCESSING_INSTRUCTION_NODE: 7, COMMENT_NODE: 8, DOCUMENT_NODE: 9, DOCUMENT_TYPE_NODE: 10, DOCUMENT_FRAGMENT_NODE: 11,
    DOCUMENT_POSITION_DISCONNECTED: 1, DOCUMENT_POSITION_PRECEDING: 2, DOCUMENT_POSITION_FOLLOWING: 4, DOCUMENT_POSITION_CONTAINS: 8, DOCUMENT_POSITION_CONTAINED_BY: 16 };
  Object.keys(nodeC).forEach(function (k) { Object.defineProperty(g.Node, k, { value: nodeC[k] }); Object.defineProperty(NP, k, { value: nodeC[k] }); });

  function toFrag(list) {
    var f = doc.createDocumentFragment();
    for (var i = 0; i < list.length; i++) { var a = list[i]; f.appendChild(a instanceof g.Node ? a : doc.createTextNode(String(a))); }
    return f;
  }
  defs(NP, {
    remove: function () { if (this.parentNode) this.parentNode.removeChild(this); },
    before: function () { var p = this.parentNode; if (p) p.insertBefore(toFrag(arguments), this); },
    after: function () { var p = this.parentNode; if (p) p.insertBefore(toFrag(arguments), this.nextSibling); },
    replaceWith: function () { var p = this.parentNode; if (!p) return; var f = toFrag(arguments); var n = this.nextSibling; p.removeChild(this); p.insertBefore(f, n); },
    isEqualNode: function (o) { return !!o && this.nodeType === o.nodeType && this.nodeName === o.nodeName && (this.nodeType === 1 ? this.outerHTML === o.outerHTML : this.nodeValue === o.nodeValue); },
    compareDocumentPosition: function (o) {
      if (o === this) return 0;
      if (this.contains(o)) return 20;
      if (o.contains && o.contains(this)) return 10;
      var all = doc.querySelectorAll('*'), a = -1, b = -1;
      for (var i = 0; i < all.length; i++) { if (all[i] === this) a = i; if (all[i] === o) b = i; }
      if (a < 0 || b < 0) return 1;
      return b > a ? 4 : 2;
    },
    lookupNamespaceURI: function () { return null; }, isDefaultNamespace: function () { return true; }
  });
  defs(EP, {
    append: function () { this.appendChild(toFrag(arguments)); },
    prepend: function () { this.insertBefore(toFrag(arguments), this.firstChild); },
    replaceChildren: function () { while (this.firstChild) this.removeChild(this.firstChild); this.appendChild(toFrag(arguments)); },
    insertAdjacentHTML: function (pos, html) { this.__ins(pos, g.__parseFrag(String(html))); },
    insertAdjacentElement: function (pos, el) { this.__ins(pos, el); return el; },
    insertAdjacentText: function (pos, t) { this.__ins(pos, doc.createTextNode(String(t))); },
    __ins: function (pos, n) {
      var p = this.parentNode;
      switch (String(pos).toLowerCase()) {
        case 'beforebegin': if (!p) throw new DOMException('Sin padre', 'NoModificationAllowedError'); p.insertBefore(n, this); break;
        case 'afterbegin': this.insertBefore(n, this.firstChild); break;
        case 'beforeend': this.appendChild(n); break;
        case 'afterend': if (!p) throw new DOMException('Sin padre', 'NoModificationAllowedError'); p.insertBefore(n, this.nextSibling); break;
        default: throw new DOMException('Posicion no valida: ' + pos, 'SyntaxError');
      }
    },
    toggleAttribute: function (n, force) { var has = this.hasAttribute(n); if (force === undefined) force = !has; if (force && !has) this.setAttribute(n, ''); else if (!force && has) this.removeAttribute(n); return !!force; },
    click: function () {
      if (this.disabled) return;
      var ev = new g.MouseEvent('click', { bubbles: true, cancelable: true, view: g });
      if (this.dispatchEvent(ev)) g.__activate(this);
    },
    focus: function () {
      var old = doc.activeElement;
      if (old === this) return;
      if (old && old !== doc.body) { g.__focus(null); old.dispatchEvent(new g.FocusEvent('blur', { relatedTarget: this })); old.dispatchEvent(new g.FocusEvent('focusout', { bubbles: true, relatedTarget: this })); }
      g.__focus(this);
      this.dispatchEvent(new g.FocusEvent('focus', { relatedTarget: old })); this.dispatchEvent(new g.FocusEvent('focusin', { bubbles: true, relatedTarget: old }));
    },
    blur: function () {
      if (doc.activeElement !== this) return;
      g.__focus(null);
      this.dispatchEvent(new g.FocusEvent('blur')); this.dispatchEvent(new g.FocusEvent('focusout', { bubbles: true }));
    },
    requestSubmit: function (btn) {
      var ev = new g.SubmitEvent('submit', { bubbles: true, cancelable: true, submitter: btn || null });
      if (this.dispatchEvent(ev)) this.submit();
    },
    scrollIntoView: function () { var r = this.getBoundingClientRect(); g.__scrollTo(g.scrollX, Math.max(0, g.scrollY + r.top - 16)); },
    scrollIntoViewIfNeeded: function () { this.scrollIntoView(); },
    scrollTo: function (x, y) { if (x && typeof x === 'object') { y = x.top; x = x.left; } if (x !== undefined) this.scrollLeft = x; if (y !== undefined) this.scrollTop = y; },
    scrollBy: function (x, y) { if (x && typeof x === 'object') { y = x.top; x = x.left; } this.scrollLeft += x | 0; this.scrollTop += y | 0; },
    scroll: function (x, y) { this.scrollTo(x, y); },
    getClientRects: function () { return [this.getBoundingClientRect()]; },
    getAnimations: function () { return []; },
    animate: function () {
      var self = this, a = new g.EventTarget(); a.playState = 'finished'; a.currentTime = 0; a.effect = null; a.pending = false; a.id = '';
      a.finished = Promise.resolve(a); a.ready = Promise.resolve(a);
      ['play', 'pause', 'cancel', 'finish', 'reverse', 'commitStyles', 'persist', 'updatePlaybackRate'].forEach(function (k) { a[k] = function () {}; });
      setTimeout(function () { var e = new g.Event('finish'); a.dispatchEvent(e); }, 0);
      return a;
    },
    attachShadow: function (o) { this.__sr = this; return this; },
    setPointerCapture: function () {}, releasePointerCapture: function () {}, hasPointerCapture: function () { return false; },
    requestFullscreen: function () { return Promise.resolve(); }, requestPointerLock: function () {},
    checkVisibility: function () { return this.getBoundingClientRect().width > 0; },
    checkValidity: function () { return true; }, reportValidity: function () { return true; },
    getContext: function (t) {
      if (this.tagName !== 'CANVAS') return null;
      t = String(t);
      if (t !== '2d') return null;
      if (this.__ctx) return this.__ctx;
      var c = { canvas: this, fillStyle: '#000', strokeStyle: '#000', lineWidth: 1, font: '10px sans-serif', globalAlpha: 1, textAlign: 'start', textBaseline: 'alphabetic', lineCap: 'butt', lineJoin: 'miter', globalCompositeOperation: 'source-over', shadowBlur: 0, shadowColor: 'rgba(0,0,0,0)', imageSmoothingEnabled: true };
      ['save', 'restore', 'scale', 'rotate', 'translate', 'transform', 'setTransform', 'resetTransform', 'clearRect', 'fillRect', 'strokeRect', 'beginPath', 'closePath', 'moveTo', 'lineTo', 'bezierCurveTo', 'quadraticCurveTo', 'arc', 'arcTo', 'ellipse', 'rect', 'roundRect', 'fill', 'stroke', 'clip', 'fillText', 'strokeText', 'drawImage', 'putImageData', 'setLineDash'].forEach(function (k) { c[k] = function () {}; });
      c.measureText = function (s) { return { width: String(s).length * 6, actualBoundingBoxAscent: 8, actualBoundingBoxDescent: 2 }; };
      c.getImageData = function (x, y, w, h) { return { width: w, height: h, data: new Uint8ClampedArray(Math.max(0, w * h * 4)) }; };
      c.createImageData = function (w, h) { return { width: w, height: h, data: new Uint8ClampedArray(Math.max(0, w * h * 4)) }; };
      c.createLinearGradient = c.createRadialGradient = c.createConicGradient = function () { return { addColorStop: function () {} }; };
      c.createPattern = function () { return {}; }; c.isPointInPath = function () { return false; }; c.getLineDash = function () { return []; }; c.getTransform = function () { return { a: 1, b: 0, c: 0, d: 1, e: 0, f: 0 }; };
      this.__ctx = c; return c;
    },
    toDataURL: function () { return 'data:,'; },
    toBlob: function (cb) { setTimeout(function () { cb(null); }, 0); },
    captureStream: function () { return {}; }
  });
  /* dialog */
  if (g.HTMLDialogElement) defs(g.HTMLDialogElement.prototype, {
    show: function () { this.setAttribute('open', ''); },
    showModal: function () { this.setAttribute('open', ''); },
    close: function (rv) { if (rv !== undefined) this.returnValue = String(rv); if (!this.hasAttribute('open')) return; this.removeAttribute('open'); this.dispatchEvent(new g.Event('close')); }
  });
  /* multimedia: no se reproduce nada, pero las paginas esperan la API */
  [g.HTMLVideoElement, g.HTMLAudioElement].forEach(function (C) {
    if (!C) return;
    defs(C.prototype, { play: function () { return Promise.resolve(); }, pause: function () {}, load: function () {}, canPlayType: function () { return ''; } });
    ['paused', 'ended'].forEach(function (k) { Object.defineProperty(C.prototype, k, { get: function () { return true; }, configurable: true }); });
    ['currentTime', 'duration', 'volume', 'playbackRate'].forEach(function (k) { Object.defineProperty(C.prototype, k, { get: function () { return k === 'volume' || k === 'playbackRate' ? 1 : 0; }, set: function () {}, configurable: true }); });
  });
  /* <select> y tablas */
  if (g.HTMLSelectElement) defs(g.HTMLSelectElement.prototype, {
    add: function (o, before) { if (typeof before === 'number') before = this.options[before]; this.insertBefore(o, before || null); },
    remove: function (i) { if (arguments.length === 0) { NP.remove.call(this); return; } var o = this.options[i]; if (o && o.parentNode) o.parentNode.removeChild(o); }
  });
  if (g.HTMLTableElement) defs(g.HTMLTableElement.prototype, {
    insertRow: function (i) { var tr = doc.createElement('tr'); var rows = this.querySelectorAll('tr'); if (i === undefined || i < 0 || i >= rows.length) { var tb = this.querySelector('tbody') || this; tb.appendChild(tr); } else rows[i].parentNode.insertBefore(tr, rows[i]); return tr; },
    deleteRow: function (i) { var rows = this.querySelectorAll('tr'); var r = rows[i < 0 ? rows.length - 1 : i]; if (r) r.parentNode.removeChild(r); },
    createTBody: function () { var tb = doc.createElement('tbody'); this.appendChild(tb); return tb; }
  });
  Object.defineProperty(g.HTMLTableElement ? g.HTMLTableElement.prototype : EP, 'rows', { get: function () { return this.querySelectorAll('tr'); }, configurable: true });
  if (g.HTMLTableRowElement) {
    defs(g.HTMLTableRowElement.prototype, { insertCell: function (i) { var td = doc.createElement('td'); var c = this.children; if (i === undefined || i < 0 || i >= c.length) this.appendChild(td); else this.insertBefore(td, c[i]); return td; }, deleteCell: function (i) { var c = this.children[i < 0 ? this.children.length - 1 : i]; if (c) this.removeChild(c); } });
    Object.defineProperty(g.HTMLTableRowElement.prototype, 'cells', { get: function () { return this.children; }, configurable: true });
  }
  /* documento */
  defs(DP, {
    createElementNS: function (ns, name) { return doc.createElement(name); },
    createAttribute: function (n) { return { name: n, value: '' }; },
    createEvent: function (t) { var e = new g.Event(''); e.initEvent = function (ty, b, c) { Object.defineProperty(e, 'type', { value: ty, configurable: true }); e.bubbles = !!b; e.cancelable = !!c; }; e.initCustomEvent = function (ty, b, c, d) { e.initEvent(ty, b, c); e.detail = d; }; e.initMouseEvent = function (ty, b, c) { e.initEvent(ty, b, c); }; e.initUIEvent = e.initMouseEvent; e.initKeyboardEvent = e.initMouseEvent; return e; },
    createRange: function () {
      return { startContainer: doc, endContainer: doc, startOffset: 0, endOffset: 0, collapsed: true, commonAncestorContainer: doc, setStart: function () {}, setEnd: function () {}, setStartBefore: function () {}, setEndAfter: function () {}, selectNode: function () {}, selectNodeContents: function () {}, collapse: function () {}, detach: function () {},
        createContextualFragment: function (h) { return g.__parseFrag(String(h)); }, getBoundingClientRect: function () { return { x: 0, y: 0, width: 0, height: 0, top: 0, left: 0, right: 0, bottom: 0 }; }, getClientRects: function () { return []; }, cloneContents: function () { return doc.createDocumentFragment(); }, deleteContents: function () {}, toString: function () { return ''; } };
    },
    createTreeWalker: function (root, what, filter) {
      var list = [], cur = null, idx = -1;
      (function walk(n) { for (var c = n.firstChild; c; c = c.nextSibling) { var ok = !what || what === 0xFFFFFFFF || (what & (1 << (c.nodeType - 1))); if (ok && (!filter || (typeof filter === 'function' ? filter(c) : filter.acceptNode(c)) === 1)) list.push(c); walk(c); } })(root);
      var w = { root: root, whatToShow: what, filter: filter, get currentNode() { return cur || root; }, set currentNode(v) { cur = v; idx = list.indexOf(v); },
        nextNode: function () { idx++; cur = list[idx] || null; return cur; }, previousNode: function () { idx--; cur = idx >= 0 ? list[idx] : null; return cur; },
        firstChild: function () { return root.firstChild; }, parentNode: function () { return cur ? cur.parentNode : null; } };
      return w;
    },
    getSelection: function () { return g.getSelection(); },
    startViewTransition: function (f) { if (f) f(); return { finished: Promise.resolve(), ready: Promise.resolve(), updateCallbackDone: Promise.resolve(), skipTransition: function () {} }; },
    exitFullscreen: function () { return Promise.resolve(); }, exitPointerLock: function () {},
    getElementsByTagNameNS: function (ns, n) { return doc.getElementsByTagName(n); }
  });
  DP.createNodeIterator = DP.createTreeWalker;
  Object.defineProperty(doc, 'fonts', { value: { ready: Promise.resolve(), status: 'loaded', load: function () { return Promise.resolve([]); }, check: function () { return true; }, add: function () {}, delete: function () {}, forEach: function () {}, addEventListener: function () {}, removeEventListener: function () {} }, configurable: true });
  Object.defineProperty(doc, 'implementation', { value: { createHTMLDocument: function () { return doc; }, hasFeature: function () { return true; } }, configurable: true });
  Object.defineProperty(doc, 'dir', { value: '', writable: true, configurable: true });
  Object.defineProperty(doc, 'designMode', { value: 'off', writable: true, configurable: true });
  Object.defineProperty(doc, 'fullscreenElement', { value: null, configurable: true });
  Object.defineProperty(doc, 'fullscreenEnabled', { value: false, configurable: true });
  Object.defineProperty(doc, 'pictureInPictureEnabled', { value: false, configurable: true });
})(globalThis);
