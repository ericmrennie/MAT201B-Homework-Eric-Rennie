// code adapted from Karl Yerkes
// 2022-01-20

#include "al/app/al_DistributedApp.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"
#include "al_ext/statedistribution/al_CuttleboneDomain.hpp"
#include "al_ext/statedistribution/al_CuttleboneStateSimulationDomain.hpp"

using namespace al;

#include <algorithm>
#include <fstream> // opens and reads GLSL shader files
#include <vector> // resizable array type
#include <cmath> // math
#include <random> // random
#include <iostream> // for console logs
using namespace std;

// Matrices
Mat4f matrix345 (
  -0.39297645, -0.19006539, -0.79904449,  0.38275456,
  0.70360333,  0.0151363,  -0.55097654, -0.42031725,
  0.24671911,  0.79894134, -0.06128217,  0.52210669,
  0.51494664, -0.54851368,  0.17241155,  0.61625113
);

Mat4f matrix133 (
  -0.62548462, -0.00276655, -0.60210054,  0.48590037,
  0.52241925, -0.29601617, -0.74998494, -0.25853052,
  -0.30724325,  0.62371315, -0.22778432, -0.67421116,
  0.48094864,  0.71638787, -0.11390664,  0.48204253
);

Mat4f matrix183 (
  -0.8229469, -0.4736292,  -0.24244344, -0.17185128,
  0.51967637, -0.77892607, -0.31859541,  0.10763629,
  -0.04655574,  0.3653016,  -0.83560482,  0.39500613,
  -0.20101081, -0.15931808,  0.36243714,  0.89035369
);

Mat4f matrix337 (
  -0.87300695, -0.20841005, -0.40092313,  0.12980276,
  0.35530637, -0.87154869, -0.27005816,  0.15618207,
  0.28966717,  0.42067163, -0.68779197,  0.49923392,
  -0.1042745,  -0.05628051,  0.52577127,  0.83227873
);

Mat4f matrix19 (
  -0.2820705,  -0.43542687, -0.4535665,  -0.69679057,
  0.67036022, -0.5532802,   0.40975586, -0.19234948,
  0.60508977,  0.56872085, -0.38589956, -0.34914824,
  0.25557734, -0.3758205,  -0.66170922,  0.56212106
);

// creates a random 3D vector where each component is in [-1, 1], then scales it.
// uniformS means "uniform, signed" (includes negatives)

Vec3f randomVec3f(float scale) {
  return Vec3f(rnd::uniformS(), rnd::uniformS(), rnd::uniformS()) * scale;
}

string slurp(string fileName);  // forward declaration - full definition is at the bottom. Reads an entire file into a string (used to load GLSL shaders)

const int N = 500;

// Claude Sonnet 4.6 - "how would I animate between two matrices"
Mat4f lerpMatrix(Mat4f A, Mat4f B, float t) {
  float t2 = (1.0f - cos(t * M_PI)) / 2.0f; // cosine curve - start slow, speed up in the middle, slow down again at the end
  // Loops through all 16 numbers of the matrix and blends each one individually between matrix A and B
  // when t2=0 you get pure a
  // when t2=1 you get pure b
  // in between you get a mix
  Mat4f result;
  for (int i = 0; i < 16; i++) {
    result[i] = A[i] * (1.0f - t2) + B[i] * t2;
  }
  return result;
}

struct WorldState {
  double time;
  int frame;
  Pose camera;
  Vec3f position[N];
};

// Declares GUI-controllable parameters. These show up as sliders at runtime.
struct AlloApp : DistributedAppWithState<WorldState> {

  // state variables
  ShaderProgram pointShader; // the GLSL program that draws particles

  //  simulation state
  Mesh mesh;  // position *is inside the mesh* mesh.vertices() are the positions. stores positions and colors
  vector<Vec3f> currentPositions; // stores starting position of each particle

  // runs before window opens. Sets up the GUI panel and registers the three sliders
  void onInit() override {
    auto cuttleboneDomain = CuttleboneStateSimulationDomain<WorldState>::enableCuttlebone(this);
    if (!cuttleboneDomain) {
      std::cerr << "ERROR: Could not start Cuttlebone. Quitting." << std::endl;
      quit();
    }
    
    if (isPrimary()) {

    }
  }

