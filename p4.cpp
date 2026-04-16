// part me, part AI https://claude.ai/share/f46ee9f0-a205-44ea-b766-c2149c5173af

#include <iostream>

#include "al/app/al_App.hpp"  // al::App
#include "al/graphics/al_Shapes.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"

using namespace al;

float r() { return rnd::uniform(); } // random float 0.0 -> 1.0
float rs() { return rnd::uniformS(); } // scatters agents in both positive and negative directions (-1 -> 1 (sined))

struct Food {
  Vec3f pos;
  bool active;
};

struct MyApp : public App {
  ParameterInt N{"/N", "", 10, 2, 100}; // number of agents
  ParameterInt F{"/F", "", 1, 0, 10};
  ParameterInt K{"/K", "", 7, 1, 20}; // max neighbors
  Parameter T{"/T", "", 1.5, 0.1, 5.0}; // neighbor detection radius
  Parameter separationDist{"/sep", "", 0.5, 0.01, 2.0}; // distance at which agents repel
  Parameter worldSize{"/world", "", 5.0, 1.0, 20.0}; // size of wrapping box
  Parameter alignStrength{"/align", "", 0.02, 0.001, 0.2}; // how strongly agents match heading        
  Parameter cohesionStrength{"/cohesion", "", 0.02, 0.001, 0.2}; // how strongly agents cluster
  Parameter separationStrength{"/separation", "", 0.05, 0.001, 0.2}; // how strongly agent avoid crowding
  Parameter moveSpeed{"/speed", "", 0.02, 0.001, 0.2};// forward speed each frame          
  ParameterColor color{"/color"}; //background color
  Parameter foodRadius{"/fr", 1.5, 0.1, 5.0};
  Parameter foodEatRadius{"/foodEat", "", 0.2, 0.01, 1.0};
  Parameter foodAttrStrength{"/foodAttr", "", 0.03, 0.001, 0.2};

  // lighting setup for 3d rendering
  Light light;
  Material material;

  // shared 3d shape drawn for every agent
  Mesh mesh;

  //mesh for food
  Mesh foodMesh;

  // each Nav is a navigator: it has a position, orientation (quaternion), and movement methods
  std::vector<Nav> agent; // this creates a list of Nav's which is currently empty

  std::vector<Food> food;   

  // GUI setup - registers the three parameters so they show up as interactive controls
  void onInit() override {
    auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
    auto &gui = GUIdomain->newGUI();
    gui.add(N);
    gui.add(F);
    gui.add(K);
    gui.add(T);
    gui.add(separationDist);
    gui.add(worldSize);
    gui.add(alignStrength);
    gui.add(cohesionStrength);
    gui.add(separationStrength); 
    gui.add(moveSpeed);
    gui.add(color);
    gui.add(foodRadius);
    gui.add(foodEatRadius);
    gui.add(foodAttrStrength);
  }
  
  // // initialize agents
  // void reset(int n) {
  //   agent.clear();
  //   agent.resize(n);
  //   for (auto& a : agent) {
  //     a.pos(Vec3d(rs(), rs(), rs()));
  //     a.quat(Quatd(Vec3d(rs(), rs(), rs())).normalize());
  //   }
  // }

  // Clears the old agent list, creates n new ones, and gives each 
  // a random position and a random facing direction.
  void resetAgent(int n) {
    agent.clear();
    agent.resize(n);    
    for (int i = 0; i < n; i++) {
      agent[i].pos(Vec3d(rs(), rs(), rs())); // random position in [-1,1]³
      agent[i].quat(Quatd(Vec3d(rs(), rs(), rs())).normalize()); // random orientation
    }
  }

  // reset the food
  void resetFood(int f) {
    food.clear();
    food.resize(f);
    for (int i = 0; i < f; i++) {
      food[i].pos.x = rs();
      food[i].pos.y = rs();
      food[i].pos.z = rs();
      food[i].active = true;
    }
  }

