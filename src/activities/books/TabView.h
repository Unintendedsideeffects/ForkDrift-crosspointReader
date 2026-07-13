#pragma once

class RenderLock;
class Activity;

// The small lifecycle surface a child needs when it is hosted by a tab
// container. The render and loop methods intentionally match Activity so an
// existing activity can implement this with no second rendering path.
class TabView {
 public:
  virtual ~TabView() = default;

  virtual void enter() = 0;
  virtual void loop() = 0;
  virtual void render(RenderLock&&) = 0;
  virtual void exit() = 0;
  virtual Activity* asActivity() = 0;

  // Up enters the strip only at a child's top navigation boundary. This keeps
  // the approved gesture from stealing an ordinary Up action mid-list.
  virtual bool atNavigationTop() const = 0;
};
