// Per-tick input for the simulation. Produced from live devices or from a replay; the simulation
// never reads devices or the clock directly, so a replay reproduces a session exactly.
#pragma once

#include <cmath>

namespace lc::game {

struct InputFrame {
    float moveX = 0.0f;           // -1 (A) .. +1 (D), camera-relative right.
    float moveZ = 0.0f;           // -1 (S) .. +1 (W), camera-relative forward.
    float lookDx = 0.0f;          // Yaw change this tick in radians (positive turns left, toward -X at yaw 0).
    float lookDy = 0.0f;          // Pitch change this tick in radians (positive looks up).
    bool sprint = false;
    bool interactPressed = false; // Edge: E pressed during this tick.
    bool lampPressed = false;     // Edge: F pressed during this tick.

    bool IsNeutral() const {
        return moveX == 0.0f && moveZ == 0.0f && lookDx == 0.0f && lookDy == 0.0f && !sprint && !interactPressed && !lampPressed;
    }
    bool operator==(const InputFrame& o) const {
        return moveX == o.moveX && moveZ == o.moveZ && lookDx == o.lookDx && lookDy == o.lookDy && sprint == o.sprint &&
               interactPressed == o.interactPressed && lampPressed == o.lampPressed;
    }
};

}  // namespace lc::game
