// Karl Yerkes
// 2026-05-26
//
// from this...
//   https://w2.mat.ucsb.edu/l.putnam/wrapture/index.html
//
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#include "al/app/al_App.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/io/al_Window.hpp"
#include "al/math/al_Mat.hpp"
#include "al/math/al_Random.hpp"
#include "al/app/al_DistributedApp.hpp"

#include "Gamma/scl.h"

std::string slurp(std::string fileName)
{
  std::fstream file(fileName);
  std::string returnValue = "";
  while (file.good())
  {
    std::string line;
    getline(file, line);
    returnValue += line + "\n";
  }
  return returnValue;
}

using namespace al;

class Mat5d : public Mat<5, double>
{
public:
  void coefficients(int seed)
  {
    rnd::Random<> rng;
    rng.seed(seed);
    for (int k = 0; k < size(); k++)
    {
      mElems[k] = rng.uniformS(); // (-1, 1)
    }
    mElems[4] = mElems[9] = mElems[14] = mElems[19] = 0;
    mElems[24] = 1;
  }
  std::array<double, 25> toArray()
  {
    std::array<double, 25> a;
    for (int k = 0; k < 25; k++)
      a[k] = mElems[k];
    return a;
  }
};

double wrapture(std::vector<Vec4d> &v, double b, int seed, int count)
{
  Mat5d m;
  m.coefficients(seed);
  rnd::Random<> rng;
  rng.seed(seed);
  Vec5d x(rng.ball<Vec4d>(), 1);
  v.push_back(x.elems());
  for (int i = 0; i < count; i++)
  {
    x = m * x;
    for (int k = 0; k < x.size() - 1; k++)
    {
      x.elems()[k] = gam::scl::wrap(x.elems()[k], b, -b);
    }
    v.push_back(x.elems());
  }
  return determinant(m);
}

double wraptureFromArray(std::vector<Vec4d> &v, double b, std::array<double, 25> &mat, int seed, int count)
{
  Mat5d m;
  for (int k = 0; k < 25; k++)
    m.elems()[k] = mat[k];
  rnd::Random<> rng;
  rng.seed(seed);
  Vec5d x(rng.ball<Vec4d>(), 1);
  v.push_back(x.elems());
  for (int i = 0; i < count; i++)
  {
    x = m * x;
    for (int k = 0; k < x.size() - 1; k++)
    {
      x.elems()[k] = gam::scl::wrap(x.elems()[k], b, -b);
    }
    v.push_back(x.elems());
  }
  return determinant(m);
}

// ── Matrix layout note ────────────────────────────────────────────────────────
// AlloLib Mat<5,double> stores elements column-major: index = col*5 + row.
// The 5th row (row 4) is the homogeneous row and is always [0,0,0,0,1].
// We expose only rows 0-3, giving 20 sliders laid out as:
//
//   col:    0       1       2       3       4
//  row 0:  m[0]   m[5]  m[10]  m[15]  m[20]
//  row 1:  m[1]   m[6]  m[11]  m[16]  m[21]
//  row 2:  m[2]   m[7]  m[12]  m[17]  m[22]
//  row 3:  m[3]   m[8]  m[13]  m[18]  m[23]
//  row 4:   0      0      0      0      1    (fixed)
//
// The slider array matSliders[r][c] maps to mElems[c*5 + r].
// ─────────────────────────────────────────────────────────────────────────────

struct AlloApp : DistributedApp
{
  std::vector<int> zoo = {
      186, // two flat planes to quad clusters d:0.311102
      0,   // circles d:0.325492
      15,  // galaxy/spirals d:0.478628
      99,  // straight lines d:0.341211
      7,   // flat planes at 80 degrees d:0.305452
      11,  // sparse formations with cirlce d:0.713958
      76,  // formations with circles d:0.744726
      77,  // 'wind blowing' effect d:0.563679
      91,  // triangles d:0.335576
      139, // 'fire' effect d:0.512121
      120, // straight line planes d:0.680127
      154  // interesting cloud/circle formations d:0.421385
  };
  int zooIndex = 0;

  Parameter b{"b", 1.0, 0.001, 5};
  Parameter r{"r", 0.01, 0.001, 0.031};
  Parameter count{"count", 10000, 1000, 200000};
  ParameterPose camera{"cam"};

