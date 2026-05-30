// Karl Yerkes
// 2026-05-26

#include <iostream>
#include "Gamma/scl.h"
#include "al/app/al_App.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/io/al_Window.hpp"
#include "al/math/al_Mat.hpp"
#include "al/math/al_Random.hpp"
#include <fstream>
#include <vector>
#include <array>

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

class Mat5d : public Mat<5, double> {
public:
  void coefficients(int seed) {
    rnd::Random<> rng;
    rng.seed(seed);
    for (int k = 0; k < size(); k++) {
      mElems[k] = rng.uniformS();
    }
    mElems[4] = mElems[9] = mElems[14] = mElems[19] = 0;
    mElems[24] = 1;
  }
};

// ── your matrices live here, at global scope ──────────────────────────────────
std::vector<std::array<double, 25>> myMatrices = {
  { -0.87144029,  0.28511616,  0.34899113,  0.14162665,  1.0745902, 
    -0.4723414,  -0.51946704, -0.65206152, -0.25380246,  0.25491881,  
    0.00365315,   0.79361764, -0.53263264, -0.26270231, -1.92718495,   
    0.00493937,   0.03982636, -0.38971241,  0.91053063,  0.39286856,   
    0.0,          0.0,        0.0,          0.0,          1.0
  },
  { -0.91448663, -0.34970227,  0.1053967,   0.02951631, -0.21180295,
     0.17110233, -0.66565387, -0.64171137, -0.29392616, -1.1862794, 
    -0.27381574,  0.5171066,  -0.73974076,  0.28454608,  3.0188235,
     0.17326363, -0.37117539,  0.02094796,  0.89572701,  0.3111008, 
     0.0,          0.0,        0.0,          0.0,          1.0        
  }, // there's a weird formation that has only two boids in second matrix
  { -0.93509581, -0.22314403,  0.24043644, -0.07942027, -1.3159418, 
    -0.00538441, -0.69035878, -0.48928362,  0.52181605, -0.51223042,
    -0.32981865,  0.66939584, -0.53610218,  0.37952341,  2.214634, 
    0.07142259,   0.11762395,  0.63537469,  0.75211509,  1.28969829,
    0.0,          0.0,         0.0,         0.0,         1.0 
  },
  {-0.78051221, -0.5247208,   0.20533365, -0.24854047, -0.79861573,
    0.29977882, -0.70965166, -0.62730084,  0.03855303, -1.05161042,
   -0.52967481,  0.31212754, -0.57362313,  0.53051202,  1.70156516,
    0.09403963, -0.3348201,   0.47302616,  0.80234893, -1.9365467,
    0.0,         0.0,         0.0,         0.0,         1.0       
  },
  {-0.53155211, -0.29859092, -0.25655138, -0.72879392,  0.95640596,
    0.24833492, -0.84346452, -0.33912227,  0.28382534, -3.80112879,
    0.46675251,  0.3646583,  -0.7532253,  -0.22468073,  0.35767367,
   -0.63764221,  0.18734662, -0.46956628,  0.55361046,  0.23360001,
    0.0,         0.0,         0.0,         0.0,         1.0        
  },
  {-0.92379227, -0.3424085,  0.06218198,  0.10048408, -0.93044583,
    0.18844675, -0.6961897,  -0.63522775, -0.24676477, -1.62854447,
   -0.22156911,  0.53799765, -0.75858621,  0.26572923,  3.56148466,
    0.21587545, -0.30534281,  0.04201758,  0.91814895,  1.13964745,
    0.0,          0.0,          0.0,          0.0,          1.0        
  },
  {-0.04471466,  0.69645877, -0.62576214, -0.28595021,  0.11382679,
   -0.88251655,  0.26899088,  0.29020481,  0.15808104,  0.46164553,
   -0.37696787, -0.5809148,  -0.68049816,  0.13324965, -2.87298931,
    0.19354681,  0.25598257, -0.14671457,  0.91426877,  3.5883863, 
    0.0,         0.0,         0.0,         0.0,         1.0        
  },
  {0.78051221, -0.5247208,   0.20533365, -0.24854047, -0.79861573,
   0.29977882, -0.7096516,  -0.62730084,  0.03855303, -1.05161042,
  -0.52967481,  0.31212754, -0.57362313,  0.53051202,  1.70156516,
   0.09403963, -0.3348201,   0.47302616,  0.80234893, -1.9365467,
   0.0,         0.0,         0.0,         0.0,         1.0        
  },
  {-9.14108095, -2.34378743,  2.03717123, -2.39887759, -2.06212555
   -8.05037257, -5.77684095, -8.18552370,  8.01669360,  2.59158985,
   -3.84056100,  6.79441132, -4.66588304,  4.03397845,  1.43581278,
    5.09864376,  3.73204027,  8.50686908,  3.55842782,  1.08187901,
    0.0,         0.0,         0.0,         0.0,         1.0 
  },
  {-0.21295909,  0.07863175, -0.10354457,  0.94770459, -0.15839607,
   -0.50630341, -0.58754639, -0.58499661, -0.12893821, -2.54104476,
    0.57589456,  0.24748235, -0.75285411,  0.02662017, -4.30428051,
    0.57188349, -0.74010728,  0.20166354,  0.21194883, -3.2015577,
    0.0,         0.0,         0.0,         0.0,         1.0        
  },
  {-0.3151103,  -0.09654286,  0.50301606,  0.77379572,  1.39280297,
   -0.54209358, -0.14282811, -0.76176218,  0.25661851,  4.59199816,
    0.0101777,   0.95200863, -0.11840303,  0.19989173,  3.57799515,
    0.75308463, -0.15607425, -0.3362647,   0.50579682, -3.08756914,
    0.0,         0.0,         0.0,         0.0,         1.0        
  }
};
// ─────────────────────────────────────────────────────────────────────────────

