// Alert decision: picks one of three responses, about 50 times a second.
//   ALERT_HUMAN  (category 1) person approaching: after it holds 1 s, beep faster as they get closer
//   ALERT_ANIMAL (category 2) animal: no sound; history records it and how close it got
//   ALERT_NONE   (category 3) nothing to report
//
// Radar motion is the main signal. The sound model (human footsteps / animal footsteps /
// background noise) only decides what the motion is, or, with no motion, adds weak evidence.
//
//   Radar motion inside the perimeter:
//     human footsteps (any confidence)       -> 1
//     animal footsteps, high confidence      -> 2
//     animal footsteps, low confidence       -> 1 if the motion is "strong", else 3
//     background noise, high confidence      -> 1 if the motion is "strong", else 3
//     background noise, low confidence       -> 1
//     no sound result (model not running)    -> 1
//   "Strong" motion = getting closer, or anywhere inside the edge band (outer 25 % of the perimeter).
//
//   No radar motion inside the perimeter:
//     human footsteps in 3 of the last 5 results AND the radar sees something (still, or beyond the perimeter) -> 1
//     animal footsteps, high confidence, in 2 of the last 3 results     -> 2
//     anything else                                                     -> 3
//
// A new category takes over only after it has held for CONFIRM_MS (1 s); going back to
// "nothing" takes CLEAR_MS (2 s), so a brief radar dropout doesn't stop the alarm.
//
// Plain C++ with no Arduino calls, so tests/alert_logic_test.cpp runs it on a computer.
#pragma once

enum Alert { ALERT_NONE = 0, ALERT_HUMAN = 1, ALERT_ANIMAL = 2 };
enum Sound { SOUND_NONE = 0, SOUND_BACKGROUND, SOUND_HUMAN, SOUND_ANIMAL };   // SOUND_NONE = no result

// ---- tuning ----
const float HIGH_CONFIDENCE = 0.75;           // model score at or above this is "high confidence"
const float EDGE_BAND = 0.25;                 // outer 25 % of the perimeter is the edge...
const float EDGE_MIN_M = 1.0;                 // ...but at least 1 m wide
const unsigned long SOUND_RECENT_MS = 2500;   // a sound result counts for this long (model runs ~1/s)
const unsigned long CONFIRM_MS = 1000;        // a new category must hold this long to take over
const unsigned long CLEAR_MS = 2000;          // "nothing" must hold this long to end an alert
const int HUMAN_NO_MOTION_HITS = 3;           // no motion: human footsteps in 3 of the last 5 results
const unsigned long HUMAN_NO_MOTION_MS = 5500;
const int ANIMAL_NO_MOTION_HITS = 2;          // no motion: sure animal footsteps in 2 of the last 3 results
const unsigned long ANIMAL_NO_MOTION_MS = 3500;

// What the radars see right now (filled in by the sketch each loop)
struct RadarView {
  bool  motion = false;       // a moving target inside the perimeter
  bool  approaching = false;  // a moving target inside the perimeter is getting closer
  float nearest = 0;          // closest moving target inside the perimeter (m)
  bool  anyTarget = false;    // anything at any distance, moving or still
  float anyNearest = 0;       // closest of those (m)
  float perimeter = 6;        // alarm radius (m)
};

inline const char* alertWord(Alert a) { return a == ALERT_HUMAN ? "human" : a == ALERT_ANIMAL ? "animal" : "none"; }

class AlertLogic {
 public:
  Alert state = ALERT_NONE;     // the confirmed category
  unsigned long since = 0;      // when it started
  float closest = -1;           // closest distance during the current alert (m); -1 = unknown
  Alert ended = ALERT_NONE;     // the alert that just ended (set when update() returns true)
  float endedClosest = -1;      // ...and how close it got
  const char* reason = "";      // why the latest instant decision was made (for tuning)

  void addSound(Sound s, float score, unsigned long now) {
    res[pos] = { s, score, now };
    pos = (pos + 1) % RING;
    if (count < RING) count++;
  }

