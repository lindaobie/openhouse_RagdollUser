import * as THREE from 'three';
import { OrbitControls } from './vendor/OrbitControls.js';

const BASE = 0xe0795a;
const DEVICE = 0x7fb0d8;
const GROUND = 0x0d0e11;
const TRUST_LIMIT_DEG = 55;

let renderer, scene, camera, controls, group, labels, raf, canvas, wrap;
let floor, grid, floorY = 0;
let fitted = false;
// Created once, moved afterwards: rebuilding the scene on every poll made motion a slideshow.
const objs = new Map();

function radialTexture() {
  const c = document.createElement('canvas');
  c.width = c.height = 128;
  const ctx = c.getContext('2d');
  const g = ctx.createRadialGradient(64, 64, 0, 64, 64, 64);
  g.addColorStop(0, 'rgba(255,255,255,1)');
  g.addColorStop(0.45, 'rgba(255,255,255,0.35)');
  g.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, 128, 128);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

let glowTex;

function ensure(container) {
  if (renderer && wrap === container) return;
  dispose();
  wrap = container;
  canvas = document.createElement('canvas');
  canvas.className = 'map3d-canvas';
  container.appendChild(canvas);

  renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.setClearColor(GROUND, 1);
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.15;

  scene = new THREE.Scene();
  scene.fog = new THREE.Fog(GROUND, 9, 26);
  camera = new THREE.PerspectiveCamera(42, 1, 0.05, 200);

  controls = new OrbitControls(camera, canvas);
  controls.enableDamping = true;
  controls.dampingFactor = 0.075;
  controls.rotateSpeed = 0.7;
  controls.maxPolarAngle = Math.PI * 0.492;
  controls.minDistance = 0.5;
  controls.maxDistance = 28;

  scene.add(new THREE.HemisphereLight(0xcfd6e6, 0x0a0b0e, 0.9));
  const key = new THREE.DirectionalLight(0xffe9df, 1.5);
  key.position.set(4, 7, 5);
  scene.add(key);
  const rim = new THREE.DirectionalLight(0x8fb6ff, 0.7);
  rim.position.set(-5, 3, -6);
  scene.add(rim);

  floor = new THREE.Mesh(
    new THREE.PlaneGeometry(80, 80),
    new THREE.MeshStandardMaterial({ color: 0x0a0b0e, roughness: 1, metalness: 0 })
  );
  floor.rotation.x = -Math.PI / 2;
  scene.add(floor);

  grid = new THREE.GridHelper(16, 32, 0x30343d, 0x1b1e24);
  grid.material.transparent = true;
  grid.material.opacity = 0.55;
  scene.add(grid);

  group = new THREE.Group();
  scene.add(group);

  glowTex = radialTexture();
  labels = [];
  objs.clear();
  fitted = false;

  resize();
  loop();
}

function resize() {
  if (!renderer || !wrap) return;
  const w = wrap.clientWidth || 800;
  const h = wrap.clientHeight || 380;
  renderer.setSize(w, h, false);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}

function dispose() {
  if (raf) cancelAnimationFrame(raf);
  raf = null;
  if (controls) controls.dispose();
  if (renderer) renderer.dispose();
  if (wrap) wrap.innerHTML = '';
  renderer = scene = camera = controls = group = null;
  labels = [];
  objs.clear();
  wrap = null;
  fitted = false;
}

function makeLabel(cls) {
  const el = document.createElement('div');
  el.className = 'map3d-label ' + cls;
  wrap.appendChild(el);
  return { el, pos: new THREE.Vector3() };
}

// telemetry q is w,x,y,z; three's constructor wants x,y,z,w.
function forward(q) {
  return new THREE.Vector3(0, 0, -1)
    .applyQuaternion(new THREE.Quaternion(q[1], q[2], q[3], q[0]))
    .normalize();
}
function quat(q) {
  return new THREE.Quaternion(q[1], q[2], q[3], q[0]);
}

function sprite(color, size, opacity, additive) {
  const s = new THREE.Sprite(new THREE.SpriteMaterial({
    map: glowTex, color, transparent: true, opacity, depthWrite: false,
    blending: additive ? THREE.AdditiveBlending : THREE.NormalBlending,
  }));
  s.scale.setScalar(size);
  return s;
}

