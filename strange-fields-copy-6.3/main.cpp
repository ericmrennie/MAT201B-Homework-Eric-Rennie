// Karl Yerkes
// 2026-05-26
//
// from this...
//   https://w2.mat.ucsb.edu/l.putnam/wrapture/index.html
//
// Primary <-> Secondary sync strategy:
//   - b, r, count, camera: synced directly via parameterServer (as before)
//   - seed: synced; secondary recomputes from exact seed (no re-search)
//   - matSliders[4][5]: synced; secondary calls rebuildFromSliders() on change
//   - targetSliders[4][5]: synced; secondary uses these as targetMat
//   - blendActive: primary sets true to tell secondary to start a blend
//   - matEditMode: synced; secondary uses to know which path to take
//   - zooIndex: synced as ParameterInt (was plain int — couldn't sync)
//
// KEY DESIGN RULES:
//   1. Secondary never calls rebuild() — that searches for a valid seed and
//      would mutate seed, desyncing from primary.
//   2. guiReady guards all callbacks on BOTH nodes so bulk slider sets
//      during init don't trigger premature rebuilds.
//   3. Blend transition: primary drives it locally; when it settles it
//      sends the final matSliders values and sets blendActive=false.
//      Secondary starts its own blend when blendActive goes true.

#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "Gamma/scl.h"
#include "al/app/al_App.hpp"
#include "al/app/al_DistributedApp.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/io/al_Window.hpp"
#include "al/math/al_Mat.hpp"
#include "al/math/al_Random.hpp"

std::string slurp(std::string fileName) {
  std::fstream file(fileName);
  std::string returnValue = "";
  while (file.good()) {
    std::string line;
    getline(file, line);
    returnValue += line + "\n";
  }
  return returnValue;
}

using namespace al;