  // The instant decision, before the 1 s / 2 s hold.
  Alert decide(const RadarView& rv, unsigned long now) {
    Result snd = recentSound(now);
    bool high = snd.score >= HIGH_CONFIDENCE;
    if (rv.motion) {
      float edge = rv.perimeter * EDGE_BAND;
      if (edge < EDGE_MIN_M) edge = EDGE_MIN_M;
      bool strong = rv.approaching || rv.nearest <= rv.perimeter - edge;
      switch (snd.s) {
        case SOUND_HUMAN:  return why(ALERT_HUMAN, "motion + human footsteps");
        case SOUND_ANIMAL:
          if (high) return why(ALERT_ANIMAL, "motion + animal footsteps (sure)");
          return strong ? why(ALERT_HUMAN, "close or approaching motion, animal unsure")
                        : why(ALERT_NONE, "motion at the edge, animal unsure");
        case SOUND_BACKGROUND:
          if (!high) return why(ALERT_HUMAN, "motion, sound unclear");
          return strong ? why(ALERT_HUMAN, "close or approaching motion, no footsteps")
                        : why(ALERT_NONE, "motion at the edge, no footsteps");
        default:           return why(ALERT_HUMAN, "motion, no sound result");
      }
    }
    if (rv.anyTarget && hits(SOUND_HUMAN, 0, RING, HUMAN_NO_MOTION_MS, now) >= HUMAN_NO_MOTION_HITS)
      return why(ALERT_HUMAN, "steady human footsteps + radar target");
    if (hits(SOUND_ANIMAL, HIGH_CONFIDENCE, 3, ANIMAL_NO_MOTION_MS, now) >= ANIMAL_NO_MOTION_HITS)
      return why(ALERT_ANIMAL, "animal footsteps, no motion");
    return why(ALERT_NONE, "no motion");
  }

  // Call every loop. Returns true when the confirmed category changes.
  bool update(const RadarView& rv, unsigned long now) {
    Alert raw = decide(rv, now);
    if (raw != candidate) { candidate = raw; candidateSince = now; }
    bool changed = false;
    if (candidate != state && now - candidateSince >= (candidate == ALERT_NONE ? CLEAR_MS : CONFIRM_MS)) {
      ended = state; endedClosest = closest;
      state = candidate; since = now; closest = -1; changed = true;
    }
    if (state != ALERT_NONE) {
      float d = distance(rv);
      if (d > 0 && (closest < 0 || d < closest)) closest = d;
    }
    return changed;
  }

  // Distance to use for the beep rate: the nearest motion, else anything seen, else -1.
  static float distance(const RadarView& rv) {
    return rv.motion ? rv.nearest : rv.anyTarget ? rv.anyNearest : -1;
  }

 private:
  struct Result { Sound s; float score; unsigned long t; };
  static const int RING = 5;
  Result res[RING] = {};
  int pos = 0, count = 0;
  Alert candidate = ALERT_NONE;
  unsigned long candidateSince = 0;

  Alert why(Alert a, const char* text) { reason = text; return a; }

  // The k-th most recent result (0 = newest)
  const Result& recent(int k) const { return res[(pos - 1 - k + 2 * RING) % RING]; }

  // The most telling result in the last SOUND_RECENT_MS: human beats animal beats background.
  Result recentSound(unsigned long now) const {
    Result best = { SOUND_NONE, 0, 0 };
    for (int k = 0; k < count; k++) {
      const Result& x = recent(k);
      if (now - x.t > SOUND_RECENT_MS) break;
      int rank = x.s == SOUND_HUMAN ? 3 : x.s == SOUND_ANIMAL ? 2 : x.s == SOUND_BACKGROUND ? 1 : 0;
      int bestRank = best.s == SOUND_HUMAN ? 3 : best.s == SOUND_ANIMAL ? 2 : best.s == SOUND_BACKGROUND ? 1 : 0;
      if (rank > bestRank || (rank == bestRank && rank != 1 && x.score > best.score)) best = x;   // background: keep the newest
    }
    return best;
  }

  // How many of the last n results (within ms) are sound s with at least minScore
  int hits(Sound s, float minScore, int n, unsigned long ms, unsigned long now) const {
    int h = 0;
    for (int k = 0; k < count && k < n; k++) {
      const Result& x = recent(k);
      if (now - x.t > ms) break;
      if (x.s == s && x.score >= minScore) h++;
    }
    return h;
  }
};