function makeBase() {
  const o = {};
  o.holder = new THREE.Group();
  group.add(o.holder);

  o.core = new THREE.Mesh(
    new THREE.SphereGeometry(0.055, 24, 16),
    new THREE.MeshStandardMaterial({ color: BASE, emissive: BASE, emissiveIntensity: 1.7, roughness: 0.3 })
  );
  o.holder.add(o.core);

  o.ring = new THREE.Mesh(
    new THREE.TorusGeometry(0.105, 0.011, 10, 48),
    new THREE.MeshStandardMaterial({ color: BASE, emissive: BASE, emissiveIntensity: 0.55, roughness: 0.5 })
  );
  o.holder.add(o.ring);

  o.holder.add(sprite(BASE, 0.8, 0.42, true));
  o.holder.add(sprite(BASE, 0.2, 0.95, false));

  const reach = 2.2, spread = 0.62;
  o.cone = new THREE.Mesh(
    new THREE.ConeGeometry(Math.tan(spread) * reach, reach, 48, 1, true),
    new THREE.ShaderMaterial({
      uniforms: { uColor: { value: new THREE.Color(BASE) }, uOpacity: { value: 0.22 }, uH: { value: reach } },
      vertexShader:
        'varying float vT; uniform float uH;\n' +
        'void main(){ vT = clamp(position.y / uH + 0.5, 0.0, 1.0);' +
        ' gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }',
      fragmentShader:
        'uniform vec3 uColor; uniform float uOpacity; varying float vT;\n' +
        'void main(){ gl_FragColor = vec4(uColor, uOpacity * pow(vT,1.6)); }',
      transparent: true,
      depthWrite: false,
      side: THREE.DoubleSide,
    })
  );
  o.cone.rotation.x = -Math.PI / 2;
  o.cone.position.z = reach / 2;
  o.holder.add(o.cone);

  o.axes = [
    new THREE.ArrowHelper(new THREE.Vector3(1, 0, 0), new THREE.Vector3(), 0.3, 0xff6b6b, 0.055, 0.035),
    new THREE.ArrowHelper(new THREE.Vector3(0, 1, 0), new THREE.Vector3(), 0.3, 0x7ddc9a, 0.055, 0.035),
    new THREE.ArrowHelper(new THREE.Vector3(0, 0, 1), new THREE.Vector3(), 0.3, 0x6fa8ff, 0.055, 0.035),
  ];
  for (const a of o.axes) o.holder.add(a);

  o.shadow = sprite(0x000000, 0.5, 0.5, false);
  group.add(o.shadow);

  o.drop = new THREE.Line(
    new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), new THREE.Vector3(0, -1, 0)]),
    new THREE.LineDashedMaterial({ color: BASE, transparent: true, opacity: 0.22, dashSize: 0.05, gapSize: 0.05 })
  );
  o.holder.add(o.drop);

  o.originRing = new THREE.Mesh(
    new THREE.TorusGeometry(0.34, 0.006, 8, 72),
    new THREE.MeshBasicMaterial({ color: BASE, transparent: true, opacity: 0.35 })
  );
  o.originRing.rotation.x = Math.PI / 2;
  o.originRing.visible = false;
  group.add(o.originRing);

  o.labelRef = makeLabel('base');
  labels.push(o.labelRef);
  return o;
}

function makeDevice() {
  const o = {};
  o.box = new THREE.Mesh(
    new THREE.BoxGeometry(0.13, 0.13, 0.18),
    new THREE.MeshStandardMaterial({ color: 0x2a3540, emissive: DEVICE, emissiveIntensity: 0.5, metalness: 0.35, roughness: 0.38 })
  );
  group.add(o.box);

  o.edges = new THREE.LineSegments(
    new THREE.EdgesGeometry(o.box.geometry),
    new THREE.LineBasicMaterial({ color: DEVICE, transparent: true, opacity: 0.9 })
  );
  group.add(o.edges);

  o.halo = sprite(DEVICE, 0.42, 0.3, true);
  group.add(o.halo);
  group.add(sprite(DEVICE, 0.17, 0.95, false));
  o.arrow = new THREE.ArrowHelper(new THREE.Vector3(0, 0, 1), new THREE.Vector3(), 0.34, DEVICE, 0.09, 0.06);
  group.add(o.arrow);

  o.labelRef = makeLabel('dev');
  labels.push(o.labelRef);
  return o;
}

function syncBase(b, fy) {
  const key = 'b:' + b.serial;
  let o = objs.get(key);
  if (!o) { o = makeBase(); objs.set(key, o); }

  const pos = new THREE.Vector3(b.p[0], b.p[1], b.p[2]);
  o.holder.position.copy(pos);
  o.holder.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), forward(b.q));
  o.shadow.position.set(pos.x, fy + 0.004, pos.z);
  o.originRing.visible = !!b.origin;
  if (b.origin) o.originRing.position.copy(pos);

  o.drop.geometry.dispose();
  o.drop.geometry = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), new THREE.Vector3(0, fy - pos.y, 0)]);
  o.drop.computeLineDistances();
  o.labelRef.pos.copy(pos);
  return o;
}