  // One-time scene setup
  void onCreate() override {
    addCone(mesh); // build a cone shape into the mesh
    mesh.scale(1, 0.2, 1); //flatten it (make it a narrow cone)
    mesh.scale(0.2); // make it small overall
    mesh.generateNormals(); // needed for lighting to work correctly

    addSphere(foodMesh, 0.1); // small sphere mesh for food;
    foodMesh.generateNormals();

    nav().pos(0, 0, 6); // place the camera 6 units back on the Z axis
    light.pos(-2, 7, 0); // position the light above-left

    resetFood(20); // spawn 20 food items at start
  }

  // per-frame logic
  // N-change detection - if you move the N slider, agents get re-initalized
  int lastN = 0;
  void onAnimate(double dt) override {
    if (N != lastN) {
      lastN = N;
      resetAgent(N); // re-initialize agents if N slider changed
    }
    // NEW: flocking loop (replaces all old love/neighbor loops)
    for (int i = 0; i < (int)agent.size(); i++) {
      auto& me = agent[i];

      Vec3f avgPos(0, 0, 0);
      Vec3f avgHeading(0, 0, 0);
      Vec3f separationForce(0, 0, 0);
      int neighborCount = 0;

      // NEW: find up to K neighbors within distance T
      for (int j = 0; j < (int)agent.size() && neighborCount < K; j++) {
        if (i == j) continue;

        // If agent j is within radius T and we haven't hit K neighbors yet, it counts.
        Vec3f diff = agent[j].pos() - me.pos();
        float dist = diff.mag();

        // For valid neighbors, three quantities accumulate:
        if (dist < T) { // if dist is less than neighbor detection radius 
          avgPos     += agent[j].pos(); // average neighbor position (for cohesion)
          avgHeading += agent[j].uf(); // accumulate neighbor forward vectors average (for alignment)

          // NEW: separation - nudge away if too close
          if (dist < separationDist && dist > 0.0001f) {
            separationForce -= diff.normalize() * (separationDist - dist); // repulsion vector from too-close neighbors (for separation)
          }

          neighborCount++;
        }
      }

      if (neighborCount > 0) {
        avgPos     /= neighborCount;
        avgHeading /= neighborCount;

        // alignment - turn a little to match average neighbor heading
        // blend the agent's current forward direction with the neighbors' average direction
        Vec3f newHeading = me.uf() + avgHeading.normalize() * (float)alignStrength;
        me.faceToward(me.pos() + newHeading);

        // cohesion - nudge toward average neighbor position
        // nudges position toward the group's center
        Vec3f toCenter = avgPos - me.pos();
        if (toCenter.mag() > 0.0001f) {
          me.pos(me.pos() + toCenter.normalize() * (float)cohesionStrength);
        }

        // separation - nudge away from too-close neighbors
        // pushes away from any neighbor closer than separationDist
        if (separationForce.mag() > 0.0001f) {
          me.pos(me.pos() + separationForce * (float)separationStrength);
        }

        // food
        for (int j = 0; j < (int)food.size(); j++) {
          if (!food[j].active) continue;
          Vec3f diff = food[j].pos - me.pos();
          float dist = diff.mag();

          if (dist < foodRadius) {
            // AI
            // nudge agent toward food
            me.pos(me.pos() + diff.normalize() * (float)foodAttrStrength);
            // eat the food if close enough
            if (dist < foodEatRadius) {
              food[j].active = false;          // mark as eaten
              food[j].pos = Vec3f(rs(), rs(), rs()); // respawn at random position
              food[j].active = true;           // reactivate
            }
          }
        }
      }
      // move forward
      me.pos(me.pos() + me.uf() * (float)moveSpeed);

      // wrap around - AI
      Vec3f p = me.pos();
      float ws = worldSize;
      if (p.x >  ws) p.x -= 2 * ws;
      if (p.x < -ws) p.x += 2 * ws;
      if (p.y >  ws) p.y -= 2 * ws;
      if (p.y < -ws) p.y += 2 * ws;
      if (p.z >  ws) p.z -= 2 * ws;
      if (p.z < -ws) p.z += 2 * ws;
      me.pos(p);
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

    for (auto& f : food) {
      if (!f.active) continue;
      g.pushMatrix();
      g.translate(f.pos);
      g.draw(foodMesh);
      g.popMatrix();
    }
  }
};

int main() { MyApp().start(); }
