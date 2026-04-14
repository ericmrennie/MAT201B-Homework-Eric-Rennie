// Claude Sonnet 4.6 - "Can you walk me through this code line by line" - where I took some comments

// steps I would take
// for each agent, search through all other agents to find those that are closer than some threshold value
// nudge away from these close neighbors

#include <iostream>

#include "al/app/al_App.hpp"  // al::App
#include "al/graphics/al_Shapes.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"

using namespace al;

float r() { return rnd::uniform(); }
float rs() { return rnd::uniformS(); }

struct MyApp : public App {
  ParameterInt N{"/N", "", 10, 2, 100};
  Parameter neighbor_distance{"/n", "", 0.1, 0.01, 1};
  ParameterColor color{"/color"};

  // lighting setup for 3d rendering
  Light light;
  Material material;

  // shared 3d shape drawn for every agent
  Mesh mesh;

  // each Nav is a navigator: it has a position, orientation (quaternion), and movement methods
  std::vector<Nav> agent; // this creates a list of Nav's which is currently empty

  // GUI setup - registers the three parameters so they show up as interactive controls
  void onInit() override {
    auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
    auto &gui = GUIdomain->newGUI();
    gui.add(N);
    gui.add(color);
    gui.add(neighbor_distance);
  }
  
  // initialize agents
  void reset(int n) {
    agent.clear();
    agent.resize(n);
    for (auto& a : agent) {
      a.pos(Vec3d(rs(), rs(), rs()));
      a.quat(Quatd(Vec3d(rs(), rs(), rs())).normalize());
    }
  }

  // One-time scene setup
  void onCreate() override {
    addCone(mesh); // build a cone shape into the mesh
    mesh.scale(1, 0.2, 1); //flatten it (make it a narrow cone)
    mesh.scale(0.2); // make it small overall
    mesh.generateNormals(); // needed for lighting to work correctly

    nav().pos(0, 0, 6); // place the camera 6 units back on the Z axis
    light.pos(-2, 7, 0); // position the light above-left
  }

  // per-frame logic
  int lastN = 0;
  void onAnimate(double dt) override {
    if (N != lastN) {
      lastN = N;
      reset(N); // re-initialize agents if N slider changed
    }

    // assign each agent to a random agent that is not itself
    

    // neighbor detection loop
    for (int i = 0; i < agent.size(); i++) {
      auto& me = agent[i];
      Vec3d sum;
      int count = 0;
      for (int j = 0; j < agent.size(); j++) {
        if (i == j) { // skip self-comparison
          continue;
        }

        auto& them = agent[j];

        // me versus them
        float distance = (me.pos() - them.pos()).mag(); // Euclidean distance
        if (distance < neighbor_distance) {
          count++;

          // them is a neighbor
          // ....
          // if me is too close, move me away
          // find center?
          sum += them.pos(); // accumulate neighbor positions
        }
        sum += me.pos();
        if (count > 1) {
          Vec3d center = sum / (count + 1);
          // if me is to far from center, move me toward center
        }
      }
    }

    // random falling in love
    for (int i = 0; i < agent.size(); i++) { // iterate throught the navs
      auto& me = agent[i]; // assign current nav to variable me 
      int j;
      do {
        j = rnd::uniform(0, (int)agent.size() - 1);
      } while (j == i); // keep finding a random value for j within the size of the nav list if j is equal to i 
      auto& them = agent[j];

      me.nudgeToward(them.pos(), 0.01);
      me.moveF(0.7);
    }

    // give some room
    // for each agent, search through all other agents to find those that are closer than some threshold value
    // nudge away from these close neighbors
    for (int i = 0; i < agent.size(); i++) {
      auto& me = agent[i];
      for (int j = 0; j < agent.size(); j++) {
        if (i == j) { // skip self-comparison
          continue;
        }

        auto& them = agent[j];

        // me versus them
        float distance = (me.pos() - them.pos()).mag(); // Euclidean distance
        if (distance < neighbor_distance) {
          me.nudgeToward(them.pos(), -0.1);
        }
      }
    }
    
    // movement
    for (auto& a : agent) {
      a.turnR(0.035); // rotate slightly to the right each frame
      a.moveF(0.7); // move forward (along facing direction)
    }

    //
    //
    for (auto& a : agent) {
      a.step(dt); // apply the accumulated movement using delta-time
    }
  }

  // rendering
  void onDraw(Graphics& g) override {
    g.clear(color); // fill background with GUI color parameter

    light.ambient(RGB(0));          // Ambient reflection for this light
    light.diffuse(RGB(1, 1, 0.5));  // Light scattered directly from light
    g.lighting(true); 
    g.light(light);
    material.specular(light.diffuse() * 0.2);  // Specular highlight, "shine"
    material.shininess(50);  // Concentration of specular component [0,128]

    g.material(material);

    //std::cout << g.modelMatrix().mElems[0] << std::endl;

    for (auto& a : agent) {
      g.pushMatrix(); // save current transform
      g.translate(a.pos()); // move to agent's position
      g.rotate(a.quat()); // orient to agent's facing direction
      g.draw(mesh); // draw the cone
      g.popMatrix(); // restore transform
    }
  }
};

int main() { MyApp().start(); }