function syncDevice(d) {
  const key = 'd:' + d.serial;
  let o = objs.get(key);
  if (!o) { o = makeDevice(); objs.set(key, o); }

  const pos = new THREE.Vector3(d.p[0], d.p[1], d.p[2]);
  const rot = quat(d.q);
  o.box.position.copy(pos); o.box.quaternion.copy(rot);
  o.edges.position.copy(pos); o.edges.quaternion.copy(rot);
  o.halo.position.copy(pos);
  o.arrow.position.copy(pos);
  o.arrow.setDirection(new THREE.Vector3(0, 0, 1).applyQuaternion(rot));
  o.labelRef.pos.copy(pos);
  o.labelRef.el.textContent = (d.role || '设备') + ' ' + d.serial.slice(-6);
  return o;
}

function dropMissing(keep) {
  for (const [key, o] of Array.from(objs.entries())) {
    if (keep.has(key)) continue;
    const i = labels.indexOf(o.labelRef);
    if (i >= 0) labels.splice(i, 1);
    o.labelRef.el.remove();
    for (const k of Object.keys(o)) {
      const v = o[k];
      if (v && v.isObject3D) group.remove(v);
    }
    objs.delete(key);
  }
}

export const Map3D = {
  show(container, state) {
    ensure(container);

    const bases = (state?.telemetry?.bases || []).filter((b) => b.p && b.p.length === 3);
    const devices = (state?.telemetry?.devices || []).filter((d) => d.p && d.p.length === 3 && d.valid);

    const ys = bases.concat(devices).map((o) => o.p[1]);
    floorY = (ys.length ? Math.min.apply(null, ys) : 0) - 0.25;
    if (floor) floor.position.y = floorY;
    if (grid) grid.position.y = floorY;

    const keep = new Set();
    const made = new Map();
    for (const b of bases) { keep.add('b:' + b.serial); made.set('b:' + b.serial, syncBase(b, floorY)); }
    for (const d of devices) { keep.add('d:' + d.serial); made.set('d:' + d.serial, syncDevice(d)); }
    dropMissing(keep);

    for (const b of bases) {
      const o = made.get('b:' + b.serial);
      const f = forward(b.q);
      const bp = new THREE.Vector3(b.p[0], b.p[1], b.p[2]);
      let worst = null;
      for (const d of devices) {
        if (!(d.coverage || []).includes(b.serial)) continue;
        const v = new THREE.Vector3(d.p[0], d.p[1], d.p[2]).sub(bp);
        if (v.lengthSq() < 1e-6) continue;
        const a = (f.angleTo(v) * 180) / Math.PI;
        worst = worst === null ? a : Math.max(worst, a);
      }
      const trust = worst === null ? 'unknown' : worst > TRUST_LIMIT_DEG ? 'bad' : 'ok';
      o.cone.material.uniforms.uOpacity.value = trust === 'ok' ? 0.22 : 0.06;
      o.labelRef.el.className = 'map3d-label ' + (trust === 'bad' ? 'base bad' : 'base');
      o.labelRef.el.textContent =
        b.serial.slice(-8) + (b.channel ? ' · ' + b.channel : '') +
        (b.origin ? ' · 原点' : '') +
        (trust === 'bad' ? ' · 朝向不可信' : trust === 'unknown' ? ' · 朝向未知' : '');
    }

    if (!fitted && bases.length + devices.length > 0) {
      const pts = bases.map((b) => new THREE.Vector3(b.p[0], b.p[1], b.p[2]))
        .concat(devices.map((d) => new THREE.Vector3(d.p[0], d.p[1], d.p[2])));
      const box = new THREE.Box3().setFromPoints(pts);
      const center = box.getCenter(new THREE.Vector3());
      const radius = Math.max(0.9, box.getSize(new THREE.Vector3()).length() * 0.55);
      controls.target.copy(center);
      camera.position.copy(center).add(new THREE.Vector3(radius * 1.15, radius * 1.35, radius * 1.75));
      fitted = true;
      controls.update();
    }
    resize();
  },
  hide() { dispose(); },
  resize,
};

function loop() {
  raf = requestAnimationFrame(loop);
  if (!renderer) return;
  controls.update();
  renderer.render(scene, camera);

  const w = renderer.domElement.clientWidth || 1;
  const h = renderer.domElement.clientHeight || 1;
  for (const l of labels) {
    const v = l.pos.clone().project(camera);
    if (v.z > 1) { l.el.style.display = 'none'; continue; }
    l.el.style.display = '';
    const x = (v.x * 0.5 + 0.5) * w;
    const y = (-v.y * 0.5 + 0.5) * h;
    l.el.style.transform = 'translate(-50%,-165%) translate(' + x + 'px, ' + y + 'px)';
  }
}
