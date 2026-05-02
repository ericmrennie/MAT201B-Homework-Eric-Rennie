// code adapted from Karl Yerkes
// 2022-01-20

#include "al/app/al_App.hpp"
#include "al/app/al_DistributedApp.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"

using namespace al;

#include <algorithm>
#include <fstream> // opens and reads GLSL shader files
#include <vector> // resizable array type
#include <cmath> // math
#include <random> // random
#include <iostream> // for console logs
using namespace std;

// creates a random 3D vector where each component is in [-1, 1], then scales it.
// uniformS means "uniform, signed" (includes negatives)

Vec3f randomVec3f(float scale) {
  return Vec3f(rnd::uniformS(), rnd::uniformS(), rnd::uniformS()) * scale;
}

string slurp(string fileName);  // forward declaration - full definition is at the bottom. Reads an entire file into a string (used to load GLSL shaders)

const int N = 500;

struct WorldState {
  double time;
  int frame;
  Pose camera;
  Vec3f position[N];
};

// Declares GUI-controllable parameters. These show up as sliders at runtime.
struct AlloApp : DistributedAppWithState<WorldState> {
  Parameter loveAttraction{"/loveAttraction", "", 0.0, 0.0, 3.0};
  ParameterBool loveLines{"/loveLines", "", false};
  Parameter coulombs{"/coulombs", "", 0.0, -0.1, 0.1};
  Parameter springForce{"/springForce", "", 0.5, 0.1, 2.0};
  Parameter pointSize{"/pointSize", "", 2.0, 1.0, 10.0};
  Parameter timeStep{"/timeStep", "", 0.1, 0.01, 0.6};
  Parameter dragFactor{"/dragFactor", "", 0.1, 0.0, 0.9};

  // state variables
  ShaderProgram pointShader; // the GLSL program that draws particles

  //  simulation state
  Mesh mesh;  // position *is inside the mesh* mesh.vertices() are the positions. stores positions and colors

  // parallel arrays - one entry per particle
  vector<Vec3f> velocity;
  vector<Vec3f> force;
  vector<float> mass;
  vector<int> love;

  // runs before window opens. Sets up the GUI panel and registers the three sliders
  void onInit() override {
    if (isPrimary()) {
      // set up GUI
      auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
      auto &gui = GUIdomain->newGUI();
      // add parameters to GUI
      gui.add(loveAttraction);
      gui.add(loveLines);
      gui.add(coulombs);
      gui.add(springForce);
      gui.add(pointSize); 
      gui.add(timeStep);  
      gui.add(dragFactor);   
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
    mesh.primitive(Mesh::POINTS);
    int count = 0;
    while (count < N) {
      Vec3f v = randomVec3f(5); // random position in [-5, 5]^3
      if (v.mag() > 5.0) continue; // if the distance is greater than 5.0, skip the rest of the loop iteration and go back to the top

      mesh.vertex(v);
      mesh.color(randomColor()); // random hue

      // float m = rnd::uniform(3.0, 0.5);
      float m = 3 + rnd::normal() / 2; // mass: normally distributed around 3
      if (m < 0.5) m = 0.5; // clamp minimum mass
      mass.push_back(m);

      // using a simplified volume/size relationship
      mesh.texCoord(pow(m, 1.0f / 3), 0);  // s, t // encode size as cube root of mass (volume -> radius)

      // separate state arrays
      velocity.push_back(randomVec3f(0.1)); // small random initial velocity
      force.push_back(randomVec3f(1)); // random initial force kick

      // increment count
      count++;
    }

    nav().pos(0, 0, 30); // place camera 30 units back

    // populating love vector
    int count2 = 0;
    while (count2 < mesh.vertices().size()) {
      int crush = rnd::uniform(mesh.vertices().size());
      if (crush == count2) continue;
      love.push_back(crush);
      count2++;
    }
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

    // 
    //
    // Vec3f has lots of operations you might use...
    // • +=
    // • -=
    // • +
    // • -
    // • .normalize() ~ Vec3f points in the direction as it did, but has length 1
    // • .normalize(float scale) ~ same but length `scale`
    // • .mag() ~ length of the Vec3f
    // • .magSqr() ~ squared length of the Vec3f
    // • .dot(Vec3f f) 
    // • .cross(Vec3f f)

    // Hook's Law
    for (int i = 0; i < velocity.size(); i++) {
      // calculate spring force between this particle and the origin

      auto& me = mesh.vertices()[i];
      float k = springForce;
      float displacement = me.mag() - 5.0;
      force[i] += me * (-k * displacement) / me.mag();
    }


    // Coulomb's law - add pairwise repulsion forces
    for (int i = 0; i < mesh.vertices().size(); ++i) {
      auto& chargeOne = mesh.vertices()[i];
      for (int j = i + 1; j < mesh.vertices().size(); ++j) {
        auto& chargeTwo = mesh.vertices()[j];
        // i and j are a pair
        // limit large forces... if the force is too large, ignore it
        float k = coulombs;
        float distance = (chargeTwo - chargeOne).mag();
        Vec3f direction = (chargeTwo - chargeOne).normalize();
        Vec3f f = direction * (k / pow(distance, 2));
        force[i] -= f;
        force[j] += f;
      }
    }

    // love attraction logic
    for (int i = 0; i < mesh.vertices().size(); i++) {
      auto& me = mesh.vertices()[i];
      auto& crush = love[i];
      float k = loveAttraction;
      Vec3f direction = (mesh.vertices()[crush] - me).normalize();
      force[i] += direction * k;
    }

    // viscous drag
    // drag is a force opposing the current velocity, proportional to speed
    // slows particles down over time
    for (int i = 0; i < velocity.size(); i++) {
      force[i] += - velocity[i] * dragFactor; // viscous drag: F = -bv
    }

    // Numerical Integration
    //
    vector<Vec3f>& position(mesh.vertices());
    for (int i = 0; i < velocity.size(); i++) {
      // "semi-implicit" Euler integration
      // updates velocity first using the new force, then updates position using the new velocity. More stable than plain Euler.
      velocity[i] += force[i] / mass[i] * timeStep;
      position[i] += velocity[i] * timeStep;
    }

    // clear all accelerations (IMPORTANT!!)
    // otherwise forces accumulate across frames
    for (auto& a : force) a.set(0);

    for (int i = 0; i < N; i++) {
      state().position[i] = mesh.vertices()[i];
    }
    state().camera = nav();
  };

  bool onKeyDown(const Keyboard& k) override {
    // pauses/unpauses simulation
    if (k.key() == ' ') {
      freeze = !freeze;
    }

    if (k.key() == '1') {
      // introduce some "random" forces - useful for testing
      for (int i = 0; i < velocity.size(); i++) {
        // F = ma
        force[i] += randomVec3f(1);
      }
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
    g.shader().uniform("pointSize", pointSize / 100); // pass slider value to shader
    g.blending(true);
    g.blendTrans(); // transparency blending
    g.depthTesting(true);

    if(!isPrimary()) {
      for (int i = 0; i < N; i++) {
        mesh.vertices()[i] = state().position[i];
      }
    }
    g.draw(mesh);

    // lines between the particles
    if (loveLines) {
      g.color(1, 1, 0);
      Mesh lines = mesh;
      lines.primitive(Mesh::LINES);
      for (int i = 0; i < mesh.vertices().size(); i++) {
        lines.index(i, love[i]);
      }
      g.draw(lines);
    }
  }
};

int main() {
  App app;
  app.configureAudio(48000, 512, 2, 0);
  app.start();
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