  // ── 20 matrix sliders: matSliders[row][col], rows 0-3, cols 0-4 ──────────
  // Named "mRC" where R=row, C=col for easy reading in the GUI.
  Parameter matSliders[4][5] = {
    { {"m00",0,-1,1}, {"m01",0,-1,1}, {"m02",0,-1,1}, {"m03",0,-1,1}, {"m04",0,-1,1} },
    { {"m10",0,-1,1}, {"m11",0,-1,1}, {"m12",0,-1,1}, {"m13",0,-1,1}, {"m14",0,-1,1} },
    { {"m20",0,-1,1}, {"m21",0,-1,1}, {"m22",0,-1,1}, {"m23",0,-1,1}, {"m24",0,-1,1} },
    { {"m30",0,-1,1}, {"m31",0,-1,1}, {"m32",0,-1,1}, {"m33",0,-1,1}, {"m34",0,-1,1} },
  };

  // When true, the matrix sliders are driving the display (manual-edit mode).
  // When false, seed/blend transitions drive it as before.
  bool matEditMode = false;

  // Guard: callbacks are suppressed until onCreate() finishes so that
  // matToSliders() during init doesn't trigger a premature rebuild.
  bool guiReady = false;

  ShaderProgram shader;

  // ── helpers to sync between currentMat <-> sliders ───────────────────────

  // Push currentMat values into the GUI sliders (call after loading a seed).
  void matToSliders()
  {
    bool prev = guiReady;
    guiReady = false; // suppress callbacks while bulk-setting all 20 sliders
    for (int row = 0; row < 4; row++)
      for (int col = 0; col < 5; col++)
        matSliders[row][col].set((float)currentMat[col * 5 + row]);
    guiReady = prev;
  }

  // Read slider values into an array (used in mat-edit mode rebuild).
  std::array<double, 25> slidersToArray()
  {
    std::array<double, 25> a = currentMat; // start from current (preserves row 4)
    for (int row = 0; row < 4; row++)
      for (int col = 0; col < 5; col++)
        a[col * 5 + row] = (double)matSliders[row][col].get();
    // row 4 always fixed
    a[4] = a[9] = a[14] = a[19] = 0.0;
    a[24] = 1.0;
    return a;
  }

  // ── save custom matrix to file ────────────────────────────────────────────
  void saveMatrixToFile()
  {
    std::ofstream f("saved_matrices.txt", std::ios::app);
    if (!f.is_open()) { printf("ERROR: could not open saved_matrices.txt\n"); return; }
    f << "# seed:" << seed << " (customized)\n";
    f << "mat";
    auto a = slidersToArray();
    for (int k = 0; k < 25; k++)
      f << " " << a[k];
    f << "\n";
    f.close();
    printf("Matrix saved to saved_matrices.txt\n");
  }

  void onInit() override
  {
    if (isPrimary())
    {
      auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
      auto &gui = GUIdomain->newGUI();
      gui.add(b);
      gui.add(r);
      gui.add(count);

      // ── matrix editor section ─────────────────────────────────────────────
      // Each row of 5 sliders maps to one row of the 4×5 editable sub-matrix.
      // Changing any slider enters mat-edit mode and immediately updates the
      // display.  Press 'L' to load the current seed back into the sliders.
      // Press 'S' to save the slider values to saved_matrices.txt.
      for (int row = 0; row < 4; row++)
        for (int col = 0; col < 5; col++)
        {
          gui.add(matSliders[row][col]);
          matSliders[row][col].registerChangeCallback([this](float) {
            if (!guiReady) return;
            matEditMode = true;
            rebuildFromSliders();
          });
        }

      // ── seed the sliders with the first zoo entry before the first frame ──
      // onInit runs before onCreate, so we compute the matrix here directly
      // rather than waiting for rebuild() so sliders never show 0.
      Mat5d m;
      m.coefficients(zoo[0]);
      currentMat = m.toArray();
      matToSliders();
    }

    parameterServer() << b << r << count << camera;
  }

  std::vector<Vec4d> v;

  void onCreate() override
  {
    shader.compile(slurp("../vertex.glsl"), slurp("../fragment.glsl"),
                   slurp("../geometry.glsl"));
    Mat5d m;
    m.coefficients(zoo[0]);
    currentMat = m.toArray();
    targetMat = currentMat;
    seed = zoo[0];
    rebuild();
    matToSliders(); // ── sync sliders to initial seed
    guiReady = true; // ── unlock callbacks now that init is complete
  }

  int seed = 0;

  std::array<double, 25> currentMat;
  std::array<double, 25> targetMat;
  double blend = 1.0;
  double blendSpeed = 0.02;

  // ── rebuild point cloud directly from slider values ───────────────────────
  void rebuildFromSliders()
  {
    auto a = slidersToArray();
    v.clear();
    wraptureFromArray(v, b, a, seed, count);
    currentMat = a;
    blend = 1.0; // suppress blend transition while in edit mode
  }

