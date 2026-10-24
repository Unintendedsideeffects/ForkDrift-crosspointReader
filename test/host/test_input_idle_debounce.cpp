#include <cstdint>

#include "doctest/doctest.h"

namespace {

struct DebounceCommit {
  uint8_t lastState = 0;
  uint8_t currentState = 0;
  uint8_t pressedEvents = 0;
  unsigned long lastDebounceTime = 0;
};

constexpr unsigned long kDebounceDelayMs = 5;

void sampleDebounce(DebounceCommit& debounce, uint8_t state, unsigned long now) {
  debounce.pressedEvents = 0;
  if (state != debounce.lastState) {
    debounce.lastDebounceTime = now;
    debounce.lastState = state;
  }
  if ((now - debounce.lastDebounceTime) > kDebounceDelayMs && state != debounce.currentState) {
    debounce.pressedEvents = static_cast<uint8_t>(state & ~debounce.currentState);
    debounce.currentState = state;
  }
}

}  // namespace

TEST_CASE("press sampled at t=0 then idle at t=50 does not commit") {
  DebounceCommit debounce;
  sampleDebounce(debounce, 1, 0);
  CHECK(debounce.pressedEvents == 0);
  sampleDebounce(debounce, 0, 50);
  CHECK(debounce.pressedEvents == 0);
}

TEST_CASE("press sampled at t=0 and t=10 commits") {
  DebounceCommit debounce;
  sampleDebounce(debounce, 1, 0);
  CHECK(debounce.pressedEvents == 0);
  sampleDebounce(debounce, 1, 10);
  CHECK(debounce.pressedEvents == 1);
}

TEST_CASE("press never sampled does not commit") {
  DebounceCommit debounce;
  CHECK(debounce.pressedEvents == 0);
  CHECK(debounce.currentState == 0);
}
