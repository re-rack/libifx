#include "ifx_base.h"

#include <stddef.h>

int ifx_context_init(ifx_context *context, double rate) {
  if (context == NULL) {
    return -1;
  }
  if (!isfinite(rate) || rate <= 0.0) {
    context->rate = 0.0;
    return -1;
  }
  context->rate = rate;
  return 0;
}
