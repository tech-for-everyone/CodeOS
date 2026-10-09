#include <cstdio>

#include "base/geometry/dimension.h"
#include "base/geometry/matrix3.h"
#include "test/mock/frameworks/core/rosen/testing_canvas.h"

using namespace OHOS::Ace;

int main() {
  // Build a simple invertible 3x3
  Matrix3 m;
  m.SetEntry(0,0,2); m.SetEntry(0,1,0); m.SetEntry(0,2,0);
  m.SetEntry(1,0,0); m.SetEntry(1,1,4); m.SetEntry(1,2,0);
  m.SetEntry(2,0,0); m.SetEntry(2,1,0); m.SetEntry(2,2,1);
  Matrix3 inv;
  if (!m.Invert(inv)) return 1;

  // Mock canvas smoke test (no rasteriser)
  Testing::TestingCanvas canvas;
  canvas.DrawRect(Testing::TestingRect(0, 0, 100, 50));
  canvas.DrawLine(Testing::TestingPoint(0, 0), Testing::TestingPoint(100, 50));

  std::printf("ace_demo: PASS\n");
  return 0;
}