  bool onKeyDown(const Keyboard &k) override
  {
    if (k.key() == ' ')
    {
      matEditMode = false;
      zooIndex = (zooIndex + 1) % zoo.size();
      seed = zoo[zooIndex];
      blend = 0.0;
      rebuild();
      matToSliders();
    }
    else if (k.key() == Keyboard::BACKSPACE)
    {
      matEditMode = false;
      zooIndex = (zooIndex - 1 + zoo.size()) % zoo.size();
      seed = zoo[zooIndex];
      rebuild();
      matToSliders();
    }
    else if (k.key() == 'p')
    {
      matEditMode = false;
      zooIndex = (zooIndex + 1) % zoo.size();
      seed = zoo[zooIndex];
      Mat5d m;
      m.coefficients(seed);
      targetMat = m.toArray();
      blend = 0.0;
      printf("zoo [%d/%lu] seed:%d\n", zooIndex, zoo.size(), seed);
    }
    else if (k.key() == 'o')
    {
      matEditMode = false;
      zooIndex = (zooIndex - 1 + zoo.size()) % zoo.size();
      seed = zoo[zooIndex];
      Mat5d m;
      m.coefficients(seed);
      targetMat = m.toArray();
      blend = 0.0;
      printf("zoo [%d/%lu] seed:%d\n", zooIndex, zoo.size(), seed);
    }
    // ── L: Load current seed into sliders (reset any manual edits) ───────────
    else if (k.key() == 'l' || k.key() == 'L')
    {
      matEditMode = false;
      blend = 1.0;
      matToSliders();
      printf("Sliders loaded from current seed:%d\n", seed);
    }
    // ── S: Save current slider values to saved_matrices.txt ─────────────────
    else if (k.key() == 's' || k.key() == 'S')
    {
      saveMatrixToFile();
    }

    return true;
  }

  bool dragged = true;
  bool onMouseDrag(const Mouse &m) override
  {
    dragged = true;
    return true;
  }

  void rebuild()
  {
    v.clear();
    double det = wrapture(v, b, seed, count);
    while (det < 0.3 || det > 0.95)
    {
      seed++;
      v.clear();
      det = wrapture(v, b, seed, count);
    }
    double mean = 0;
    double minimum = 1e100;
    double maximum = -1e100;
    for (auto &e : v)
    {
      if (e.w > maximum)
        maximum = e.w;
      if (e.w < minimum)
        minimum = e.w;
      mean += e.w;
    }
    mean /= v.size();
    double dev = 0;
    for (auto &e : v)
    {
      dev += pow(e.w - mean, 2);
    }
    dev = sqrt(dev / v.size());
    printf("seed:%d det:%lf b:%lf min:%lf max:%lf mean:%lf dev:%lf\n",
           seed, det, b.get(), minimum, maximum, mean, dev);
    // keep currentMat in sync after a seed rebuild
    Mat5d m;
    m.coefficients(seed);
    currentMat = m.toArray();
  }

  double angle = 0;

  void onAnimate(double dt) override
  {
    angle += 0.07;

    // When in mat-edit mode the sliders own the display; skip blend transitions.
    if (!matEditMode && blend < 1.0)
    {
      blend = std::min(blend + blendSpeed, 1.0);

      std::array<double, 25> lerpedMat;
      for (int k = 0; k < 25; k++)
      {
        lerpedMat[k] = currentMat[k] * (1.0 - blend) + targetMat[k] * blend;
      }
      // enforce homogeneous row
      lerpedMat[4] = lerpedMat[9] = lerpedMat[14] = lerpedMat[19] = 0.0;
      lerpedMat[24] = 1.0;

      v.clear();
      wraptureFromArray(v, b, lerpedMat, seed, count);

      if (blend >= 1.0)
      {
        currentMat = targetMat;
        matToSliders(); // ── keep sliders in sync after transition settles
      }
    }

    if (!matEditMode && dragged)
    {
      dragged = false;
      rebuild();
      matToSliders();
    }

    if (isPrimary()) {
      camera.set(nav());
    }
    else {
      if (lastknown != count) {
        lastknown = count;
        rebuild();
      }
      nav().set(camera);
    }
  }

  int lastknown = 0;

  void onDraw(Graphics &g) override
  {
    g.shader(shader);
    g.shader().uniform("b", b);
    g.shader().uniform("r", r);
    g.clear(0);
    Mesh m{Mesh::POINTS};
    for (auto &vertex : v)
    {
      m.vertex(vertex / b);
    }
    g.draw(m);
  }
};

int main() { AlloApp().start(); }