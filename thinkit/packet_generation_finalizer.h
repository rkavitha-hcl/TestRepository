#ifndef GOOGLE_THINKIT_PACKET_GENERATION_FINALIZER_H_
#define GOOGLE_THINKIT_PACKET_GENERATION_FINALIZER_H_

namespace thinkit {

// PacketGenerationFinalizer will stop listening for packets when it goes out of
// scope.
class PacketGenerationFinalizer {
 public:
  virtual ~PacketGenerationFinalizer() = 0;
};
}  // namespace thinkit
#endif  // GOOGLE_THINKIT_PACKET_GENERATION_FINALIZER_H_
