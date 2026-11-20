// Skips the copyright warning Dead or Alive 2 shows at every start.

#include "xdk/hle.h"

using namespace kernel;

// void ShowWarning(void), cdecl: esp -> [return]
X86_FUNC(sub_000D92C0) {
    ctx.eax = 0;
    ctx.esp += 4;  // pop the return address, as the function's ret would
}