// wrapture now takes the mat array directly instead of a seed for the matrix
double wrapture(std::vector<Vec4d> &v, double b, std::array<double, 25>& mat, int seed, int count) {
  Mat5d m;
  for (int k = 0; k < 25; k++) m.elems()[k] = mat[k];

  printf("Matrix:\n");
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 5; col++) {
      printf("%8.4f ", m.elems()[col * 5 + row]);
    }
    printf("\n");
  }

  rnd::Random<> rng;
  rng.seed(seed);
  Vec5d x(rng.ball<Vec4d>(), 1);
  v.push_back(x.elems());

  for (int i = 0; i < count; i++) {
    x = m * x;
    for (int k = 0; k < x.size() - 1; k++) {
      x.elems()[k] = gam::scl::wrap(x.elems()[k], b, -b);
    }
    v.push_back(x.elems());
  }
  return determinant(m);
}

struct MyApp : public App {
  Parameter b{"b", 1.0, 0.001, 5};
  Parameter r{"r", 0.01, 0.001, 0.031};
  ShaderProgram shader;

  // ── interpolation state ────────────────────────────────────────────────────
  std::array<double, 25> currentMat;
  std::array<double, 25> targetMat;
  double blend = 1.0;         // 1.0 means no transition in progress
  double blendSpeed = 0.02;   // ~50 frames to fully transition; tune this
  int matIndex = 0;
  // ──────────────────────────────────────────────────────────────────────────

  void onInit() override {
    auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
    auto &gui = GUIdomain->newGUI();
    gui.add(b);
    gui.add(r);
  }

  std::vector<Vec4d> v;
  int seed = 0;

  void onCreate() override {
    shader.compile(slurp("../vertex.glsl"), slurp("../fragment.glsl"),
                   slurp("../geometry.glsl"));
    currentMat = myMatrices[0];   // start on the first matrix
    targetMat  = myMatrices[0];
    rebuild();
  }

  bool onKeyDown(const Keyboard& k) override {
    if (k.key() == ' ') {
      matIndex = (matIndex + 1) % myMatrices.size();
      targetMat = myMatrices[matIndex];
      blend = 0.0;  // kick off the transition
    }
    else if (k.key() == Keyboard::BACKSPACE) {
      matIndex = (matIndex - 1 + myMatrices.size()) % myMatrices.size();
      targetMat = myMatrices[matIndex];
      blend = 0.0;
    }
    return true;
  }

  bool dragged = true;
  bool onMouseDrag(const Mouse &m) override {
    dragged = true;
    return true;
  }

  // builds the point cloud from a specific mat array
  void rebuildWithMat(std::array<double, 25> mat) {
    v.clear();
    wrapture(v, b, mat, seed, 10000);
  }

  // convenience: rebuild from whatever currentMat is
  void rebuild() {
    rebuildWithMat(currentMat);
  }

  double angle = 0;

  void onAnimate(double dt) override {
    angle += 0.07;

    // tick the blend and rebuild each frame during a transition
    if (blend < 1.0) {
      blend = std::min(blend + blendSpeed, 1.0);

      std::array<double, 25> lerpedMat;
      for (int k = 0; k < 25; k++) {
        lerpedMat[k] = currentMat[k] * (1.0 - blend) + targetMat[k] * blend;
      }
      // enforce homogeneous row
      lerpedMat[4] = lerpedMat[9] = lerpedMat[14] = lerpedMat[19] = 0.0;
      lerpedMat[24] = 1.0;

      rebuildWithMat(lerpedMat);

      if (blend >= 1.0) {
        currentMat = targetMat;  // lock in once done
      }
    }

    if (dragged) {
      dragged = false;
      rebuild();
    }
  }

  void onDraw(Graphics &g) override {
    g.shader(shader);
    g.shader().uniform("b", b);
    g.shader().uniform("r", r);
    g.clear(0.27);
    g.rotate(angle, 0, 1, 0);
    g.scale(1 / b);
    g.scale(0.2);
    Mesh m{Mesh::POINTS};
    for (auto &e : v) {
      m.vertex(e);
    }
    g.draw(m);
  }
};

int main() { MyApp().start(); }