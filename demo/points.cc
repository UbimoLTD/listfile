#include "base/logging.h"
#include "file/proto_writer.h"
#include "demo/points.pb.h"

int main() {
  file::ProtoWriter writer("/dev/stdout", points::Point::descriptor());
  for (int i = 0; i < 100; ++i) {
    points::Point p;
    p.set_x(rand() % 10);
    p.set_y(rand() % 10);
    CHECK(writer.Add(p).ok());
  }
}