// ─────────────────────────────────────────────────────────────────────────────
// Mat5d
// ─────────────────────────────────────────────────────────────────────────────
class Mat5d : public Mat<5, double> {
 public:
  void coefficients(int seed) {
    rnd::Random<> rng;
    rng.seed(seed);
    for (int k = 0; k < size(); k++) mElems[k] = rng.uniformS();
    mElems[4] = mElems[9] = mElems[14] = mElems[19] = 0;
    mElems[24] = 1;
  }
  std::array<double, 25> toArray() {
    std::array<double, 25> a;
    for (int k = 0; k < 25; k++) a[k] = mElems[k];
    return a;
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// wrapture helpers
// ─────────────────────────────────────────────────────────────────────────────
double wrapture(std::vector<Vec4d> &v, double b, int seed, int count) {
  Mat5d m;
  m.coefficients(seed);
  rnd::Random<> rng;
  rng.seed(seed);
  Vec5d x(rng.ball<Vec4d>(), 1);
  v.push_back(x.elems());
  for (int i = 0; i < count; i++) {
    x = m * x;
    for (int k = 0; k < x.size() - 1; k++)
      x.elems()[k] = gam::scl::wrap(x.elems()[k], b, -b);
    v.push_back(x.elems());
  }
  return determinant(m);
}

double wraptureFromArray(std::vector<Vec4d> &v, double b,
                         std::array<double, 25> &mat, int seed, int count) {
  Mat5d m;
  for (int k = 0; k < 25; k++) m.elems()[k] = mat[k];
  m.elems()[4] = m.elems()[9] = m.elems()[14] = m.elems()[19] = 0;
  m.elems()[24] = 1;
  rnd::Random<> rng;
  rng.seed(seed);
  Vec5d x(rng.ball<Vec4d>(), 1);
  v.push_back(x.elems());
  for (int i = 0; i < count; i++) {
    x = m * x;
    for (int k = 0; k < x.size() - 1; k++)
      x.elems()[k] = gam::scl::wrap(x.elems()[k], b, -b);
    v.push_back(x.elems());
  }
  return determinant(m);
}

// ─────────────────────────────────────────────────────────────────────────────
// Matrix layout note
// AlloLib Mat<5,double> is column-major: index = col*5 + row.
// Row 4 (homogeneous) is always [0,0,0,0,1] — fixed, not exposed.
// matSliders[row][col] maps to mElems[col*5 + row].
// ─────────────────────────────────────────────────────────────────────────────

struct AlloApp : DistributedApp {
  // ── Zoo of interesting seeds ──────────────────────────────────────────────
  std::vector<int> zoo = {
      186,  // two flat planes to quad clusters  d:0.311102
      0,    // circles                           d:0.325492
      15,   // galaxy/spirals                    d:0.478628
      99,   // straight lines                    d:0.341211
      7,    // flat planes at 80 degrees         d:0.305452
      11,   // sparse formations with circle     d:0.713958
      76,   // formations with circles           d:0.744726
      77,   // 'wind blowing' effect             d:0.563679
      91,   // triangles                         d:0.335576
      139,  // 'fire' effect                     d:0.512121
      120,  // straight line planes              d:0.680127
      154   // interesting cloud/circle          d:0.421385
  };

  // ── Synced parameters ─────────────────────────────────────────────────────
  Parameter b{"b", 1.0, 0.001, 5};
  Parameter r{"r", 0.01, 0.001, 0.031};
  Parameter count{"count", 10000, 1000, 200000};
  ParameterPose camera{"cam"};
  ParameterInt seed{"seed", "", 0, 0, 1000000};
  ParameterInt zooIndex{"zooIndex", "", 0, 0, 100};  // was plain int — now synced
  ParameterBool matEditMode{"matEditMode", "", false};

  // blendActive: primary sets true to tell secondary "start blending".
  // Primary sets false when its own blend settles.
  ParameterBool blendActive{"blendActive", "", false};

  // ── 20 current-matrix sliders: matSliders[row][col] ──────────────────────
  Parameter matSliders[4][5] = {
      {{"m00", 0, -1, 1}, {"m01", 0, -1, 1}, {"m02", 0, -1, 1},
       {"m03", 0, -1, 1}, {"m04", 0, -1, 1}},
      {{"m10", 0, -1, 1}, {"m11", 0, -1, 1}, {"m12", 0, -1, 1},
       {"m13", 0, -1, 1}, {"m14", 0, -1, 1}},
      {{"m20", 0, -1, 1}, {"m21", 0, -1, 1}, {"m22", 0, -1, 1},
       {"m23", 0, -1, 1}, {"m24", 0, -1, 1}},
      {{"m30", 0, -1, 1}, {"m31", 0, -1, 1}, {"m32", 0, -1, 1},
       {"m33", 0, -1, 1}, {"m34", 0, -1, 1}},
  };

  // ── 20 target-matrix sliders: targetSliders[row][col] ────────────────────
  // Primary writes these when starting a blend so secondaries know targetMat.
  Parameter targetSliders[4][5] = {
      {{"t00", 0, -1, 1}, {"t01", 0, -1, 1}, {"t02", 0, -1, 1},
       {"t03", 0, -1, 1}, {"t04", 0, -1, 1}},
      {{"t10", 0, -1, 1}, {"t11", 0, -1, 1}, {"t12", 0, -1, 1},
       {"t13", 0, -1, 1}, {"t14", 0, -1, 1}},
      {{"t20", 0, -1, 1}, {"t21", 0, -1, 1}, {"t22", 0, -1, 1},
       {"t23", 0, -1, 1}, {"t24", 0, -1, 1}},
      {{"t30", 0, -1, 1}, {"t31", 0, -1, 1}, {"t32", 0, -1, 1},
       {"t33", 0, -1, 1}, {"t34", 0, -1, 1}},
  };

  // ── Local (non-synced) state ──────────────────────────────────────────────
  bool guiReady = false;
  ShaderProgram shader;
  std::vector<Vec4d> v;
  std::array<double, 25> currentMat{};
  std::array<double, 25> targetMat{};
  double blendT = 1.0;       // local blend progress [0,1]
  double blendSpeed = 0.02;
  bool dragged = false;
  double angle = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // Slider <-> array helpers
  // ─────────────────────────────────────────────────────────────────────────

  void matToSliders() {
    bool prev = guiReady;
    guiReady = false;
    for (int row = 0; row < 4; row++)
      for (int col = 0; col < 5; col++)
        matSliders[row][col].set((float)currentMat[col * 5 + row]);
    guiReady = prev;
  }

  void targetToTargetSliders() {
    for (int row = 0; row < 4; row++)
      for (int col = 0; col < 5; col++)
        targetSliders[row][col].set((float)targetMat[col * 5 + row]);
  }

  std::array<double, 25> slidersToArray() {
    std::array<double, 25> a = currentMat;
    for (int row = 0; row < 4; row++)
      for (int col = 0; col < 5; col++)
        a[col * 5 + row] = (double)matSliders[row][col].get();
    a[4] = a[9] = a[14] = a[19] = 0.0;
    a[24] = 1.0;
    return a;
  }

  std::array<double, 25> targetSlidersToArray() {
    std::array<double, 25> a = targetMat;
    for (int row = 0; row < 4; row++)
      for (int col = 0; col < 5; col++)
        a[col * 5 + row] = (double)targetSliders[row][col].get();
    a[4] = a[9] = a[14] = a[19] = 0.0;
    a[24] = 1.0;
    return a;
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Rebuild helpers
  // ─────────────────────────────────────────────────────────────────────────

  // Full rebuild from seed (PRIMARY ONLY — searches for valid determinant).
  void rebuild() {
    v.clear();
    double det = wrapture(v, b, seed.get(), count);
    while (det < 0.3 || det > 0.95) {
      seed.set(seed.get() + 1);
      v.clear();
      det = wrapture(v, b, seed.get(), count);
    }
    double mean = 0, minimum = 1e100, maximum = -1e100;
    for (auto &e : v) {
      if (e.w > maximum) maximum = e.w;
      if (e.w < minimum) minimum = e.w;
      mean += e.w;
    }
    mean /= v.size();
    double dev = 0;
    for (auto &e : v) dev += pow(e.w - mean, 2);
    dev = sqrt(dev / v.size());
    printf("seed:%d det:%lf b:%lf min:%lf max:%lf mean:%lf dev:%lf\n",
           int(seed.get()), det, b.get(), minimum, maximum, mean, dev);
    Mat5d m;
    m.coefficients(seed.get());
    currentMat = m.toArray();
  }

  // Rebuild from exact seed without any searching (SECONDARY — deterministic).
  void rebuildFromSeed(int s) {
    v.clear();
    wrapture(v, b, s, count);
    Mat5d m;
    m.coefficients(s);
    currentMat = m.toArray();
  }

  // Rebuild from current slider values (both nodes, for manual edit mode).
  void rebuildFromSliders() {
    auto a = slidersToArray();
    v.clear();
    wraptureFromArray(v, b, a, seed.get(), count);
    currentMat = a;
    blendT = 1.0;
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Save
  // ─────────────────────────────────────────────────────────────────────────
  void saveMatrixToFile() {
    std::ofstream f("saved_matrices.txt", std::ios::app);
    if (!f.is_open()) { printf("ERROR: could not open saved_matrices.txt\n"); return; }
    f << "# seed:" << seed.get() << " (customized)\n";
    f << "mat";
    auto a = slidersToArray();
    for (int k = 0; k < 25; k++) f << " " << a[k];
    f << "\n";
    f.close();
    printf("Matrix saved to saved_matrices.txt\n");
  }

  // ─────────────────────────────────────────────────────────────────────────
  // onInit
  // ─────────────────────────────────────────────────────────────────────────
  void onInit() override {
    if (isPrimary()) {
      auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
      auto &gui = GUIdomain->newGUI();
      gui.add(b);
      gui.add(r);
      gui.add(count);

      for (int row = 0; row < 4; row++)
        for (int col = 0; col < 5; col++) {
          gui.add(matSliders[row][col]);
          matSliders[row][col].registerChangeCallback([this](float) {
            if (!guiReady) return;
            matEditMode = true;
            rebuildFromSliders();
          });
        }

      // Seed initial slider values from first zoo entry before first frame.
      Mat5d m;
      m.coefficients(zoo[0]);
      currentMat = m.toArray();
      matToSliders();
    }

    // ── Register everything with parameterServer ───────────────────────────
    parameterServer() << b << r << count << camera;
    parameterServer() << seed << zooIndex << matEditMode << blendActive;

    for (int row = 0; row < 4; row++)
      for (int col = 0; col < 5; col++) {
        parameterServer() << matSliders[row][col];
        parameterServer() << targetSliders[row][col];
      }

    // ── Secondary callbacks ────────────────────────────────────────────────
    // NOTE: these fire on BOTH nodes when OSC arrives; the isPrimary() guard
    // ensures only the secondary acts on remotely-received changes.

    seed.registerChangeCallback([this](int32_t newSeed) {
      if (isPrimary()) return;
      // Exact deterministic rebuild — no searching, no seed mutation.
      rebuildFromSeed(newSeed);
      matToSliders();
    });

    matSliders[0][0].registerChangeCallback([this](float) {
      // Use the [0][0] callback as a trigger; by the time it fires all 20
      // values have been sent over OSC and are in the sliders.
      if (isPrimary()) return;
      if (!guiReady) return;
      if (matEditMode.get()) rebuildFromSliders();
    });

    // For robustness, also register on every slider on the secondary.
    for (int row = 0; row < 4; row++)
      for (int col = 0; col < 5; col++) {
        matSliders[row][col].registerChangeCallback([this](float) {
          if (isPrimary()) return;
          if (!guiReady) return;
          if (matEditMode.get()) rebuildFromSliders();
        });
      }

    // blendActive: secondary starts its own blend when primary signals it.
    blendActive.registerChangeCallback([this](float active) {
      if (isPrimary()) return;
      if (active > 0.5f) {
        // targetMat comes from targetSliders (already synced via parameterServer)
        targetMat = targetSlidersToArray();
        blendT = 0.0;
      }
    });

    // ── Bug fix: rebuild when b changes (both nodes) ──────────────────────
    // At low b the wrap range is tiny; stale vertices divided by b in onDraw
    // fly off screen. We must regenerate the point cloud with the new b.
    b.registerChangeCallback([this](float) {
      if (!guiReady) return;
      if (matEditMode.get()) {
        rebuildFromSliders();
      } else if (isPrimary()) {
        rebuild();
        matToSliders();
      } else {
        rebuildFromSeed(seed.get());
        matToSliders();
      }
    });

    // ── Bug fix: rebuild when count changes (both nodes) ──────────────────
    // count is a Parameter<float> cast to int at call sites; changing it in
    // the GUI never triggered a rebuild in the new code (lastknown was removed).
    count.registerChangeCallback([this](float) {
      if (!guiReady) return;
      if (matEditMode.get()) {
        rebuildFromSliders();
      } else if (isPrimary()) {
        rebuild();
        matToSliders();
      } else {
        rebuildFromSeed(seed.get());
        matToSliders();
      }
    });
  }

  // ─────────────────────────────────────────────────────────────────────────
  // onCreate
  // ─────────────────────────────────────────────────────────────────────────
  void onCreate() override {
    shader.compile(slurp("../vertex.glsl"), slurp("../fragment.glsl"),
                   slurp("../geometry.glsl"));
    Mat5d m;
    m.coefficients(zoo[0]);
    currentMat = m.toArray();
    targetMat = currentMat;
    seed.set(zoo[0]);
    rebuild();
    matToSliders();

    // ── Bug fix: set identical starting camera on both nodes ──────────────
    // If the primary's nav() default differs from the secondary's (or the
    // primary has been moved), the secondary sees a different view distance.
    // Set a known default here on both nodes so they start in the same place,
    // then immediately push it from the primary so the secondary snaps to it
    // within the first onAnimate frame.
    nav().pos(0, 0, 4);   // place camera 4 units back along Z
    nav().faceToward(Vec3d(0, 0, 0), Vec3d(0, 1, 0));
    if (isPrimary()) {
      camera.set(nav());  // push immediately so secondary doesn't use its own default
    }

    guiReady = true;  // unlock callbacks on BOTH nodes after init
  }

  // ─────────────────────────────────────────────────────────────────────────
  // onKeyDown  (primary only — secondaries have no keyboard)
  // ─────────────────────────────────────────────────────────────────────────
  bool onKeyDown(const Keyboard &k) override {
    if (!isPrimary()) return true;

    if (k.key() == ' ') {
      // Next zoo entry — immediate snap (no blend)
      matEditMode = false;
      int nextIdx = (zooIndex.get() + 1) % (int)zoo.size();
      zooIndex.set(nextIdx);
      seed.set(zoo[nextIdx]);
      blendT = 0.0;
      rebuild();
      matToSliders();

    } else if (k.key() == Keyboard::BACKSPACE) {
      // Previous zoo entry — immediate snap
      matEditMode = false;
      int nextIdx = (zooIndex.get() - 1 + (int)zoo.size()) % (int)zoo.size();
      zooIndex.set(nextIdx);
      seed.set(zoo[nextIdx]);
      blendT = 0.0;
      rebuild();
      matToSliders();

    } else if (k.key() == 'p') {
      // Next zoo entry — animated blend
      matEditMode = false;
      int nextIdx = (zooIndex.get() + 1) % (int)zoo.size();
      zooIndex.set(nextIdx);
      seed.set(zoo[nextIdx]);
      Mat5d m;
      m.coefficients(seed.get());
      targetMat = m.toArray();

      // Tell secondaries what to blend toward, then fire the signal.
      targetToTargetSliders();
      blendActive.set(false);  // reset edge so callback fires again
      blendT = 0.0;
      blendActive.set(true);
      printf("zoo [%d/%lu] seed:%d\n", nextIdx, zoo.size(), (int)seed.get());

    } else if (k.key() == 'o') {
      // Previous zoo entry — animated blend
      matEditMode = false;
      int nextIdx = (zooIndex.get() - 1 + (int)zoo.size()) % (int)zoo.size();
      zooIndex.set(nextIdx);
      seed.set(zoo[nextIdx]);
      Mat5d m;
      m.coefficients(seed.get());
      targetMat = m.toArray();

      targetToTargetSliders();
      blendActive.set(false);
      blendT = 0.0;
      blendActive.set(true);
      printf("zoo [%d/%lu] seed:%d\n", nextIdx, zoo.size(), (int)seed.get());

    } else if (k.key() == 'l' || k.key() == 'L') {
      // Reset sliders to current seed
      matEditMode = false;
      blendT = 1.0;
      matToSliders();
      printf("Sliders loaded from current seed:%d\n", (int)seed.get());

    } else if (k.key() == 's' || k.key() == 'S') {
      saveMatrixToFile();
    }

    return true;
  }

  bool onMouseDrag(const Mouse &m) override {
    dragged = true;
    return true;
  }

  // ─────────────────────────────────────────────────────────────────────────
  // onAnimate
  // ─────────────────────────────────────────────────────────────────────────
  void onAnimate(double dt) override {
    angle += 0.07;

    // ── Blend transition (both nodes run the same lerp logic) ──────────────
    if (!matEditMode && blendT < 1.0) {
      blendT = std::min(blendT + blendSpeed, 1.0);

      std::array<double, 25> lerpedMat;
      for (int k = 0; k < 25; k++)
        lerpedMat[k] = currentMat[k] * (1.0 - blendT) + targetMat[k] * blendT;
      lerpedMat[4] = lerpedMat[9] = lerpedMat[14] = lerpedMat[19] = 0.0;
      lerpedMat[24] = 1.0;

      v.clear();
      wraptureFromArray(v, b, lerpedMat, seed.get(), count);

      if (blendT >= 1.0) {
        currentMat = targetMat;
        matToSliders();

        if (isPrimary()) {
          // Tell secondaries the blend is done.
          blendActive.set(false);
        }
      }
    }

    // ── Mouse-drag triggered rebuild (primary only) ────────────────────────
    if (isPrimary() && !matEditMode && dragged) {
      dragged = false;
      rebuild();
      matToSliders();
    }

    // ── Camera sync ───────────────────────────────────────────────────────
    if (isPrimary()) {
      camera.set(nav());
    } else {
      nav().set(camera);
    }
  }

  // ─────────────────────────────────────────────────────────────────────────
  // onDraw
  // ─────────────────────────────────────────────────────────────────────────
  void onDraw(Graphics &g) override {
    g.shader(shader);
    g.shader().uniform("b", b);
    g.shader().uniform("r", r);
    g.clear(0);
    Mesh m{Mesh::POINTS};
    // Raw vertices in [-b,b] space; vertex shader divides by b to normalize.
    for (auto &vertex : v) m.vertex(vertex);
    g.draw(m);
  }
};

int main() { AlloApp().start(); }