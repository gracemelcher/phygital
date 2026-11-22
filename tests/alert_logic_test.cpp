// Checks phygital/alert_logic.h against the decision table, on a computer (no board needed):
//   c++ -std=c++17 -o /tmp/alert_test tests/alert_logic_test.cpp && /tmp/alert_test
#include "../phygital/alert_logic.h"
#include <cstdio>

static int failures = 0;

// Runs `secs` seconds: a sound result every second, the radar view every 20 ms.
static Alert run(RadarView rv, Sound s, float score, float secs, AlertLogic* out = nullptr) {
  AlertLogic a;
  for (unsigned long t = 1000; t <= 1000 + (unsigned long)(secs * 1000); t += 20) {
    if (t % 1000 == 0 && s != SOUND_NONE) a.addSound(s, score, t);
    a.update(rv, t);
  }
  if (out) *out = a;
  return a.state;
}

static void expect(const char* name, Alert got, Alert want) {
  printf("%s %-62s -> %s\n", got == want ? "ok  " : "FAIL", name, alertWord(got));
  if (got != want) failures++;
}

int main() {
  RadarView none;                                                            // nothing at all
  RadarView close;  close.motion = close.anyTarget = true; close.nearest = close.anyNearest = 3;
  RadarView edge;   edge.motion = edge.anyTarget = true;   edge.nearest = edge.anyNearest = 5.6;   // 6 m perimeter, edge band 4.5-6 m
  RadarView edgeIn = edge; edgeIn.approaching = true;
  RadarView still;  still.anyTarget = true; still.anyNearest = 4;            // radar sees someone standing still

  puts("Radar motion:");
  expect("human footsteps, high", run(close, SOUND_HUMAN, 0.9, 3), ALERT_HUMAN);
  expect("human footsteps, low", run(close, SOUND_HUMAN, 0.5, 3), ALERT_HUMAN);
  expect("animal footsteps, high", run(close, SOUND_ANIMAL, 0.9, 3), ALERT_ANIMAL);
  expect("animal footsteps, low, close", run(close, SOUND_ANIMAL, 0.5, 3), ALERT_HUMAN);
  expect("animal footsteps, low, at the edge", run(edge, SOUND_ANIMAL, 0.5, 3), ALERT_NONE);
  expect("animal footsteps, low, at the edge but approaching", run(edgeIn, SOUND_ANIMAL, 0.5, 3), ALERT_HUMAN);
  expect("background, high, close", run(close, SOUND_BACKGROUND, 0.95, 3), ALERT_HUMAN);
  expect("background, high, at the edge", run(edge, SOUND_BACKGROUND, 0.95, 3), ALERT_NONE);
  expect("background, high, at the edge but approaching", run(edgeIn, SOUND_BACKGROUND, 0.95, 3), ALERT_HUMAN);
  expect("background, low, at the edge", run(edge, SOUND_BACKGROUND, 0.5, 3), ALERT_HUMAN);
  expect("no sound model", run(close, SOUND_NONE, 0, 3), ALERT_HUMAN);

  puts("No radar motion:");
  expect("human footsteps, nothing on radar", run(none, SOUND_HUMAN, 0.9, 6), ALERT_NONE);
  expect("human footsteps 5 s + radar sees someone still", run(still, SOUND_HUMAN, 0.9, 6), ALERT_HUMAN);
  expect("human footsteps only 2 s + radar sees someone still", run(still, SOUND_HUMAN, 0.9, 2.5), ALERT_NONE);
  expect("animal footsteps, high", run(none, SOUND_ANIMAL, 0.9, 4), ALERT_ANIMAL);
  expect("animal footsteps, low", run(none, SOUND_ANIMAL, 0.5, 4), ALERT_NONE);
  expect("background", run(none, SOUND_BACKGROUND, 0.95, 4), ALERT_NONE);

  puts("Timing:");
  {
    AlertLogic a; RadarView rv = close; unsigned long t = 1000;
    a.addSound(SOUND_HUMAN, 0.9, t);
    for (; t < 1980; t += 20) a.update(rv, t);
    expect("human for 0.98 s: not yet alarming", a.state, ALERT_NONE);
    for (; t <= 2020; t += 20) a.update(rv, t);
    expect("human for 1 s: alarming", a.state, ALERT_HUMAN);
    for (; t < 3500; t += 20) a.update(none, t);                              // radar drops out 1.5 s
    expect("1.5 s radar dropout: still alarming", a.state, ALERT_HUMAN);
    for (; t < 5000; t += 20) a.update(none, t);
    expect("3 s with nothing: alarm ends", a.state, ALERT_NONE);
  }
  {
    AlertLogic a; RadarView rv = close; unsigned long t = 1000; bool changed = false;
    a.addSound(SOUND_ANIMAL, 0.9, t);
    for (int i = 0; t < 4000; t += 20, i++) { rv.nearest = rv.anyNearest = 5 - i * 0.02f > 2.2f ? 5 - i * 0.02f : 2.2f; if (t % 1000 == 0) a.addSound(SOUND_ANIMAL, 0.9, t); a.update(rv, t); }
    for (; t < 10000; t += 20) changed |= a.update(none, t);
    printf("%s animal episode ends and reports closest %.1f m (want 2.2)\n",
           changed && a.ended == ALERT_ANIMAL && a.endedClosest > 2.19f && a.endedClosest < 2.21f ? "ok  " : "FAIL", a.endedClosest);
    if (!(changed && a.ended == ALERT_ANIMAL && a.endedClosest > 2.19f && a.endedClosest < 2.21f)) failures++;
  }

  printf(failures ? "\n%d FAILED\n" : "\nAll passed\n", failures);
  return failures ? 1 : 0;
}