  // loads and compiles the three shader stages (vertex -> geometry -> fragment) from files
  void onCreate() override {
    // compile shaders
    pointShader.compile(slurp("../point-vertex.glsl"),
                        slurp("../point-fragment.glsl"),
                        slurp("../point-geometry.glsl"));

    // set initial conditions of the simulation

    // c++11 "lambda" function
    // A lambda that returns a fully-saturated, full-brightness color with a random hue.
    auto randomColor = []() { return HSV(rnd::uniform(), 1.0f, 1.0f); };

    // spawns 500 particles with randomized positions, colors, masses, velocities, and initial forces

    //***** YOU ARE HERE ******
    mesh.primitive(Mesh::POINTS);
    int count = 0;
    while (count < N) {
      Vec3f v = randomVec3f(5); // random position in [-5, 5]^3
      Vec4f transformed = Vec4f(v, 1.0);
      //if (transformed.mag() > 5.0) continue; // if the distance is greater than 5.0, skip the rest of the loop iteration and go back to the top
      mesh.vertex(transformed.x, transformed.y, transformed.z); 
      mesh.color(255, 255, 255); // white color
      mesh.texCoord(1.0, 0);
      currentPositions.push_back(v);
      
      std::cout << transformed.x << " "
          << transformed.y << " "
          << transformed.z << " "
          << transformed.w << std::endl;

      // increment count
      count++;
    }
    //***** YOU ARE HERE ******

    nav().pos(0, 0, 10); // place camera 30 units back
  }

  // spacebar toggles this - pauses the whole simulation
  bool freeze = false;

  // the simulation loop
  void onAnimate(double dt) override {
    if (!isPrimary()) {
      return;
    }

    state().frame++;
    state().time += dt;

    if (freeze) return;

    // ── Add or remove matrices from this array ──
    Mat4f matrices[] = { matrix345, matrix133, matrix183, matrix337, matrix19 };
    int numMatrices = 5;  // update this when you add more

    float duration = 5.0f;  // seconds per transition
    float totalDuration = duration * numMatrices;
    // fmod is modulo for floats
    // wraps state().time into a repeating window of duration times number of matrices 
    float t = fmod(state().time, totalDuration);

    // figure out which two matrices we're between
    int idx = (int)(t / duration);
    float localT = (t - idx * duration) / duration;

    // cycling through matrices
    Mat4f current = lerpMatrix(matrices[idx], matrices[(idx + 1) % numMatrices], localT);

    // loop through all particles, grab each one's original starting position
    // this is transforming from scratch each frame rather than building on the previous frame's result
    // ** might want to change this to feed into the next matrix **
    for (int i = 0; i < N; i++) {
      // ** left off here ** - right now it collapses on itself
      Vec3f v = currentPositions[i];
      // apply the blended matrix to the original position
      // promotes the 3d position to 4d by adding a 1.0 as the fourth component
      Vec4f transformed = current * Vec4f(v, 1.0);
      // extract only the x,y,z from the 4d result and store it back into the mesh so it gets drawn in the new posiiton
      Vec3f newPos = Vec3f(transformed.x, transformed.y, transformed.z);
      mesh.vertices()[i] = newPos;
      state().position[i] = newPos;
      currentPositions[i] = newPos;
    }
    // syncs the camera position to the world state so all screens share the same viewpoint
    state().camera = nav();
  };

  bool onKeyDown(const Keyboard& k) override {
    // pauses/unpauses simulation
    if (k.key() == ' ') {
      freeze = !freeze;
    }

    return true;
  }

  // clears the screen, activates the point shader, draws all particles
  void onDraw(Graphics& g) override {
    if(!isPrimary()) {
      nav().set(state().camera);
    }

    g.clear(0.0); // black background
    g.shader(pointShader);
    g.shader().uniform("pointSize", 0.1); // pass slider value to shader
    g.blending(true);
    g.blendTrans(); // transparency blending
    g.depthTesting(true);

    if(!isPrimary()) {
      for (int i = 0; i < N; i++) {
        mesh.vertices()[i] = state().position[i];
      }
    }
    g.draw(mesh);
  }
};

int main() {
  AlloApp ericApp;
  ericApp.configureAudio(48000, 512, 2, 0);
  ericApp.start();
}

// Reads a file line by line and concatenates everything into one string. Used to feed the GLSL shader source code to the compiler.
string slurp(string fileName) {
  fstream file(fileName);
  string returnValue = "";
  while (file.good()) {
    string line;
    getline(file, line);
    returnValue += line + "\n";
  }
  return returnValue;
}
